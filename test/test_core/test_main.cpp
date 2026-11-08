#include <unity.h>
#include <string>
#include "cam_frames.h"
#include "cam_rtp.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_subs.h"

void setUp() {}
void tearDown() {}

// ---- sensors ----

static void hold_expires_and_retrigger_extends() {
  SensorBoard b(3, 1000);
  TEST_ASSERT_FALSE(b.anyActive());
  TEST_ASSERT_TRUE(b.trigger(2, 100));
  b.tick(900);
  TEST_ASSERT_TRUE(b.active(2));
  TEST_ASSERT_TRUE(b.trigger(2, 900));
  b.tick(1500);
  TEST_ASSERT_TRUE(b.active(2));
  b.tick(1900);
  TEST_ASSERT_FALSE(b.active(2));
}

static void hold_survives_millis_wrap() {
  SensorBoard b(1, 1000);
  b.trigger(1, 0xFFFFFF00u);
  b.tick(0x00000010u);
  TEST_ASSERT_TRUE(b.active(1));
  b.tick(0x00000400u);
  TEST_ASSERT_FALSE(b.active(1));
}

static void unknown_sensors_rejected() {
  SensorBoard b(3, 1000);
  TEST_ASSERT_FALSE(b.trigger(0, 0));
  TEST_ASSERT_FALSE(b.trigger(4, 0));
  TEST_ASSERT_FALSE(b.clear(4));
}

static void frame_cycles_between_active_sensors() {
  SensorBoard b(4, 100000);
  TEST_ASSERT_EQUAL(0, b.frameIndex(0));
  b.trigger(1, 0);
  b.trigger(3, 0);
  TEST_ASSERT_EQUAL(1, b.frameIndex(0));
  TEST_ASSERT_EQUAL(3, b.frameIndex(2000));
  TEST_ASSERT_EQUAL(1, b.frameIndex(4000));
}

static void motion_repeats_true_faster_than_nvr_latch() {
  MotionSignal s(1000);
  TEST_ASSERT_TRUE(s.update(false, 0));   // first state always goes out
  TEST_ASSERT_FALSE(s.update(false, 5000));
  TEST_ASSERT_TRUE(s.update(true, 5100));  // rising edge
  TEST_ASSERT_FALSE(s.update(true, 5900));
  TEST_ASSERT_TRUE(s.update(true, 6100));  // repeat
  TEST_ASSERT_TRUE(s.update(false, 6200));  // falling edge
  TEST_ASSERT_FALSE(s.update(false, 9000));
}

// ---- subscriptions ----

static void same_consumer_is_one_subscription() {
  SubTable t;
  uint32_t first = t.addPush("http://10.1.1.1:8180/x/", 1, 0).id;
  uint32_t second = t.addPush("http://10.1.1.1:8180/x/", 1, 10).id;
  TEST_ASSERT_EQUAL(1, t.count());
  TEST_ASSERT_NULL(t.find(first));
  TEST_ASSERT_NOT_NULL(t.find(second));
}

static void subscription_lives_while_peer_talks() {
  SubTable t;
  t.addPush("http://10.1.1.1/", 7, 0);
  t.heard(7, SubTable::IDLE_MS - 1);
  t.expire(SubTable::IDLE_MS + 100);
  TEST_ASSERT_EQUAL(1, t.count());
  t.expire(2 * SubTable::IDLE_MS + 100);
  TEST_ASSERT_EQUAL(0, t.count());
}

static void full_table_replaces_oldest() {
  SubTable t;
  uint32_t oldest = t.addPull(1, 0).id;
  for (int i = 1; i < SubTable::SIZE; i++) t.addPull(1, 100 + i);
  uint32_t fresh = t.addPull(1, 200).id;
  TEST_ASSERT_EQUAL(SubTable::SIZE, t.count());
  TEST_ASSERT_NULL(t.find(oldest));
  TEST_ASSERT_NOT_NULL(t.find(fresh));
}

// ---- parsing ----

static const char SUBSCRIBE[] =
    "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"a\"><s:Header><Security><UsernameToken><Created>x</Created></UsernameToken></Security></s:Header>"
    "<s:Body xmlns:w=\"b\"><w:Subscribe><w:ConsumerReference><a:Address> http://192.0.2.5:8180/onvif_notify_server/ </a:Address></w:ConsumerReference>"
    "<w:InitialTerminationTime>PT60S</w:InitialTerminationTime></w:Subscribe></s:Body></s:Envelope>";

static void parses_action_after_security_header() { TEST_ASSERT_EQUAL_STRING("Subscribe", soapAction(SUBSCRIBE).c_str()); }

static void parses_action_without_prefix() {
  TEST_ASSERT_EQUAL_STRING("GetProfiles", soapAction("<Envelope><Body><GetProfiles/></Body></Envelope>").c_str());
}

static void parses_consumer_address() {
  TEST_ASSERT_EQUAL_STRING("http://192.0.2.5:8180/onvif_notify_server/", soapConsumerAddress(SUBSCRIBE).c_str());
}

static void parses_urls() {
  HttpUrl u;
  TEST_ASSERT_TRUE(parseHttpUrl("http://192.0.2.5:8180/a/b", u));
  TEST_ASSERT_EQUAL_STRING("192.0.2.5", u.host.c_str());
  TEST_ASSERT_EQUAL(8180, u.port);
  TEST_ASSERT_EQUAL_STRING("/a/b", u.path.c_str());
  TEST_ASSERT_TRUE(parseHttpUrl("http://192.0.2.5", u));
  TEST_ASSERT_EQUAL(80, u.port);
  TEST_ASSERT_EQUAL_STRING("/", u.path.c_str());
  TEST_ASSERT_FALSE(parseHttpUrl("https://192.0.2.5/", u));
}

