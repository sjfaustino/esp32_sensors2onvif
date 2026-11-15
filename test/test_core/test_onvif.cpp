// What the NVR sees: ONVIF requests and answers, event subscriptions, RTP and MQTT.
#include "tests.h"

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

static void other_clients_cannot_push_out_the_nvr() {
  SubTable t;
  t.protect(9);
  uint32_t nvr = t.addPush("http://9/a", 9, 0).id;     // the oldest of all
  for (int i = 0; i < 50; i++) t.addPull(5, 100 + i);  // another client subscribing again and again
  TEST_ASSERT_NOT_NULL(t.find(nvr));
  TEST_ASSERT_EQUAL(SubTable::SIZE, t.count());
}

static const char SUBSCRIBE[] =
    "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"a\"><s:Header><Security><UsernameToken><Created>x</Created></UsernameToken></Security></s:Header>"
    "<s:Body xmlns:w=\"b\"><w:Subscribe><w:ConsumerReference><a:Address> http://192.0.2.5:8180/onvif_notify_server/ "
    "</a:Address></w:ConsumerReference>"
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

// NVRs format their requests differently; the same meaning must read the same.
static void xml_is_read_as_xml() {
  const char* forms[] = {
      "<s:Envelope><s:Body><tev:Subscribe><wsnt:ConsumerReference><wsa:Address>http://10.0.0.5/e</wsa:Address></wsnt:ConsumerReference></"
      "tev:Subscribe></s:Body></s:Envelope>",
      "<?xml version=\"1.0\"?>\r\n<Envelope>\n  <Body>\n    <Subscribe>\n      <ConsumerReference>\n        <Address>\n http://10.0.0.5/e "
      "\n</Address>\n"
      "      </ConsumerReference>\n    </Subscribe>\n  </Body>\n</Envelope>",
      "<s:Envelope xmlns:s=\"a>b\"><!-- a > comment --><s:Body "
      "note='1>0'><Subscribe><ConsumerReference><Address><![CDATA[http://10.0.0.5/e]]></Address>"
      "</ConsumerReference></Subscribe></s:Body></s:Envelope>",
      "<!DOCTYPE x [<!ENTITY y \"z\">]><Envelope><Body><Subscribe><ConsumerReference><Address>http://10.0.0.5/<!-- -->e</Address></ConsumerReference>"
      "</Subscribe></Body></Envelope>",
  };
  for (const char* f : forms) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Subscribe", soapAction(f).c_str(), f);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("http://10.0.0.5/e", soapConsumerAddress(f).c_str(), f);
  }
  TEST_ASSERT_EQUAL_STRING("a<b&c", xmlFirstText("<Password><![CDATA[a<b&c]]></Password>", "Password").c_str());  // as it is, not unescaped
  TEST_ASSERT_EQUAL_STRING("", xmlFirstText("<Password>never closed", "Password").c_str());
  TEST_ASSERT_EQUAL_STRING("", soapAction("<Body><!-- never closed").c_str());
}

static void answers_every_call_the_nvr_made() {
  SoapContext c;
  c.host = "192.0.2.9:8899";
  c.rtspPort = 8554;
  c.serial = "abc";
  c.now = 1800000000;
  const char* calls[] = {"GetSystemDateAndTime",
                         "GetCapabilities",
                         "GetProfiles",
                         "GetDeviceInformation",
                         "GetStreamUri",
                         "GetVideoEncoderConfiguration",
                         "GetVideoEncoderConfigurationOptions",
                         "GetAudioSources",
                         "SetSynchronizationPoint"};
  for (const char* call : calls) TEST_ASSERT_FALSE_MESSAGE(soapDeviceReply(call, c).empty(), call);
  TEST_ASSERT_TRUE(soapDeviceReply("NoSuchAction", c).empty());
}

static void stream_uri_uses_the_address_the_client_used() {
  SoapContext c;
  c.host = "192.0.2.9:8899";
  c.rtspPort = 8554;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, soapDeviceReply("GetStreamUri", c).find("rtsp://192.0.2.9:8554/stream"));
}

