// CamAlarm on its own: a folder of sensors, a clock the test moves, and stand-ins for the rest of the hub. Each test
// follows what the alarm decides, without HTTP or sockets in between.
#include "tests.h"
#include "cam_alarm.h"

namespace {

// The rest of the hub, as the alarm sees it: whether the NVR is listening, and what was sent to the webhook.
struct Outside : CamAlarmOutside {
  bool listening = true;
  std::vector<CamEventKind> hooks;
  bool nvrListening(uint32_t) const override { return listening; }
  bool downloadingHistory() const override { return false; }
  void notifyHook(int, CamEventKind kind, bool) override { hooks.push_back(kind); }
  void notifyHubHook(CamEventKind, const std::string&, const std::string&) override {}
  void sensorReset(int) override {}
  void sensorsRescanned(bool) override {}
  int sent(CamEventKind kind) const {
    int n = 0;
    for (CamEventKind k : hooks) n += k == kind;
    return n;
  }
};

struct Store : CamStore {
  bool armedSave(CamArmMode, int64_t) override { return true; }
};

const uint8_t DOOR[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
const uint8_t STRANGER[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x99};

std::string emptyFolder(const char* name) {
  std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : "/tmp") + "/" + name;
  CamStorage(path).removeTree("");
  CamStorage(path).makeDirs("");
  return path;
}

// An alarm with its own folder, made for the test and emptied first. Sensors are added before start(), as folders.
struct Rig {
  uint32_t now = 1000;
  CamConfig cfg;
  CamChip chip;
  CamRadio radio;
  Store store;
  Outside outside;
  CamStorage storage;
  CamNodeImages images{storage};
  CamAlarm alarm;
  explicit Rig(const char* folder) : storage(emptyFolder(folder)), alarm(cfg, chip, radio, store, storage, images, outside, [this] { return now; }) {}
  // A rig on the folder another left, as after a restart; the clock starts again, as millis() does.
  Rig(const Rig& before, int) : storage(before.storage), alarm(cfg, chip, radio, store, storage, images, outside, [this] { return now; }) {}

  void sensor(const char* path, const char* settings = nullptr) {
    storage.makeDirs(path);
    if (settings) storage.writeFile(std::string(path) + "/settings.txt", settings, strlen(settings));
  }
  void start() { alarm.begin(); }
  int report(const uint8_t mac[6], uint8_t input, bool active, uint32_t ageS = 0) {
    SensorReport r;
    r.active = active;
    r.age = ageS;
    return alarm.webReport(mac, input, r);
  }
  void run(uint32_t ms) {  // the loop's passes, every 10 ms
    for (uint32_t end = now + ms; (int32_t)(now - end) < 0;) {
      now += 10;
      alarm.tick(now);
    }
  }
  int id(uint8_t input = 1) const { return alarm.findSlot(DOOR, input); }
  bool on(uint8_t input = 1) const { return alarm.board().active(id(input)); }
  bool past(uint8_t input = 1) const { return alarm.board().pastEvent(id(input)); }
  int logged(CamEventKind kind) const {
    int n = 0;
    for (int i = 0; i < alarm.log().count(); i++) n += alarm.log().recent(i).kind == kind;
    return n;
  }
};

}  // namespace

static void an_alert_from_a_known_sensor_is_raised_logged_and_sent() {
  Rig r("cam_alarm_raised");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  TEST_ASSERT_EQUAL(200, r.report(DOOR, 1, true));
  TEST_ASSERT_TRUE(r.on());
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_REPORT));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_REPORT));
  TEST_ASSERT_EQUAL(404, r.report(STRANGER, 1, true));  // no folder, so not a sensor
  TEST_ASSERT_EQUAL(404, r.report(DOOR, 2, true));      // nor another input of a known node
}

static void an_alert_lasts_its_hold_even_when_the_sensor_clears_at_once() {
  Rig r("cam_alarm_hold");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.report(DOOR, 1, true);
  r.report(DOOR, 1, false);
  r.run(9000);
  TEST_ASSERT_TRUE(r.on());
  r.run(2000);
  TEST_ASSERT_FALSE(r.on());
  TEST_ASSERT_EQUAL(2, r.logged(CAM_EVENT_REPORT));  // the activation and the clear
}

