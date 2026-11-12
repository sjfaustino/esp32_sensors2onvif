#include <unity.h>
#include <string.h>
#include <string>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_rtp.h"
#include "cam_server.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
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
  const char* bad[] = {"late=0\n", "late=86401\n", "late=\n", "late=-5\n", "late=1x\n", "late=99999999999\n", "hold=5\n"};
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

int main() {
  UNITY_BEGIN();
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
  RUN_TEST(signed_messages_are_checked);
  RUN_TEST(sensor_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(node_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(quiet_hours_cross_midnight);
  RUN_TEST(a_sensor_can_have_its_own_hold);
  RUN_TEST(rtp_time_follows_the_clock_when_frames_are_skipped);
  return UNITY_END();
}