static void reports_firmware_version_and_time_source() {
  SoapContext c;
  c.firmwareVersion = "9.8.7";
  TEST_ASSERT_NOT_EQUAL(std::string::npos, soapDeviceReply("GetDeviceInformation", c).find("<tds:FirmwareVersion>9.8.7<"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, soapDeviceReply("GetSystemDateAndTime", c).find("<tt:DateTimeType>NTP<"));
  c.timeFromNtp = false;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, soapDeviceReply("GetSystemDateAndTime", c).find("<tt:DateTimeType>Manual<"));
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

static uint32_t rtpTime(const std::string& wire) {
  const uint8_t* p = (const uint8_t*)wire.data() + 4 + 4;
  return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static void rtp_time_follows_the_clock_when_frames_are_skipped() {
  RtpState st;
  std::string wire;
  rtpAppendFrame(CAM_FRAMES[0], st, 0, 0xFFFFFF00u, wire);  // the millisecond count is about to wrap
  TEST_ASSERT_EQUAL_UINT32(0, rtpTime(wire));
  wire.clear();
  rtpAppendFrame(CAM_FRAMES[0], st, 0, 0xFFFFFF00u + 1500, wire);  // two frames were skipped
  TEST_ASSERT_EQUAL_UINT32(1500 * 90, rtpTime(wire));
}

static void rtp_reassembles_to_the_original_nals() {
  const CamFrame& f = CAM_FRAMES[1];
  RtpState st;
  st.seq = 0xFFFE;  // wraps during the frame
  std::string wire;
  rtpAppendFrame(f, st, 4, 1000, wire);

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
  TEST_ASSERT_EQUAL_UINT32(0, st.ts);  // the first frame starts the clock
}

// The bytes MQTT 3.1.1 specifies, worked out by hand from the standard.
static void mqtt_packets_match_the_standard() {
  std::string c = camMqttConnect("c", "", "", "w", "x", 60);
  TEST_ASSERT_EQUAL_STRING(
      "1013"
      "00044d51545404"
      "2e"
      "003c"
      "000163"
      "000177"
      "000178",
      camHex((const uint8_t*)c.data(), c.size()).c_str());
  c = camMqttConnect("c", "u", "p", "w", "x", 60);
  TEST_ASSERT_EQUAL_STRING(
      "1019"
      "00044d51545404"
      "ee"
      "003c"
      "000163"
      "000177"
      "000178"
      "000175"
      "000170",
      camHex((const uint8_t*)c.data(), c.size()).c_str());
  std::string p = camMqttPublish("t", "hi", true, 0);
  TEST_ASSERT_EQUAL_STRING(
      "3105000174"
      "6869",
      camHex((const uint8_t*)p.data(), p.size()).c_str());
  p = camMqttPublish("t", "hi", false, 0x0102);
  TEST_ASSERT_EQUAL_STRING(
      "3207000174"
      "0102"
      "6869",
      camHex((const uint8_t*)p.data(), p.size()).c_str());
  p = camMqttPublish("t", std::string(200, 'a'), false, 0);  // a length of 203 takes two bytes
  TEST_ASSERT_EQUAL_STRING("30cb01", camHex((const uint8_t*)p.data(), 3).c_str());
  TEST_ASSERT_EQUAL(206, (int)p.size());
  TEST_ASSERT_EQUAL(206, (int)camMqttPacketSize((const uint8_t*)p.data(), p.size()));
  TEST_ASSERT_EQUAL(0, (int)camMqttPacketSize((const uint8_t*)p.data(), 2));  // the length is not all there yet
  const uint8_t ack[] = {0x20, 0x02, 0x00, 0x00}, broken[] = {0x30, 0xff, 0xff, 0xff, 0xff, 0x01};
  TEST_ASSERT_EQUAL(4, (int)camMqttPacketSize(ack, 4));
  TEST_ASSERT_EQUAL(0, (int)camMqttPacketSize(ack, 1));
  TEST_ASSERT_EQUAL(-1, (int)camMqttPacketSize(broken, sizeof(broken)));
  TEST_ASSERT_EQUAL_STRING("c000", camHex((const uint8_t*)camMqttPing().data(), 2).c_str());
  std::string s = camMqttSubscribe("t", 1);
  TEST_ASSERT_EQUAL_STRING(
      "8206"
      "0001"
      "000174"
      "00",
      camHex((const uint8_t*)s.data(), s.size()).c_str());
  // A command from Home Assistant, at QoS 0 and at QoS 1, read back to its topic and payload.
  for (uint16_t id : {0, 7}) {
    std::string in = camMqttPublish("a/set", "OFF", false, id);
    CamMqttMessage m;
    TEST_ASSERT_TRUE(camMqttReadPublish((const uint8_t*)in.data(), in.size(), m));
    TEST_ASSERT_EQUAL_STRING("a/set", std::string(m.topic, m.topicLen).c_str());
    TEST_ASSERT_EQUAL_STRING("OFF", std::string(m.payload, m.payloadLen).c_str());
    TEST_ASSERT_FALSE(camMqttReadPublish((const uint8_t*)in.data(), in.size() - 1, m));  // cut short
  }
  CamMqttMessage none;
  TEST_ASSERT_FALSE(camMqttReadPublish(ack, 4, none));  // not a PUBLISH
  // The settings a backup may bring back.
  TEST_ASSERT_TRUE(camBackupValueValid("net.mqhost", "192.168.1.10"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.mqhost", ""));
  TEST_ASSERT_FALSE(camBackupValueValid("net.mqhost", "broker.lan"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.mqport", "1883"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.mqport", "0"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.mquser", "home assistant"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.mqpw", "line\nbreak"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.mqpw", std::string(65, 'a')));
}

void runOnvifTests() {
  RUN_TEST(xml_is_read_as_xml);
  RUN_TEST(same_consumer_is_one_subscription);
  RUN_TEST(subscription_lives_while_peer_talks);
  RUN_TEST(full_table_replaces_oldest);
  RUN_TEST(other_clients_cannot_push_out_the_nvr);
  RUN_TEST(parses_action_after_security_header);
  RUN_TEST(parses_action_without_prefix);
  RUN_TEST(parses_consumer_address);
  RUN_TEST(parses_urls);
  RUN_TEST(parses_headers_and_query);
  RUN_TEST(answers_every_call_the_nvr_made);
  RUN_TEST(stream_uri_uses_the_address_the_client_used);
  RUN_TEST(reports_firmware_version_and_time_source);
  RUN_TEST(notify_carries_both_topics);
  RUN_TEST(rtp_reassembles_to_the_original_nals);
  RUN_TEST(mqtt_packets_match_the_standard);
  RUN_TEST(rtp_time_follows_the_clock_when_frames_are_skipped);
}