static void disarmed_an_alert_is_logged_but_not_raised() {
  Rig r("cam_alarm_disarmed");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  TEST_ASSERT_TRUE(r.alarm.setArmMode(CAM_DISARMED, "the test"));
  r.run(100);
  r.report(DOOR, 1, true);
  TEST_ASSERT_FALSE(r.on());
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_DISARMED_ALERT));
  TEST_ASSERT_EQUAL(0, r.outside.sent(CAM_EVENT_REPORT));
}

static void armed_home_holds_back_interior_sensors_only() {
  Rig r("cam_alarm_home");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.sensor("AA-BB-CC-DD-EE-01/2", "zone=interior\n");
  r.start();
  TEST_ASSERT_TRUE(r.alarm.setArmMode(CAM_ARMED_HOME, "the test"));
  r.run(100);
  r.report(DOOR, 2, true);
  TEST_ASSERT_FALSE(r.on(2));
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_HOME_ALERT));
  r.report(DOOR, 1, true);
  TEST_ASSERT_TRUE(r.on(1));
}

static void a_report_of_something_before_the_disarm_still_counts() {
  Rig r("cam_alarm_before_disarm");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.run(60000);
  r.alarm.setArmMode(CAM_DISARMED, "the test");
  r.run(1000);
  r.report(DOOR, 1, true, 20);  // happened 20 s ago, while still armed
  TEST_ASSERT_TRUE(r.on());
  TEST_ASSERT_EQUAL(0, r.logged(CAM_EVENT_DISARMED_ALERT));
}

static void a_timed_disarm_arms_away_again_by_itself() {
  Rig r("cam_alarm_timed");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.alarm.setMinuteMs(10);
  TEST_ASSERT_TRUE(r.alarm.setArmMode(CAM_DISARMED, "the test", 5));  // 50 ms on this clock
  r.run(30);
  TEST_ASSERT_FALSE(r.alarm.armed());
  TEST_ASSERT_TRUE(r.alarm.disarmSecondsLeft(r.now) >= 0);
  r.run(30);
  TEST_ASSERT_EQUAL(CAM_ARMED_AWAY, r.alarm.armMode());
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_ARMED));
}

static void an_alert_the_nvr_missed_is_logged_and_replayed_once_it_listens() {
  Rig r("cam_alarm_missed");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.outside.listening = false;
  r.report(DOOR, 1, true);
  r.run(CAM_DELIVERY_CHECK_MS + 1000);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_NOT_DELIVERED));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_NOT_DELIVERED));
  r.run(10000);  // the live alert is over
  TEST_ASSERT_FALSE(r.on());
  r.outside.listening = true;
  r.run(100);
  TEST_ASSERT_TRUE(r.past());  // played again, as a past event
}

static void a_late_report_plays_as_a_past_event_of_its_own() {
  Rig r("cam_alarm_late");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.report(DOOR, 1, true, 120);  // two minutes old: past the 30 s after which a report is a past event
  TEST_ASSERT_TRUE(r.alarm.log().recent(0).delayed);
  r.run(100);
  TEST_ASSERT_TRUE(r.past());
  TEST_ASSERT_EQUAL(0, r.alarm.delayedCount());
}

static void a_live_alert_pauses_a_past_event_which_plays_in_full_afterwards() {
  Rig r("cam_alarm_interrupt");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.sensor("AA-BB-CC-DD-EE-01/2");
  r.start();
  r.report(DOOR, 1, true, 120);
  r.run(100);
  TEST_ASSERT_TRUE(r.past(1));
  r.report(DOOR, 2, true);  // live
  TEST_ASSERT_FALSE(r.on(1));
  TEST_ASSERT_TRUE(r.on(2));
  TEST_ASSERT_EQUAL(1, r.alarm.delayedCount());  // back in the queue
  r.run(10000 + CAM_PAST_GAP_MS + 1000);
  TEST_ASSERT_TRUE(r.past(1));
}

