#include <unity.h>
#include <string.h>
#include <string>
#include "cam_auth.h"
#include "cam_events.h"
#include "cam_frames.h"
#include "cam_mqtt.h"
#include "cam_rtp.h"
#include "cam_server.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
#include "cam_subs.h"
#include "cam_x25519.h"

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
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  TEST_ASSERT_FALSE(b.shown(0, id, direction, delayed));
  b.trigger(1, 0);
  b.trigger(3, 0);
  TEST_ASSERT_TRUE(b.shown(0, id, direction, delayed));
  TEST_ASSERT_EQUAL(1, id);
  TEST_ASSERT_TRUE(b.shown(2000, id, direction, delayed));
  TEST_ASSERT_EQUAL(3, id);
  TEST_ASSERT_TRUE(b.shown(4000, id, direction, delayed));
  TEST_ASSERT_EQUAL(1, id);
}

static void shown_sensor_carries_direction_and_delay() {
  SensorBoard b(2, 100000);
  b.trigger(2, 0, 3, true);
  int id = 0;
  uint8_t direction = 0;
  bool delayed = false;
  TEST_ASSERT_TRUE(b.shown(0, id, direction, delayed));
  TEST_ASSERT_EQUAL(2, id);
  TEST_ASSERT_EQUAL(3, direction);
  TEST_ASSERT_TRUE(delayed);
}