static void parses_headers_and_query() {
  TEST_ASSERT_EQUAL_STRING("12", headerValue("POST / HTTP/1.1\r\nCONTENT-length:  12\r\nX: y", "content-length").c_str());
  TEST_ASSERT_EQUAL_STRING("", headerValue("POST / HTTP/1.1\r\nX: y", "cseq").c_str());
  TEST_ASSERT_EQUAL_STRING("3", queryParam("/sensor?id=3&state=clear", "id").c_str());
  TEST_ASSERT_EQUAL_STRING("clear", queryParam("/sensor?id=3&state=clear", "state").c_str());
  TEST_ASSERT_EQUAL_STRING("", queryParam("/sensor", "id").c_str());
}

// ---- replies the NVR is known to need ----

static void answers_every_call_the_nvr_made() {
  SoapContext c;
  c.host = "192.0.2.9:8899";
  c.rtspPort = 8554;
  c.serial = "abc";
  c.now = 1800000000;
  const char* calls[] = {"GetSystemDateAndTime", "GetCapabilities", "GetProfiles", "GetDeviceInformation", "GetStreamUri",
                         "GetVideoEncoderConfiguration", "GetVideoEncoderConfigurationOptions", "GetAudioSources", "SetSynchronizationPoint"};
  for (const char* call : calls) TEST_ASSERT_FALSE_MESSAGE(soapDeviceReply(call, c).empty(), call);
  TEST_ASSERT_TRUE(soapDeviceReply("NoSuchAction", c).empty());
}

static void stream_uri_uses_the_address_the_client_used() {
  SoapContext c;
  c.host = "192.0.2.9:8899";
  c.rtspPort = 8554;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, soapDeviceReply("GetStreamUri", c).find("rtsp://192.0.2.9:8554/stream"));
}

static void notify_carries_both_topics() {
  std::string n = soapNotify(true, false, 1800000000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, n.find("tns1:VideoSource/MotionAlarm"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, n.find("Name=\"State\" Value=\"true\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, n.find("tns1:RuleEngine/CellMotionDetector/Motion"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, n.find("Name=\"IsMotion\" Value=\"true\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, n.find("PropertyOperation=\"Changed\""));
  TEST_ASSERT_EQUAL_STRING("Notify", soapAction(n).c_str());
}

// ---- RTP ----

static void rtp_reassembles_to_the_original_nals() {
  const CamFrame& f = CAM_FRAMES[1];
  RtpState st;
  st.seq = 0xFFFE;  // wraps during the frame
  std::string wire;
  rtpAppendFrame(f, st, 4, CAM_FPS, wire);

  std::string nals, expect;
  for (int i = 0; i < f.count; i++) expect.append((const char*)f.nals[i].data, f.nals[i].len);
  size_t pos = 0;
  int markers = 0;
  uint16_t seq = 0xFFFE;
  while (pos < wire.size()) {
    TEST_ASSERT_EQUAL('$', wire[pos]);
    TEST_ASSERT_EQUAL(4, wire[pos + 1]);
    size_t len = ((uint8_t)wire[pos + 2] << 8) | (uint8_t)wire[pos + 3];
    TEST_ASSERT_LESS_OR_EQUAL(1400 + 14, len);
    const char* p = wire.data() + pos + 4;
    TEST_ASSERT_EQUAL_UINT16(seq++, (uint16_t)(((uint8_t)p[2] << 8) | (uint8_t)p[3]));
    bool marker = (uint8_t)p[1] & 0x80;
    markers += marker;
    const char* payload = p + 12;
    size_t size = len - 12;
    if ((payload[0] & 0x1F) == 28) {
      if ((uint8_t)payload[1] & 0x80) nals += (char)((payload[0] & 0xE0) | (payload[1] & 0x1F));
      nals.append(payload + 2, size - 2);
    } else {
      nals.append(payload, size);
    }
    pos += 4 + len;
  }
  TEST_ASSERT_EQUAL(1, markers);
  TEST_ASSERT_TRUE(nals == expect);
  TEST_ASSERT_EQUAL_UINT32(90000 / CAM_FPS, st.ts);
}

static void base64_matches_known_values() {
  TEST_ASSERT_EQUAL_STRING("TWE=", base64((const uint8_t*)"Ma", 2).c_str());
  TEST_ASSERT_EQUAL_STRING("TWFu", base64((const uint8_t*)"Man", 3).c_str());
  TEST_ASSERT_EQUAL_STRING("TQ==", base64((const uint8_t*)"M", 1).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(hold_expires_and_retrigger_extends);
  RUN_TEST(hold_survives_millis_wrap);
  RUN_TEST(unknown_sensors_rejected);
  RUN_TEST(frame_cycles_between_active_sensors);
  RUN_TEST(motion_repeats_true_faster_than_nvr_latch);
  RUN_TEST(same_consumer_is_one_subscription);
  RUN_TEST(subscription_lives_while_peer_talks);
  RUN_TEST(full_table_replaces_oldest);
  RUN_TEST(parses_action_after_security_header);
  RUN_TEST(parses_action_without_prefix);
  RUN_TEST(parses_consumer_address);
  RUN_TEST(parses_urls);
  RUN_TEST(parses_headers_and_query);
  RUN_TEST(answers_every_call_the_nvr_made);
  RUN_TEST(stream_uri_uses_the_address_the_client_used);
  RUN_TEST(notify_carries_both_topics);
  RUN_TEST(rtp_reassembles_to_the_original_nals);
  RUN_TEST(base64_matches_known_values);
  return UNITY_END();
}
