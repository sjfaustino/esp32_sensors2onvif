// Text for people: event names and notifications, durations, names cut to fit, and the JSON the pages read.
#include "tests.h"
#include "cam_number.h"
#include "cam_text.h"

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
      "test alert in the last month: send one from the hub's Overview, to see that the NVR still records",
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
  // Over a month since the last test alert, the daily check asks for one, so the reminder reaches the phone.
  d.testAgoS = 31LL * 86400;
  TEST_ASSERT_TRUE(camDailyMessage(d).find("last test alert 31 days 0 h ago, over a month: send one from the hub's Overview") != std::string::npos);
  d.testAgoS = 29LL * 86400;
  TEST_ASSERT_TRUE(camDailyMessage(d).find("over a month") == std::string::npos);
}

// Under Notifications the messages can be in Portuguese: whole sentences, each in its own word order, with the names,
// numbers and times in them, and the events' names too.
static void messages_can_be_in_portuguese() {
  camSetTextLanguage(CAM_LANG_PT);
  CamDailyCheck d;
  d.hub = "central";
  d.mode = "home";
  d.nodes = 3;
  d.heard = 2;
  d.problems = {std::string("Portão: ") + camText("silent")};
  d.alerts = 1;
  d.nvrListening = true;
  d.testAgoS = 3 * 86400;
  TEST_ASSERT_EQUAL_STRING(
      "central verificação diária: 2 de 3 nós ouvidos no último dia; problemas: Portão: em silêncio; 1 alerta no último dia, todos "
      "entregues ao NVR; o NVR está a ouvir; armada em casa; último alerta de teste há 3 dias 0 h",
      camDailyMessage(d).c_str());
  TEST_ASSERT_EQUAL_STRING("Portão (X/1): alerta", camEventMessage(CAM_EVENT_REPORT, "Portão (X/1)", "", CAM_AGE_UNKNOWN, false).c_str());
  TEST_ASSERT_EQUAL_STRING("central foi desarmada a partir de a página web, durante 1 h: os alertas ficam registados mas não são enviados ao NVR",
                           camEventMessage(CAM_EVENT_DISARMED, "central", "the web page, for 1 h", CAM_AGE_UNKNOWN, false).c_str());
  TEST_ASSERT_EQUAL_STRING("central arrancou (ligação à corrente ou botão de reinício), depois de desligada até 2 h 0 min",
                           camEventMessage(CAM_EVENT_HUB_STARTED, "central", "Power on or reset button", 7200, false).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "central: 3 nós deixaram de comunicar ao mesmo tempo. As baterias falham uma de cada vez, por isso algo pode estar a "
      "bloquear o rádio, como um bloqueador",
      camEventMessage(CAM_EVENT_JAMMING, "central", "3 nodes", CAM_AGE_UNKNOWN, false).c_str());
  TEST_ASSERT_EQUAL_STRING("5 dias 2 h", camDurationText(5 * 86400 + 2 * 3600).c_str());
  camSetTextLanguage(CAM_LANG_EN);
  TEST_ASSERT_EQUAL_STRING("Gate (X/1): alert", camEventMessage(CAM_EVENT_REPORT, "Gate (X/1)", "", CAM_AGE_UNKNOWN, false).c_str());
}

// Each phrase has the same {n} in both languages, and each event's name is in the table, so no message is half English.
static void each_message_phrase_has_its_portuguese() {
  for (size_t i = 0; i < camPhraseCount(); i++) {
    std::string en = camPhraseEnglish(i), pt = camPhrasePortuguese(i);
    for (char n = '0'; n <= '9'; n++) {
      std::string mark = std::string("{") + n + "}";
      TEST_ASSERT_EQUAL_MESSAGE(en.find(mark) != std::string::npos, pt.find(mark) != std::string::npos, en.c_str());
    }
  }
  camSetTextLanguage(CAM_LANG_PT);
  std::string missing;
  for (const char* name : camEventNames())
    if (strcmp(camText(name), name) == 0) missing += std::string(name) + "; ";
  camSetTextLanguage(CAM_LANG_EN);  // before the check, so a failure leaves the other tests in English
  TEST_ASSERT_EQUAL_STRING("", missing.c_str());
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
  RUN_TEST(messages_can_be_in_portuguese);
  RUN_TEST(each_message_phrase_has_its_portuguese);
  RUN_TEST(the_weekly_summary_says_what_matters);
  RUN_TEST(every_event_kind_has_its_own_name);
}