static void classifies_storage_paths() {
  const char* mac = "AA-BB-CC-DD-EE-01";
  TEST_ASSERT_EQUAL(CAM_PATH_CLEAR, camClassifyPath("clear.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_NODE, camClassifyPath(mac));
  TEST_ASSERT_EQUAL(CAM_PATH_SENSOR, camClassifyPath(std::string(mac) + "/1"));
  TEST_ASSERT_EQUAL(CAM_PATH_SENSOR, camClassifyPath(std::string(mac) + "/255"));
  TEST_ASSERT_EQUAL(CAM_PATH_FILE, camClassifyPath(std::string(mac) + "/1/active.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_FILE, camClassifyPath(std::string(mac) + "/1/rtl-delayed.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath("text.json"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath(std::string(mac) + "/1/text.json"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath(std::string(mac) + "/settings.txt"));  // the node's own settings
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath(std::string(mac) + "/name.txt"));
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath(std::string(mac) + "/text.json"));
  const char* bad[] = {"", "/", "..", "../x", "clear.h264/x", "aa-bb-cc-dd-ee-01", "AA:BB:CC:DD:EE:01", "AA-BB-CC-DD-EE-01/",
                       "AA-BB-CC-DD-EE-01/0", "AA-BB-CC-DD-EE-01/256", "AA-BB-CC-DD-EE-01/01", "AA-BB-CC-DD-EE-01/..",
                       "AA-BB-CC-DD-EE-01/1/..", "AA-BB-CC-DD-EE-01/1/evil.h264", "AA-BB-CC-DD-EE-01/1/active.h264.tmp",
                       "AA-BB-CC-DD-EE-01/1/active.h264/x", "../AA-BB-CC-DD-EE-01/1/active.h264"};
  for (const char* p : bad) TEST_ASSERT_EQUAL_MESSAGE(CAM_PATH_INVALID, camClassifyPath(p), p);
}

static void parses_and_prints_macs() {
  uint8_t mac[6];
  TEST_ASSERT_TRUE(camParseMac("84-F7-03-DF-03-62", mac));
  TEST_ASSERT_EQUAL_HEX8(0x84, mac[0]);
  TEST_ASSERT_EQUAL_HEX8(0x62, mac[5]);
  TEST_ASSERT_EQUAL_STRING("84-F7-03-DF-03-62", camMacText(mac).c_str());
  TEST_ASSERT_FALSE(camParseMac("84-F7-03-DF-03", mac));
  TEST_ASSERT_FALSE(camParseMac("84:F7:03:DF:03:62", mac));
}

static void names_state_files() {
  TEST_ASSERT_EQUAL_STRING("active.h264", camStateFile(0, false).c_str());
  TEST_ASSERT_EQUAL_STRING("approaching-delayed.h264", camStateFile(1, true).c_str());
  TEST_ASSERT_EQUAL_STRING("rtl.h264", camStateFile(4, false).c_str());
}

static void splits_picture_files() {
  const uint8_t ok[] = {0, 0, 0, 1, 0x67, 1, 2, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x06, 9, 0, 0, 1, 0x65, 4, 5, 6};
  CamNal n[3];
  TEST_ASSERT_TRUE(camParseFrame(ok, sizeof(ok), n));
  TEST_ASSERT_EQUAL(3, n[0].len);
  TEST_ASSERT_EQUAL_HEX8(0x67, n[0].data[0]);
  TEST_ASSERT_EQUAL(2, n[1].len);  // the zero of the following 4-byte start code is not part of it
  TEST_ASSERT_EQUAL_HEX8(0x68, n[1].data[0]);
  TEST_ASSERT_EQUAL(4, n[2].len);
  TEST_ASSERT_EQUAL_HEX8(0x65, n[2].data[0]);

  const uint8_t noIdr[] = {0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2};
  const uint8_t twoIdr[] = {0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2, 0, 0, 1, 0x65, 3, 0, 0, 1, 0x65, 4};
  const uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8};
  TEST_ASSERT_FALSE(camParseFrame(noIdr, sizeof(noIdr), n));
  TEST_ASSERT_FALSE(camParseFrame(twoIdr, sizeof(twoIdr), n));
  TEST_ASSERT_FALSE(camParseFrame(garbage, sizeof(garbage), n));
}

static void accepts_sps_differing_only_in_vui() {
  uint8_t hub[] = {0x67, 0x42, 0xc0, 0x1e, 0xd9, 0x00, 0xa0, 0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x02, 0x0f, 0x16, 0x2e, 0x48};
  // The header ffmpeg.wasm produced in a real browser.
  uint8_t browser[] = {0x67, 0x42, 0xc0, 0x1e, 0xd9, 0x00, 0xa0, 0x2f, 0xf9, 0x61, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x02, 0x0f, 0x16, 0x2e, 0x48};
  CamNal a{hub, sizeof(hub)}, b{browser, sizeof(browser)};
  TEST_ASSERT_FALSE(camSameNal(a, b));
  TEST_ASSERT_TRUE(camCompatibleSps(a, b));
  TEST_ASSERT_TRUE(camCompatibleSps(a, CAM_FRAMES[0].nals[0]));

  uint8_t changed[sizeof(browser)];
  CamNal c{changed, sizeof(changed)};
  for (int index : {1, 2, 3, 4, 5, 6, 7}) {  // profile, constraints, level, and the fields up to the picture size
    memcpy(changed, browser, sizeof(browser));
    changed[index] ^= 0x10;
    TEST_ASSERT_FALSE_MESSAGE(camCompatibleSps(a, c), std::to_string(index).c_str());
  }
  CamNal empty{browser, 0};
  TEST_ASSERT_FALSE(camCompatibleSps(a, empty));
}

static void builtin_pictures_are_valid() {
  for (int f = 0; f < 2; f++) {
    TEST_ASSERT_EQUAL(3, CAM_FRAMES[f].count);
    TEST_ASSERT_EQUAL(7, CAM_FRAMES[f].nals[0].data[0] & 31);
    TEST_ASSERT_EQUAL(8, CAM_FRAMES[f].nals[1].data[0] & 31);
    TEST_ASSERT_EQUAL(5, CAM_FRAMES[f].nals[2].data[0] & 31);
  }
  TEST_ASSERT_TRUE(camSameNal(CAM_FRAMES[0].nals[0], CAM_FRAMES[1].nals[0]));
}

static void backup_values_follow_the_page_rules() {
  TEST_ASSERT_TRUE(camBackupValueValid("net.web", "80"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.web", "0"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.tzoff", "-180"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzoff", "99999"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzoff", "10"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzdst", "7"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.rbday", "-1"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.rbmin", "1440"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.name", "esp-alarmhub"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.name", "bad name"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.name", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.enkey", "0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.enkey", "short"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hook", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hook", "http://192.168.1.10:8123/api/webhook/hub"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "https://192.168.1.10/x"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "http://homeassistant.local/x"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "http://192.168.1.10/a b"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hookal", "1"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.enuns", "yes"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.token", "0123abcd"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.token", "a\"b"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.ip", "192.168.8.192"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.ip", "192.168.8"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.nvrip", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.webpw", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("wifi.password2", ""));
  TEST_ASSERT_FALSE(camBackupValueValid("wifi.password", "short"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.ntp", "2"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.unknown", "1"));
}

static std::string hex(const std::string& raw) { return camHex((const uint8_t*)raw.data(), raw.size()); }

static void hmac_sha1_matches_rfc_2202() {
  TEST_ASSERT_EQUAL_STRING("b617318655057264e28bc0b6fb378c8ef146be00", hex(camHmacSha1(std::string(20, '\x0b'), "Hi There")).c_str());
  TEST_ASSERT_EQUAL_STRING("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", hex(camHmacSha1("Jefe", "what do ya want for nothing?")).c_str());
  TEST_ASSERT_EQUAL_STRING("aa4ae5e15272d00e95705637ce8a3b55ed402112", hex(camHmacSha1(std::string(80, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")).c_str());
}

// FIPS 180 examples, and the same hash whatever pieces the message arrives in, across block edges.
static void sha1_in_pieces_matches() {
  TEST_ASSERT_EQUAL_STRING("a9993e364706816aba3e25717850c26c9cd0d89d", hex(camSha1("abc")).c_str());
  TEST_ASSERT_EQUAL_STRING("84983e441c3bd26ebaae4aa1f95129e5e54670f1", hex(camSha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")).c_str());
  TEST_ASSERT_EQUAL_STRING("da39a3ee5e6b4b0d3255bfef95601890afd80709", hex(camSha1("")).c_str());
  std::string text;
  for (int i = 0; i < 300; i++) text += (char)(i * 7);
  for (size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 300u}) {
    CamSha1 s;
    for (size_t at = 0; at < text.size(); at += piece) s.add(text.data() + at, text.size() - at < piece ? text.size() - at : piece);
    uint8_t out[20];
    s.finish(out);
    TEST_ASSERT_EQUAL_STRING(hex(camSha1(text)).c_str(), camHex(out, 20).c_str());
  }
  // Lengths where the padding does and does not need a block of its own, checked against Python's hashlib.
  TEST_ASSERT_EQUAL_STRING("c1c8bbdc22796e28c0e15163d20899b65621d65a", hex(camSha1(std::string(55, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("c2db330f6083854c99d4b5bfb6e8f29f201be699", hex(camSha1(std::string(56, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("03f09f5b158a7a8cdad920bddc29b81c18a551f5", hex(camSha1(std::string(63, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("0098ba824b5c16427bd7a1122a5a442a25ec644d", hex(camSha1(std::string(64, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56", hex(camSha1(std::string(119, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("f34c1488385346a55709ba056ddd08280dd4c6d6", hex(camSha1(std::string(120, 'a'))).c_str());
}

// The bytes MQTT 3.1.1 specifies, worked out by hand from the standard.
static void mqtt_packets_match_the_standard() {
  std::string c = camMqttConnect("c", "", "", "w", "x", 60);
  TEST_ASSERT_EQUAL_STRING("1013" "00044d51545404" "2e" "003c" "000163" "000177" "000178", camHex((const uint8_t*)c.data(), c.size()).c_str());
  c = camMqttConnect("c", "u", "p", "w", "x", 60);
  TEST_ASSERT_EQUAL_STRING("1019" "00044d51545404" "ee" "003c" "000163" "000177" "000178" "000175" "000170", camHex((const uint8_t*)c.data(), c.size()).c_str());
  std::string p = camMqttPublish("t", "hi", true, 0);
  TEST_ASSERT_EQUAL_STRING("3105000174" "6869", camHex((const uint8_t*)p.data(), p.size()).c_str());
  p = camMqttPublish("t", "hi", false, 0x0102);
  TEST_ASSERT_EQUAL_STRING("3207000174" "0102" "6869", camHex((const uint8_t*)p.data(), p.size()).c_str());
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
  TEST_ASSERT_EQUAL_STRING("8206" "0001" "000174" "00", camHex((const uint8_t*)s.data(), s.size()).c_str());
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

// RFC 7748's test vectors: section 5.2, and the key agreement of section 6.1.
static void x25519_matches_rfc_7748() {
  auto bytes = [](const char* hexText, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) {
      unsigned v = 0;
      sscanf(hexText + 2 * i, "%2x", &v);
      out[i] = (uint8_t)v;
    }
  };
  uint8_t k[32], u[32], out[32];
  bytes("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
  bytes("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
  camX25519(out, k, u);
  TEST_ASSERT_EQUAL_STRING("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", camHex(out, 32).c_str());
  uint8_t alice[32], bob[32], alicePub[32], bobPub[32], s1[32], s2[32];
  bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice);
  bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob);
  camX25519Public(alicePub, alice);
  camX25519Public(bobPub, bob);
  TEST_ASSERT_EQUAL_STRING("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", camHex(alicePub, 32).c_str());
  TEST_ASSERT_EQUAL_STRING("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", camHex(bobPub, 32).c_str());
  camX25519(s1, alice, bobPub);
  camX25519(s2, bob, alicePub);
  TEST_ASSERT_EQUAL_STRING("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", camHex(s1, 32).c_str());
  TEST_ASSERT_EQUAL_STRING(camHex(s1, 32).c_str(), camHex(s2, 32).c_str());
}

// Every event kind has a name of its own, so none is shown or sent as a plain "alert", and every name is in the list
// Home Assistant is told to expect.
static void every_event_kind_has_its_own_name() {
  std::vector<const char*> names = camEventNames();
  for (int kind = 1; kind < CAM_EVENT_KIND_COUNT; kind++) {
    const char* name = camEventName((uint8_t)kind, true, false);
    TEST_ASSERT_TRUE_MESSAGE(strcmp(name, "alert") != 0, ("kind " + std::to_string(kind) + " has no name in camEventName").c_str());
    for (int variant = 0; variant < 4; variant++) {
      const char* each = camEventName((uint8_t)kind, (variant & 1) == 0, (variant & 2) != 0);
      bool listed = false;
      for (const char* n : names) listed = listed || strcmp(n, each) == 0;
      TEST_ASSERT_TRUE_MESSAGE(listed, each);
    }
  }
  for (size_t i = 0; i < names.size(); i++)
    for (size_t k = i + 1; k < names.size(); k++) TEST_ASSERT_TRUE(strcmp(names[i], names[k]) != 0);
}

// After a restart: a timed disarm ends when its time has passed, and at once when how long is left cannot be known.
static void a_timed_disarm_ends_safely_after_a_restart() {
  const time_t now = 1800000000;  // a clock that is set
  const uint32_t wait = 120000;
  TEST_ASSERT_FALSE(camTimedDisarmOver(0, now, 0, wait));          // no end: until armed again
  TEST_ASSERT_FALSE(camTimedDisarmOver(now + 60, now, 0, wait));   // a minute left
  TEST_ASSERT_TRUE(camTimedDisarmOver(now - 1, now, 0, wait));     // over while the hub was off
  TEST_ASSERT_TRUE(camTimedDisarmOver(1, now, 0, wait));           // began without a clock
  TEST_ASSERT_FALSE(camTimedDisarmOver(now + 60, 0, 1000, wait));  // no clock yet: waits for it a while
  TEST_ASSERT_TRUE(camTimedDisarmOver(now + 60, 0, wait, wait));   // and arms once it has waited long enough
  TEST_ASSERT_EQUAL_STRING("30 min", camMinutesText(30).c_str());
  TEST_ASSERT_EQUAL_STRING("2 h", camMinutesText(120).c_str());
  TEST_ASSERT_EQUAL_STRING("1 day", camMinutesText(1440).c_str());
  TEST_ASSERT_EQUAL_STRING("7 days", camMinutesText(10080).c_str());
  TEST_ASSERT_TRUE(camBackupValueValid("net.rearm", "720"));
  CamSensorSettings zone;  // the zone, and that it is kept with the other settings
  TEST_ASSERT_TRUE(camParseSensorSettings("hold=20\nzone=interior\n", zone));
  TEST_ASSERT_TRUE(zone.interior);
  TEST_ASSERT_EQUAL_STRING("hold=20\nzone=interior\n", camSensorSettingsText(zone).c_str());
  TEST_ASSERT_TRUE(camParseSensorSettings("zone=perimeter\n", zone) && !zone.interior);
  TEST_ASSERT_FALSE(camParseSensorSettings("zone=garden\n", zone));
  TEST_ASSERT_FALSE(camBackupValueValid("net.rearm", "10081"));
}

static void signed_messages_are_checked() {
  const uint8_t mac[6] = {0x84, 0xF7, 0x03, 0xDF, 0x03, 0x62};
  uint8_t p[13 + 8] = {2, 1, 1, 90, 4, 3, 2, 1, 0, 0, 0, 0, 0};
  std::string sig = camPacketSignature(mac, p, 13, "key-1");
  memcpy(p + 13, sig.data(), 8);
  SensorReport r;
  TEST_ASSERT_EQUAL(CAM_PACKET_SIGNED, camCheckPacket(mac, p, sizeof(p), "key-1", r));
  TEST_ASSERT_EQUAL(0x01020304u, r.counter);
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "key-2", r));
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "", r));
  const uint8_t other[6] = {0x84, 0xF7, 0x03, 0xDF, 0x03, 0x63};
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(other, p, sizeof(p), "key-1", r));  // copied to another node
  p[3] = 10;  // altered after signing
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "key-1", r));
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckPacket(mac, p, 13, "key-1", r));  // version 2 without its signature
  p[0] = 1;
  TEST_ASSERT_EQUAL(CAM_PACKET_UNSIGNED, camCheckPacket(mac, p, 13, "key-1", r));
}

static void sensor_settings_round_trip_and_refuse_bad_values() {
  CamSensorSettings s;
  TEST_ASSERT_TRUE(camParseSensorSettings("hold=30\nquiet=1320-420\n", s));
  TEST_ASSERT_EQUAL(30, s.holdS);
  TEST_ASSERT_EQUAL(1320, s.quietFrom);
  TEST_ASSERT_EQUAL(420, s.quietTo);
  TEST_ASSERT_EQUAL_STRING("hold=30\nquiet=1320-420\n", camSensorSettingsText(s).c_str());
  TEST_ASSERT_TRUE(camParseSensorSettings("", s));
  TEST_ASSERT_EQUAL(0, s.holdS);
  TEST_ASSERT_EQUAL(-1, s.quietFrom);
  TEST_ASSERT_EQUAL_STRING("", camSensorSettingsText(s).c_str());
  const char* bad[] = {"hold=0\n", "hold=3601\n", "hold=-5\n", "hold=1x\n", "quiet=60-60\n", "quiet=1440-10\n", "quiet=10\n", "colour=red\n"};
  for (const char* text : bad) TEST_ASSERT_FALSE_MESSAGE(camParseSensorSettings(text, s), text);
}

static void node_settings_round_trip_and_refuse_bad_values() {
  CamNodeSettings s;
  TEST_ASSERT_TRUE(camParseNodeSettings("late=300\r\n", s));
  TEST_ASSERT_EQUAL(300, s.lateS);
  TEST_ASSERT_EQUAL_STRING("late=300\n", camNodeSettingsText(s).c_str());
  TEST_ASSERT_TRUE(camParseNodeSettings("late=86400\n", s));
  TEST_ASSERT_TRUE(camParseNodeSettings("", s));
  TEST_ASSERT_EQUAL(0, s.lateS);
  TEST_ASSERT_EQUAL_STRING("", camNodeSettingsText(s).c_str());
  TEST_ASSERT_TRUE(camParseNodeSettings("silent=600\nlate=45\n", s));
  TEST_ASSERT_EQUAL(600, s.silentS);
  TEST_ASSERT_EQUAL_STRING("late=45\nsilent=600\n", camNodeSettingsText(s).c_str());
  const char* bad[] = {"late=0\n", "late=86401\n", "late=\n", "late=-5\n", "late=1x\n", "late=99999999999\n", "hold=5\n", "silent=59\n", "silent=604801\n"};
  for (const char* text : bad) TEST_ASSERT_FALSE_MESSAGE(camParseNodeSettings(text, s), text);
}

static void quiet_hours_cross_midnight() {
  CamSensorSettings night, day, none;
  night.quietFrom = 22 * 60, night.quietTo = 7 * 60;
  day.quietFrom = 9 * 60, day.quietTo = 17 * 60;
  TEST_ASSERT_TRUE(camQuiet(night, 23 * 60));
  TEST_ASSERT_TRUE(camQuiet(night, 0));
  TEST_ASSERT_TRUE(camQuiet(night, 6 * 60 + 59));
  TEST_ASSERT_FALSE(camQuiet(night, 7 * 60));
  TEST_ASSERT_FALSE(camQuiet(night, 12 * 60));
  TEST_ASSERT_TRUE(camQuiet(day, 9 * 60));
  TEST_ASSERT_FALSE(camQuiet(day, 17 * 60));
  TEST_ASSERT_FALSE(camQuiet(night, -1));  // clock not set: alerts still go through
  TEST_ASSERT_FALSE(camQuiet(none, 0));
}

static void a_sensor_can_have_its_own_hold() {
  SensorBoard b(2, 10000);
  b.trigger(1, 0, 0, false, 30000);
  b.trigger(2, 0);
  b.tick(20000);
  TEST_ASSERT_TRUE(b.active(1));
  TEST_ASSERT_FALSE(b.active(2));
}

static void long_names_are_cut_between_characters() {
  char out[6];
  camCopyText(out, sizeof(out), "abc");
  TEST_ASSERT_EQUAL_STRING("abc", out);
  camCopyText(out, sizeof(out), "abcde");
  TEST_ASSERT_EQUAL_STRING("abcde", out);
  camCopyText(out, sizeof(out), "abcdef");
  TEST_ASSERT_EQUAL_STRING("abcde", out);
  camCopyText(out, sizeof(out), "abcd\xC3\xA3");  // "abcdã" needs 6 bytes, so the whole letter goes
  TEST_ASSERT_EQUAL_STRING("abcd", out);
  camCopyText(out, sizeof(out), "abc\xC3\xA3x");  // "abcãx": the letter fits, the x does not
  TEST_ASSERT_EQUAL_STRING("abc\xC3\xA3", out);
  camCopyText(out, sizeof(out), "ab\xE2\x82\xAC\xE2\x82\xAC");  // "ab€€": one 3-byte euro sign fits
  TEST_ASSERT_EQUAL_STRING("ab\xE2\x82\xAC", out);
}

static void battery_outside_0_to_100_is_unknown() {
  uint8_t p[] = {1, 2, 1, 100, 4, 3, 2, 1, 44, 1, 0, 0, 0};
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(100, r.battery);
  p[3] = 101;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, r.battery);
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, camBatteryPercent(300));
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, camBatteryPercent(-1));
  TEST_ASSERT_EQUAL(0, camBatteryPercent(0));
}

static void parses_report_with_no_targets() {
  // version, input, active, battery, counter 0x01020304, age 300, no targets
  const uint8_t p[] = {1, 2, 1, 80, 4, 3, 2, 1, 44, 1, 0, 0, 0};
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(2, r.input);
  TEST_ASSERT_TRUE(r.active);
  TEST_ASSERT_EQUAL(80, r.battery);
  TEST_ASSERT_EQUAL_UINT32(0x01020304, r.counter);
  TEST_ASSERT_EQUAL_UINT32(300, r.age);
  TEST_ASSERT_EQUAL(0, r.targetCount);
}

static void parses_three_targets_and_picks_nearest() {
  const uint8_t p[] = {1, 1, 1, 0xFF, 1, 0, 0, 0, 0, 0, 0, 0, 3,
                       0x90, 0x01, 1,    // 400 cm, approaching
                       0x96, 0x00, 2,    // 150 cm, moving away
                       0xFF, 0xFF, 3};   // unknown distance
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(3, r.targetCount);
  TEST_ASSERT_EQUAL_UINT16(400, r.targets[0].distanceCm);
  TEST_ASSERT_EQUAL_UINT16(150, r.targets[1].distanceCm);
  TEST_ASSERT_EQUAL_UINT16(CAM_DISTANCE_UNKNOWN, r.targets[2].distanceCm);
  TEST_ASSERT_EQUAL(2, reportDirection(r));
}

static void rejects_malformed_reports() {
  const uint8_t good[] = {1, 1, 1, 80, 1, 0, 0, 0, 0, 0, 0, 0, 1, 10, 0, 1};
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(good, sizeof(good), r));
  uint8_t bad[sizeof(good)];
  struct { int index; uint8_t value; } changes[] = {{0, 2}, {1, 0}, {2, 2}, {12, 4}, {12, 2}, {15, 5}};
  for (auto& c : changes) {
    memcpy(bad, good, sizeof(good));
    bad[c.index] = c.value;
    TEST_ASSERT_FALSE(parseSensorPacket(bad, sizeof(bad), r));
  }
  TEST_ASSERT_FALSE(parseSensorPacket(good, sizeof(good) - 1, r));
  TEST_ASSERT_FALSE(parseSensorPacket(good, 5, r));
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

static void other_clients_cannot_push_out_the_nvr() {
  SubTable t;
  t.protect(9);
  uint32_t nvr = t.addPush("http://9/a", 9, 0).id;  // the oldest of all
  for (int i = 0; i < 50; i++) t.addPull(5, 100 + i);  // another client subscribing again and again
  TEST_ASSERT_NOT_NULL(t.find(nvr));
  TEST_ASSERT_EQUAL(SubTable::SIZE, t.count());
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

// ---- RTP ----

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

static void base64_matches_known_values() {
  TEST_ASSERT_EQUAL_STRING("TWE=", base64((const uint8_t*)"Ma", 2).c_str());
  TEST_ASSERT_EQUAL_STRING("TWFu", base64((const uint8_t*)"Man", 3).c_str());
  TEST_ASSERT_EQUAL_STRING("TQ==", base64((const uint8_t*)"M", 1).c_str());
}

static void json_writer_places_commas_and_brackets() {
  std::string out;
  CamJson j(out);
  j.object().num("a", 1).array("b").num(2).str("x").null().flag(true).end().object("c").end().array("d").end().num("e", false, 5).num("f", true, -7);
  j.str("g", false, "no").str("h", true, "yes").finish();
  TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":[2,\"x\",null,true],\"c\":{},\"d\":[],\"e\":null,\"f\":-7,\"g\":null,\"h\":\"yes\"}\n", out.c_str());
  out.clear();
  CamJson(out).array().object().str("k", "v").finish();  // closes what is left open
  TEST_ASSERT_EQUAL_STRING("[{\"k\":\"v\"}]\n", out.c_str());
}

static void json_writer_escapes_any_text() {
  std::string out;
  CamJson(out).object().str("say \"hi\"", "a\"b\\c\nd\re\tf\x01g\x1f h\xc3\xa9").finish();
  TEST_ASSERT_EQUAL_STRING("{\"say \\\"hi\\\"\":\"a\\\"b\\\\c\\nd\\re\\tf\\u0001g\\u001f h\xc3\xa9\"}\n", out.c_str());
  TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"can't \\\"x\\\"\"}\n", camJsonError("can't \"x\"").c_str());
}

static void past_events_share_records_only_as_they_would_live() {
  CamDelayedQueue q;
  uint8_t a[6] = {2, 0, 0, 0, 0, 1}, b[6] = {2, 0, 0, 0, 0, 2};
  q.add(a, 1, 1, true, 100000, 8000);
  q.add(a, 1, 2, true, 105000, 8000);  // within the hold: one record, with the latest direction
  q.add(a, 1, 0, true, 200000, 8000);  // later than the hold: its own record
  q.add(b, 1, 0, true, 100000, 8000);  // another sensor: its own record
  TEST_ASSERT_EQUAL(3, q.count());
  CamDelayed d;
  TEST_ASSERT_TRUE(q.take(d));
  TEST_ASSERT_EQUAL(2, d.direction);
  TEST_ASSERT_EQUAL_UINT32(105000, d.happenedAt);
}

static void a_full_past_event_queue_still_records_every_sensor() {
  CamDelayedQueue q;
  uint8_t a[6] = {2, 0, 0, 0, 0, 1}, b[6] = {2, 0, 0, 0, 0, 2}, c[6] = {2, 0, 0, 0, 0, 3};
  for (int i = 0; i < CamDelayedQueue::SIZE; i++) q.add(a, 1, 1, true, 100000u * (i + 1), 8000);
  q.add(a, 1, 3, false, 0, 8000);  // the same sensor joins one of its own records
  q.add(b, 1, 1, true, 50000, 8000);  // another sensor joins the newest record, which then stands for several
  q.add(c, 1, 2, true, 60000, 8000);  // and stays standing for several
  TEST_ASSERT_EQUAL(CamDelayedQueue::SIZE, q.count());
  CamDelayed d;
  for (int i = 0; i < CamDelayedQueue::SIZE - 1; i++) {
    TEST_ASSERT_TRUE(q.take(d));
    TEST_ASSERT_FALSE(d.several);
  }
  TEST_ASSERT_TRUE(q.take(d));
  TEST_ASSERT_TRUE(d.several);
  TEST_ASSERT_FALSE(q.take(d));
}

static void confirmation_is_signed_for_that_sensor() {
  uint8_t mac[6] = {2, 0, 0, 0, 0, 1}, other[6] = {2, 0, 0, 0, 0, 2};
  SensorReport r;
  r.input = 2;
  r.active = true;
  r.counter = 0x01020304;
  std::string c = camConfirmation(mac, r, "key");
  TEST_ASSERT_EQUAL(CAM_CONFIRMATION_LEN, (int)c.size());
  TEST_ASSERT_EQUAL_MEMORY("\x03\x02\x01\x04\x03\x02\x01", c.data(), 7);
  TEST_ASSERT_TRUE(c.substr(7) == camPacketSignature(mac, (const uint8_t*)c.data(), 7, "key"));
  TEST_ASSERT_FALSE(c.substr(7) == camPacketSignature(other, (const uint8_t*)c.data(), 7, "key"));
}

static void durations_read_as_people_say_them() {
  TEST_ASSERT_EQUAL_STRING("1 min", camDurationText(1).c_str());
  TEST_ASSERT_EQUAL_STRING("59 min", camDurationText(59 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("2 h 13 min", camDurationText(2 * 3600 + 13 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("3 days 4 h", camDurationText(3 * 86400 + 4 * 3600).c_str());
}

static void xml_text_is_unescaped_and_secrets_compared_whole() {
  TEST_ASSERT_EQUAL_STRING("p&ss<w>\"'A", xmlFirstText("<a:Password> p&amp;ss&lt;w&gt;&quot;&apos;&#65;</a:Password>", "Password").c_str());
  TEST_ASSERT_EQUAL_STRING("a&b;c&zz;", xmlFirstText("<x>a&b;c&zz;</x>", "x").c_str());  // not entities, so kept
  TEST_ASSERT_TRUE(camSameText("secret", "secret"));
  TEST_ASSERT_FALSE(camSameText("secret", "secreT"));
  TEST_ASSERT_FALSE(camSameText("secret", "secrets"));
  TEST_ASSERT_FALSE(camSameText("", "x"));
  TEST_ASSERT_FALSE(camSnapshotToken("one", "pw") == camSnapshotToken("two", "pw"));  // depends on the hub's secret
}

static void wrong_passwords_lock_out_quick_and_slow_guessing() {
  CamLoginGuard g;
  uint32_t t = 1000;
  for (int i = 0; i < 9; i++) g.note(7, false, t);
  TEST_ASSERT_TRUE(g.allowed(7, t));  // nine wrong: still allowed
  g.note(7, false, t);
  TEST_ASSERT_FALSE(g.allowed(7, t + 59000));  // the tenth locks it for a minute
  TEST_ASSERT_TRUE(g.allowed(8, t));          // only that address
  TEST_ASSERT_TRUE(g.allowed(7, t + 60000));
  g.note(7, false, t + 60000);  // waiting out the lock does not start the count again: the next lock is twice as long
  TEST_ASSERT_FALSE(g.allowed(7, t + 60000 + 119000));
  TEST_ASSERT_TRUE(g.allowed(7, t + 60000 + 120000));
  uint32_t at = t + 180000;
  for (int i = 0; i < 20; i++) {  // guessing slowly, once each lock runs out, reaches the longest lock
    g.note(7, false, at);
    at += CamLoginGuard::LONGEST_LOCK_MS;
  }
  TEST_ASSERT_FALSE(g.allowed(7, at - CamLoginGuard::LONGEST_LOCK_MS + CamLoginGuard::LONGEST_LOCK_MS - 1));
  g.note(7, true, at);  // a right password clears it
  TEST_ASSERT_TRUE(g.allowed(7, at));
  for (int i = 0; i < 9; i++) g.note(9, false, 0);
  g.note(9, false, CamLoginGuard::FORGET_MS);  // a day later, the old ones are forgotten
  TEST_ASSERT_TRUE(g.allowed(9, CamLoginGuard::FORGET_MS));
}

// ---- the event log ----

static std::string tempFolder(const char* name) {
  std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : "/tmp") + "/" + name;
  CamStorage(path).removeTree("");
  CamStorage(path).makeDirs("");
  return path;
}

static CamEvent sampleEvent(int n) {
  CamEvent e;
  e.received = 1800000000 + n;
  e.age = (uint32_t)n;
  snprintf(e.sensor, sizeof(e.sensor), "AA-BB-CC-DD-EE-%02d/1", n % 100);
  snprintf(e.text, sizeof(e.text), "Gate, \"%d\"", n);
  e.kind = CAM_EVENT_REPORT;
  e.active = n % 2;
  e.delayed = n % 3 == 0;
  e.battery = (uint8_t)(n % 101);
  e.targetCount = 2;
  e.targets[0].distanceCm = 250;
  e.targets[0].direction = 1;
  e.targets[1].distanceCm = 120;
  e.targets[1].direction = 3;
  return e;
}

static void an_event_record_reads_back_and_damage_is_found() {
  CamEvent e = sampleEvent(42), back;
  e.offS = 3600;
  uint8_t r[CAM_EVENT_RECORD];
  camEventEncode(e, r);
  TEST_ASSERT_TRUE(camEventDecode(r, back));
  TEST_ASSERT_TRUE(camEventCsvLine(back) == camEventCsvLine(e));
  TEST_ASSERT_EQUAL_UINT32(3600, back.offS);
  TEST_ASSERT_EQUAL_STRING(e.text, back.text);
  r[40] ^= 1;
  TEST_ASSERT_FALSE(camEventDecode(r, back));
  TEST_ASSERT_EQUAL_STRING("2027-01-15T08:00:42Z,2027-01-15T08:00:00Z,AA-BB-CC-DD-EE-42/1,\"Gate, \"\"42\"\"\",clear,42,1.20,left to right\n",
                           camEventCsvLine(e).c_str());
}

// Reads the download the way the web server does: in pieces, some sent only in part and asked for again.
static std::string download(CamEventLog& log) {
  log.csvBegin();
  size_t at = 0;
  std::string out;
  char piece[700];
  for (;;) {
    size_t n = log.csvRead(at, piece, sizeof(piece));
    if (!n) break;
    size_t sent = n > 100 && (at / 700) % 2 ? n / 3 : n;  // every other piece only partly sent
    out.append(piece, sent);
    at += sent;
  }
  char extra;
  return log.csvRead(at, &extra, 1) ? "read past the end" : out;
}

static void the_event_log_survives_restarts_and_downloads_whole() {
  std::string folder = tempFolder("cam_event_log_test");
  CamStorage storage(folder);
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
  {
    CamEventLog log(storage);
    log.load();
    for (int i = 0; i < 100; i++) {
      log.add(sampleEvent(i));
      expect += camEventCsvLine(sampleEvent(i));
      if (i % 7 == 0) TEST_ASSERT_TRUE(log.save(false));
    }
    TEST_ASSERT_TRUE(log.save(false));
    TEST_ASSERT_TRUE(download(log) == expect);
  }
  FILE* f = fopen((folder + "/events.log").c_str(), "ab");  // a power cut part way through a record
  fwrite("\x01\x02\x03", 1, 3, f);
  fclose(f);
  CamEventLog again(storage);
  again.load();
  TEST_ASSERT_EQUAL(CamEventLog::RECENT, again.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(99).text, again.recent(0).text);
  TEST_ASSERT_EQUAL_STRING(sampleEvent(36).text, again.recent(63).text);
  again.add(sampleEvent(100));  // written over the cut-off record
  TEST_ASSERT_TRUE(again.save(false));
  TEST_ASSERT_TRUE(download(again) == expect + camEventCsvLine(sampleEvent(100)));
  TEST_ASSERT_EQUAL(101 * CAM_EVENT_RECORD, (int)again.bytesOnStorage());
}

static void the_event_log_starts_a_new_file_but_not_during_a_download() {
  std::string folder = tempFolder("cam_event_log_rotate");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.load();
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
  size_t perFile = CamEventLog::FILE_MAX / CAM_EVENT_RECORD;
  for (size_t i = 0; i < perFile + 10; i++) {
    log.add(sampleEvent((int)i));
    expect += camEventCsvLine(sampleEvent((int)i));
    TEST_ASSERT_TRUE(log.save(i == perFile + 5));  // one save during a download keeps adding to the same file
  }
  TEST_ASSERT_TRUE(storage.exists("events-old.log"));
  TEST_ASSERT_TRUE(download(log) == expect);
  CamEventLog again(storage);
  again.load();
  TEST_ASSERT_TRUE(download(again) == expect);
}

static void the_older_firmwares_history_is_kept() {
  std::string folder = tempFolder("cam_event_log_legacy");
  CamStorage storage(folder);
  // Its newest events as a copy of memory, with the count and next position: three events, the newest at index 2.
  std::string blob(16 + 64 * sizeof(CamEvent), '\0');
  uint32_t header[4] = {0x31564543, (uint32_t)sizeof(CamEvent), 3, 3};
  memcpy(&blob[0], header, sizeof(header));
  for (int i = 0; i < 3; i++) {
    CamEvent e = sampleEvent(i);
    memcpy(&blob[16 + i * sizeof(CamEvent)], &e, sizeof(e));
  }
  storage.writeFile("events.bin", blob.data(), blob.size());
  storage.writeFile("events.csv", "old line\n", 9);
  CamEventLog log(storage);
  log.load();
  TEST_ASSERT_EQUAL(3, log.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(2).text, log.recent(0).text);
  TEST_ASSERT_FALSE(storage.exists("events.bin"));
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\nold line\n";
  for (int i = 0; i < 3; i++) expect += camEventCsvLine(sampleEvent(i));
  TEST_ASSERT_TRUE(download(log) == expect);  // the old CSV file first, unchanged

  // A failed update is undone, and the older firmware starts a new file: its events go after the log's.
  header[2] = header[3] = 2;
  for (int i = 0; i < 2; i++) {
    CamEvent e = sampleEvent(10 + i);
    memcpy(&blob[16 + i * sizeof(CamEvent)], &e, sizeof(e));
  }
  memcpy(&blob[0], header, sizeof(header));
  storage.writeFile("events.bin", blob.data(), blob.size());
  CamEventLog later(storage);
  later.load();
  TEST_ASSERT_EQUAL(5, later.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(11).text, later.recent(0).text);
  TEST_ASSERT_FALSE(storage.exists("events.bin"));
  for (int i = 0; i < 2; i++) expect += camEventCsvLine(sampleEvent(10 + i));
  TEST_ASSERT_TRUE(download(later) == expect);
}

static void only_the_hubs_own_names_are_served() {
  const char* ip = "192.168.1.50";
  TEST_ASSERT_TRUE(camKnownHost("", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("192.168.1.50:8080", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("192.168.4.1", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("ESP-AlarmHub.Local.", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camKnownHost("esp-alarmhub.home.arpa", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_FALSE(camKnownHost("esp-alarmhub.fritz.box", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camKnownHost("esp-alarmhub.fritz.box:80", ip, "esp-alarmhub.local", "", " Esp-AlarmHub.Fritz.Box , other"));
  TEST_ASSERT_FALSE(camKnownHost("esp-alarmhub.attacker.example", ip, "esp-alarmhub.local", "", "other"));
  TEST_ASSERT_FALSE(camKnownHost("192.168.1.5", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camHostListValid(""));
  TEST_ASSERT_TRUE(camHostListValid("a.b, c-d,e,f"));
  TEST_ASSERT_FALSE(camHostListValid("a,b,c,d,e"));
  TEST_ASSERT_FALSE(camHostListValid("bad name"));
  TEST_ASSERT_FALSE(camHostListValid(".lead"));
  TEST_ASSERT_FALSE(camHostListValid("x/y"));
}

static void a_replay_stays_one_only_while_nothing_new_joins_it() {
  CamDelayedQueue q;
  uint8_t a[6] = {2, 0, 0, 0, 0, 1}, b[6] = {2, 0, 0, 0, 0, 2};
  q.add(a, 1, 0, true, 100000, 8000, false, true);  // a replay of one the NVR missed
  q.add(b, 1, 0, true, 100000, 8000, false, true);
  q.add(b, 1, 0, true, 103000, 8000);  // a new alert from that sensor joins the second
  CamDelayed d;
  TEST_ASSERT_TRUE(q.take(d));
  TEST_ASSERT_TRUE(d.replay);
  TEST_ASSERT_TRUE(q.take(d));
  TEST_ASSERT_FALSE(d.replay);  // so it is logged if the NVR misses it
}

static void delivery_is_checked_and_missed_alerts_kept_once() {
  CamDeliveryWatch w;
  uint8_t a[6] = {2, 0, 0, 0, 0, 1};
  bool on = true;
  auto active = [&](int) { return on; };
  std::vector<CamDeliveryWatch::Missed> missed;
  w.started(3, a, 1, false, true, false, 8000, 1000);
  w.check(1000 + CamDeliveryWatch::CHECK_AFTER_MS - 1, false, active, missed);
  TEST_ASSERT_EQUAL(0, (int)missed.size());  // too early, and still on
  on = false;
  w.check(1500, false, active, missed);  // it ended: checked at once
  TEST_ASSERT_EQUAL(1, (int)missed.size());
  TEST_ASSERT_TRUE(missed[0].log);
  TEST_ASSERT_EQUAL(3, missed[0].id);
  CamDelayed d;
  TEST_ASSERT_TRUE(w.takeMissed(d));
  TEST_ASSERT_TRUE(d.several);  // kept from the start, although the alert had ended
  TEST_ASSERT_TRUE(d.replay);
  missed.clear();
  w.started(3, a, 1, true, false, true, 8000, 2000);  // its replay, missed too
  w.check(9000, false, active, missed);
  TEST_ASSERT_EQUAL(1, (int)missed.size());
  TEST_ASSERT_FALSE(missed[0].log);  // not logged a second time
  missed.clear();
  w.started(4, a, 2, false, false, false, 8000, 3000);
  w.check(9000, true, active, missed);  // the NVR was listening
  TEST_ASSERT_EQUAL(0, (int)missed.size());
}

static void each_notification_has_one_wording() {
  TEST_ASSERT_EQUAL_STRING("Gate (X/1): alert", camEventMessage(CAM_EVENT_REPORT, "Gate (X/1)", "", CAM_AGE_UNKNOWN, false).c_str());
  TEST_ASSERT_EQUAL_STRING("Gate (X/1): delayed alert not delivered, because the NVR is not listening",
                           camEventMessage(CAM_EVENT_NOT_DELIVERED, "Gate (X/1)", "", CAM_AGE_UNKNOWN, true).c_str());
  TEST_ASSERT_EQUAL_STRING("hub.local started (Power on), after being off for up to 2 h 0 min",
                           camEventMessage(CAM_EVENT_HUB_STARTED, "hub.local", "Power on", 7200, false).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(delivery_is_checked_and_missed_alerts_kept_once);
  RUN_TEST(each_notification_has_one_wording);
  RUN_TEST(a_replay_stays_one_only_while_nothing_new_joins_it);
  RUN_TEST(only_the_hubs_own_names_are_served);
  RUN_TEST(an_event_record_reads_back_and_damage_is_found);
  RUN_TEST(the_event_log_survives_restarts_and_downloads_whole);
  RUN_TEST(the_event_log_starts_a_new_file_but_not_during_a_download);
  RUN_TEST(the_older_firmwares_history_is_kept);
  RUN_TEST(wrong_passwords_lock_out_quick_and_slow_guessing);
  RUN_TEST(xml_text_is_unescaped_and_secrets_compared_whole);
  RUN_TEST(past_events_share_records_only_as_they_would_live);
  RUN_TEST(a_full_past_event_queue_still_records_every_sensor);
  RUN_TEST(confirmation_is_signed_for_that_sensor);
  RUN_TEST(durations_read_as_people_say_them);
  RUN_TEST(json_writer_places_commas_and_brackets);
  RUN_TEST(json_writer_escapes_any_text);
  RUN_TEST(hold_expires_and_retrigger_extends);
  RUN_TEST(hold_survives_millis_wrap);
  RUN_TEST(unknown_sensors_rejected);
  RUN_TEST(frame_cycles_between_active_sensors);
  RUN_TEST(shown_sensor_carries_direction_and_delay);
  RUN_TEST(classifies_storage_paths);
  RUN_TEST(parses_and_prints_macs);
  RUN_TEST(names_state_files);
  RUN_TEST(splits_picture_files);
  RUN_TEST(accepts_sps_differing_only_in_vui);
  RUN_TEST(builtin_pictures_are_valid);
  RUN_TEST(parses_report_with_no_targets);
  RUN_TEST(parses_three_targets_and_picks_nearest);
  RUN_TEST(rejects_malformed_reports);
  RUN_TEST(motion_repeats_true_faster_than_nvr_latch);
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
  RUN_TEST(base64_matches_known_values);
  RUN_TEST(battery_outside_0_to_100_is_unknown);
  RUN_TEST(backup_values_follow_the_page_rules);
  RUN_TEST(long_names_are_cut_between_characters);
  RUN_TEST(hmac_sha1_matches_rfc_2202);
  RUN_TEST(sha1_in_pieces_matches);
  RUN_TEST(signed_messages_are_checked);
  RUN_TEST(mqtt_packets_match_the_standard);
  RUN_TEST(x25519_matches_rfc_7748);
  RUN_TEST(a_timed_disarm_ends_safely_after_a_restart);
  RUN_TEST(every_event_kind_has_its_own_name);
  RUN_TEST(sensor_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(node_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(quiet_hours_cross_midnight);
  RUN_TEST(a_sensor_can_have_its_own_hold);
  RUN_TEST(rtp_time_follows_the_clock_when_frames_are_skipped);
  return UNITY_END();
}
