// Text for people: event names and notifications, durations, names cut to fit, and the JSON the pages read.
#include "tests.h"
#include "cam_number.h"

// Numbers from outside are read whole and within range, never cut down to fit, so input 257 does not become input 1.
static void numbers_are_read_whole_and_within_range() {
  uint8_t input = 7;
  TEST_ASSERT_TRUE(camNumber(std::string("255"), input));
  TEST_ASSERT_EQUAL(255, input);
  for (const char* bad : {"256", "257", "", " 5", "5 ", "+5", "-1", "5a", "0x5", "1.5", "99999999999999999999"}) {
    input = 7;
    TEST_ASSERT_FALSE_MESSAGE(camNumber(std::string(bad), input), bad);
    TEST_ASSERT_EQUAL_MESSAGE(7, input, bad);  // left as it was
  }
  long long signedValue = 0;
  TEST_ASSERT_TRUE(camNumber(std::string("-12"), signedValue));
  TEST_ASSERT_EQUAL(-12, (int)signedValue);
  uint16_t port = 0;
  TEST_ASSERT_FALSE(camNumberIn(std::string("0"), (uint16_t)1, (uint16_t)65535, port));
  TEST_ASSERT_FALSE(camNumberIn(std::string("65536"), (uint16_t)1, (uint16_t)65535, port));
  TEST_ASSERT_TRUE(camNumberIn(std::string("8080"), (uint16_t)1, (uint16_t)65535, port));
  TEST_ASSERT_EQUAL(8080, port);
  long code = 0;
  TEST_ASSERT_TRUE(camNumber(std::string("4a"), code, 16));
  TEST_ASSERT_EQUAL(0x4a, (int)code);

  // Where outside text is read.
  HttpUrl url;
  TEST_ASSERT_TRUE(parseHttpUrl("http://host:8080/x", url));
  TEST_ASSERT_EQUAL(8080, url.port);
  TEST_ASSERT_FALSE(parseHttpUrl("http://host:8080x/", url));
  TEST_ASSERT_FALSE(parseHttpUrl("http://host:70000/", url));
  TEST_ASSERT_EQUAL_STRING("A&B&#65x;", xmlFirstText("<a>&#65;&amp;&#x42;&#65x;</a>", "a").c_str());
  CamNodeInfo info;
  TEST_ASSERT_TRUE(camParseNodeInfo("every=60\nrestarts=3", info));
  TEST_ASSERT_EQUAL(60, info.everyS);
  TEST_ASSERT_FALSE(camParseNodeInfo("every=60s", info));
  TEST_ASSERT_FALSE(camParseNodeInfo("restarts=65536", info));
  TEST_ASSERT_EQUAL(CAM_PATH_SENSOR, camClassifyPath("AA-BB-CC-DD-EE-01/255"));
  TEST_ASSERT_TRUE(camClassifyPath("AA-BB-CC-DD-EE-01/256") != CAM_PATH_SENSOR);
  TEST_ASSERT_TRUE(camClassifyPath("AA-BB-CC-DD-EE-01/01") != CAM_PATH_SENSOR);
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

static void the_daily_check_says_what_matters() {
  CamDailyCheck d;
  d.hub = "esp-alarmhub";
  d.mode = "away";
  d.nodes = 3;
  d.heard = 3;
  d.nvrListening = true;
  TEST_ASSERT_EQUAL_STRING(
      "esp-alarmhub daily check: 3 of 3 nodes heard in the last day; no problems; 0 alerts in the last day; the NVR is listening; armed away; no "
      "test alert recently",
      camDailyMessage(d).c_str());
  d.heard = 2;
  d.problems = {"Gate: silent", "Shed: low battery"};
  d.alerts = 1;
  d.notDelivered = 1;
  d.allCounted = false;
  d.nvrListening = false;
  d.mode = "disarmed";
  d.testAgoS = 3 * 86400;
  TEST_ASSERT_EQUAL_STRING(
      "esp-alarmhub daily check: 2 of 3 nodes heard in the last day; problems: Gate: silent, Shed: low battery; at least 1 alert in the last day, 1 "
      "not delivered to the NVR; the NVR is NOT listening; disarmed; last test alert 3 days 0 h ago",
      camDailyMessage(d).c_str());
  d.notDelivered = 0;
  d.alerts = 4;
  TEST_ASSERT_TRUE(camDailyMessage(d).find("at least 4 alerts in the last day, all delivered to the NVR") != std::string::npos);
}

static void the_weekly_summary_says_what_matters() {
  CamWeeklySummary w;
  w.hub = "esp-alarmhub";
  w.mode = "away";
  w.nvrListening = true;
  TEST_ASSERT_EQUAL_STRING(
      "esp-alarmhub weekly summary: 0 alerts in the last 7 days; 0 problems reported; no problems now; the NVR is listening; armed away; no test "
      "alert this week",
      camWeeklyMessage(w).c_str());
  w.alerts = 12;
  w.busiest = "Gate";
  w.busiestAlerts = 7;
  w.held = 2;
  w.problems = 1;
  w.batteries = {"Shed: about 12 days"};
  w.nowProblems = {"Shed: low battery"};
  w.testAgoS = 2 * 86400;
  TEST_ASSERT_EQUAL_STRING(
      "esp-alarmhub weekly summary: 12 alerts in the last 7 days, all delivered to the NVR; most from Gate (7); 2 more only logged, while "
      "disarmed, armed home or in quiet hours; 1 problem reported; batteries running low: Shed: about 12 days; problems now: Shed: low battery; "
      "the NVR is listening; armed away; last test alert 2 days 0 h ago",
      camWeeklyMessage(w).c_str());
  w.notDelivered = 3;
  w.allCounted = false;
  TEST_ASSERT_TRUE(camWeeklyMessage(w).find("at least 12 alerts in the last 7 days, 3 not delivered to the NVR") != std::string::npos);
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

static void durations_read_as_people_say_them() {
  TEST_ASSERT_EQUAL_STRING("1 min", camDurationText(1).c_str());
  TEST_ASSERT_EQUAL_STRING("59 min", camDurationText(59 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("2 h 13 min", camDurationText(2 * 3600 + 13 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("3 days 4 h", camDurationText(3 * 86400 + 4 * 3600).c_str());
}

static void each_notification_has_one_wording() {
  TEST_ASSERT_EQUAL_STRING("Gate (X/1): alert", camEventMessage(CAM_EVENT_REPORT, "Gate (X/1)", "", CAM_AGE_UNKNOWN, false).c_str());
  TEST_ASSERT_EQUAL_STRING("Gate (X/1): delayed alert not delivered, because the NVR is not listening",
                           camEventMessage(CAM_EVENT_NOT_DELIVERED, "Gate (X/1)", "", CAM_AGE_UNKNOWN, true).c_str());
  TEST_ASSERT_EQUAL_STRING("hub.local started (Power on), after being off for up to 2 h 0 min",
                           camEventMessage(CAM_EVENT_HUB_STARTED, "hub.local", "Power on", 7200, false).c_str());
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

void runTextTests() {
  RUN_TEST(each_notification_has_one_wording);
  RUN_TEST(durations_read_as_people_say_them);
  RUN_TEST(numbers_are_read_whole_and_within_range);
  RUN_TEST(json_writer_places_commas_and_brackets);
  RUN_TEST(json_writer_escapes_any_text);
  RUN_TEST(long_names_are_cut_between_characters);
  RUN_TEST(the_daily_check_says_what_matters);
  RUN_TEST(the_weekly_summary_says_what_matters);
  RUN_TEST(every_event_kind_has_its_own_name);
}