static void a_sensor_never_heard_is_reported_silent_once() {
  Rig r("cam_alarm_silent");
  r.cfg.silentMs = 1000;
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.run(900);
  TEST_ASSERT_EQUAL(0, r.logged(CAM_EVENT_SILENT));
  r.run(200);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_SILENT));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_SILENT));
  TEST_ASSERT_TRUE(r.alarm.healthOf(r.id()).silent);
  TEST_ASSERT_FALSE(r.on());  // and raises no alarm unless asked to
  r.run(5000);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_SILENT));
  r.report(DOOR, 1, false);  // heard again
  TEST_ASSERT_FALSE(r.alarm.healthOf(r.id()).silent);
}

static void a_silent_sensor_can_raise_the_alarm_when_asked_to() {
  Rig r("cam_alarm_silent_alarm");
  r.cfg.silentMs = 1000;
  r.cfg.healthAlarm = true;
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.run(1100);
  TEST_ASSERT_TRUE(r.on());
}

static void a_sensor_firing_too_often_is_reported_once() {
  Rig r("cam_alarm_busy");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.alarm.setBusyAlerts(3);
  for (int i = 0; i < 5; i++) {
    r.report(DOOR, 1, true);
    r.report(DOOR, 1, false);
  }
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_BUSY_SENSOR));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_BUSY_SENSOR));
}

static void a_test_alert_takes_the_whole_path_even_disarmed() {
  Rig r("cam_alarm_test_alert");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.alarm.setArmMode(CAM_DISARMED, "the test");
  TEST_ASSERT_TRUE(r.alarm.testAlert(r.id()));
  TEST_ASSERT_TRUE(r.on());
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_TEST_ALERT));
  TEST_ASSERT_FALSE(r.alarm.testAlert(0));
  TEST_ASSERT_FALSE(r.alarm.testAlert(CAM_MAX_SENSORS + 1));
}

static void an_alert_waiting_for_the_nvr_is_replayed_after_a_restart() {
  Rig before("cam_alarm_restart");
  before.sensor("AA-BB-CC-DD-EE-01/1");
  before.start();
  before.outside.listening = false;
  before.report(DOOR, 1, true);  // saved with its pending check before it was confirmed
  Rig after(before, 0);
  after.start();
  after.run(100);
  TEST_ASSERT_TRUE(after.past());  // the NVR may never have got it, so it plays again
}

static void a_deleted_sensor_is_unknown_after_the_table_is_read_again() {
  Rig r("cam_alarm_deleted");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  TEST_ASSERT_EQUAL(200, r.report(DOOR, 1, true));
  r.storage.removeTree("AA-BB-CC-DD-EE-01");
  r.alarm.rescan();
  TEST_ASSERT_EQUAL(0, r.id());
  TEST_ASSERT_FALSE(r.alarm.board().anyActive());  // its alert goes with it
  TEST_ASSERT_EQUAL(404, r.report(DOOR, 1, true));
}

void runAlarmTests() {
  RUN_TEST(an_alert_from_a_known_sensor_is_raised_logged_and_sent);
  RUN_TEST(an_alert_lasts_its_hold_even_when_the_sensor_clears_at_once);
  RUN_TEST(disarmed_an_alert_is_logged_but_not_raised);
  RUN_TEST(armed_home_holds_back_interior_sensors_only);
  RUN_TEST(a_report_of_something_before_the_disarm_still_counts);
  RUN_TEST(a_timed_disarm_arms_away_again_by_itself);
  RUN_TEST(an_alert_the_nvr_missed_is_logged_and_replayed_once_it_listens);
  RUN_TEST(a_late_report_plays_as_a_past_event_of_its_own);
  RUN_TEST(a_live_alert_pauses_a_past_event_which_plays_in_full_afterwards);
  RUN_TEST(a_sensor_never_heard_is_reported_silent_once);
  RUN_TEST(a_silent_sensor_can_raise_the_alarm_when_asked_to);
  RUN_TEST(a_sensor_firing_too_often_is_reported_once);
  RUN_TEST(a_test_alert_takes_the_whole_path_even_disarmed);
  RUN_TEST(an_alert_waiting_for_the_nvr_is_replayed_after_a_restart);
  RUN_TEST(a_deleted_sensor_is_unknown_after_the_table_is_read_again);
}
