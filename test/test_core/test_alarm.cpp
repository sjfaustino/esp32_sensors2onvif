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

// A chip whose wall clock the test sets: a date in November 2026 to begin with.
struct Chip : CamChip {
  time_t wall = 1794000000;
  time_t wallTime() override { return wall; }
};

// Keeps the arm mode as the board would, so a test can start the next alarm from it.
struct Store : CamStore {
  CamArmMode mode = CAM_ARMED_AWAY;
  int64_t until = 0;
  bool armedSave(CamArmMode m, int64_t u) override {
    mode = m;
    until = u;
    return true;
  }
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
  Chip chip;
  CamRadio radio;
  Store store;
  Outside outside;
  CamStorage storage;
  CamNodeImages images{storage};
  CamSensorTable table{cfg, storage};
  CamMemoryFailures failures;
  CamAlarm alarm;
  // start: the start-up settings, which the alarm reads as it is made.
  explicit Rig(const char* folder, const CamConfig& start = CamConfig())
      : cfg(start), storage(emptyFolder(folder)), alarm(cfg, chip, radio, store, storage, table, images, outside, failures, [this] { return now; }) {}
  // A rig on the folder another left, as after a restart, with the settings it starts with. The clock starts again, as
  // millis() does; the wall clock is the same date until the test moves it.
  Rig(const Rig& before, const CamConfig& start)
      : cfg(start), storage(before.storage), alarm(cfg, chip, radio, store, storage, table, images, outside, failures, [this] { return now; }) {}

  void sensor(const char* path, const char* settings = nullptr) {
    storage.makeDirs(path);
    if (settings) storage.writeFile(std::string(path) + "/settings.txt", settings, strlen(settings));
  }
  void start() { alarm.begin(); }
  int report(const uint8_t mac[6], uint8_t input, bool active, uint32_t ageS = 0, uint8_t battery = CAM_BATTERY_UNKNOWN) {
    SensorReport r;
    r.active = active;
    r.age = ageS;
    r.battery = battery;
    return alarm.webReport(mac, input, r);
  }
  void run(uint32_t ms) {  // the loop's passes, every 10 ms
    for (uint32_t end = now + ms; (int32_t)(now - end) < 0;) {
      now += 10;
      alarm.tick(now);
    }
  }
  int id(uint8_t input = 1) const { return table.findSlot(DOOR, input); }
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

// Busy while an alert is on, live or a past event playing, so a scheduled restart holds off until it has ended. A past
// event still waiting does not count: waiting.bin keeps it through a restart.
static void the_alarm_is_busy_while_an_alert_is_on() {
  Rig r("cam_alarm_busy");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  TEST_ASSERT_FALSE(r.alarm.busy());
  r.report(DOOR, 1, true);
  TEST_ASSERT_TRUE(r.alarm.busy());
  r.report(DOOR, 1, false);
  r.run(11000);  // past its hold
  TEST_ASSERT_FALSE(r.alarm.busy());
  r.report(DOOR, 1, true, 120);  // a past event
  r.run(100);
  TEST_ASSERT_TRUE(r.past());
  TEST_ASSERT_TRUE(r.alarm.busy());
  r.run(60000);
  TEST_ASSERT_FALSE(r.past());
  TEST_ASSERT_FALSE(r.alarm.busy());
}

static void a_sensor_never_heard_is_reported_silent_once() {
  CamConfig quick;
  quick.silentMs = 1000;
  Rig r("cam_alarm_silent", quick);
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  r.run(900);
  TEST_ASSERT_EQUAL(0, r.logged(CAM_EVENT_SILENT));
  r.run(200);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_SILENT));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_SILENT));
  TEST_ASSERT_TRUE(r.table.healthOf(r.id()).silent);
  TEST_ASSERT_FALSE(r.on());  // and raises no alarm unless asked to
  r.run(5000);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_SILENT));
  r.report(DOOR, 1, false);  // heard again
  TEST_ASSERT_FALSE(r.table.healthOf(r.id()).silent);
}

static void a_silent_sensor_can_raise_the_alarm_when_asked_to() {
  CamConfig quick;
  quick.silentMs = 1000;
  Rig r("cam_alarm_silent_alarm", quick);
  r.alarm.setHealthAlarm(true);  // as the page turns it on
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
  Rig after(before, CamConfig());
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

static void a_timed_disarm_that_ends_while_the_hub_is_off_is_over_at_the_start() {
  Rig before("cam_alarm_timed_restart");
  before.sensor("AA-BB-CC-DD-EE-01/1");
  before.start();
  TEST_ASSERT_TRUE(before.alarm.setArmMode(CAM_DISARMED, "the test", 5));
  TEST_ASSERT_EQUAL((int64_t)before.chip.wall + 300, before.store.until);  // kept as a time of day, to outlast a restart
  CamConfig kept;
  kept.armMode = before.store.mode;
  kept.disarmedUntil = before.store.until;
  Rig early(before, kept);  // back after 100 s: 200 s still to go
  early.chip.wall = before.chip.wall + 100;
  early.start();
  early.run(100);
  TEST_ASSERT_FALSE(early.alarm.armed());
  TEST_ASSERT_EQUAL(200, early.alarm.disarmSecondsLeft(early.now));
  Rig late(before, kept);  // back after 10 minutes: the disarm is over
  late.chip.wall = before.chip.wall + 600;
  late.start();
  late.run(100);
  TEST_ASSERT_EQUAL(CAM_ARMED_AWAY, late.alarm.armMode());
}

static void the_start_is_logged_with_how_long_the_hub_was_off() {
  Rig before("cam_alarm_off_time");
  before.start();
  before.alarm.networkUp();
  before.run(100);  // logs its own start, then the time it is still running
  Rig after(before, CamConfig());
  after.chip.wall = before.chip.wall + 300;
  after.start();
  after.alarm.networkUp();
  after.run(100);
  const CamEvent& e = after.alarm.log().recent(0);
  TEST_ASSERT_EQUAL(CAM_EVENT_HUB_STARTED, e.kind);
  TEST_ASSERT_EQUAL(300, e.offS);
  TEST_ASSERT_TRUE(e.active);  // not asked for, so worth a look
}

static void the_start_waits_for_the_clock_before_it_is_logged() {
  Rig r("cam_alarm_no_clock");
  r.chip.wall = 1000;  // not set
  r.start();
  r.alarm.networkUp();
  r.run(1000);
  TEST_ASSERT_EQUAL(0, r.logged(CAM_EVENT_HUB_STARTED));
  r.chip.wall = 1794000000;
  r.run(100);
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_HUB_STARTED));
  TEST_ASSERT_EQUAL(CAM_AGE_UNKNOWN, r.alarm.log().recent(0).offS);  // no earlier run left a time
}

static void a_battery_that_will_run_out_within_a_week_is_reported_once() {
  Rig r("cam_alarm_battery");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.start();
  for (uint8_t percent : {40, 34, 28, 22, 21}) {  // 6% a day
    r.report(DOOR, 1, false, 0, percent);
    r.chip.wall += 86400;
  }
  TEST_ASSERT_EQUAL(1, r.logged(CAM_EVENT_BATTERY_RUNNING_OUT));
  TEST_ASSERT_EQUAL(1, r.outside.sent(CAM_EVENT_BATTERY_RUNNING_OUT));
  TEST_ASSERT_TRUE(r.table.nodeOf(r.id()).daysLeft >= 0 && r.table.nodeOf(r.id()).daysLeft < 7);
}

static void battery_readings_need_the_clock() {
  Rig r("cam_alarm_battery_no_clock");
  r.sensor("AA-BB-CC-DD-EE-01/1");
  r.chip.wall = 1000;
  r.start();
  r.report(DOOR, 1, false, 0, 50);
  TEST_ASSERT_FALSE(r.storage.exists("AA-BB-CC-DD-EE-01/battery.txt"));
}

// ---- CamSensorTable on its own: the folders it reads ----

static void the_table_reads_sensors_nodes_and_their_settings() {
  CamConfig cfg;
  CamStorage storage(emptyFolder("cam_table_read"));
  storage.makeDirs("AA-BB-CC-DD-EE-01/1");
  storage.makeDirs("AA-BB-CC-DD-EE-01/2");
  storage.writeFile("AA-BB-CC-DD-EE-01/1/name.txt", "Door", 4);
  storage.writeFile("AA-BB-CC-DD-EE-01/2/settings.txt", "zone=interior\n", 14);
  storage.writeFile("AA-BB-CC-DD-EE-01/settings.txt", "silent=600\n", 11);
  CamSensorTable table(cfg, storage);
  CamSensorTable::Changes c = table.rescan(1000);
  TEST_ASSERT_TRUE(c.readable);
  TEST_ASSERT_TRUE(c.listChanged);
  TEST_ASSERT_EQUAL(2, table.sensorCount());
  int door = table.findSlot(DOOR, 1), hall = table.findSlot(DOOR, 2);
  TEST_ASSERT_TRUE(door && hall && door != hall);
  TEST_ASSERT_TRUE(c.fresh[door - 1] && c.fresh[hall - 1]);
  TEST_ASSERT_EQUAL_STRING("Door", table.slotOf(door).name.c_str());
  TEST_ASSERT_TRUE(table.slotOf(hall).settings.interior);
  TEST_ASSERT_EQUAL(600000, table.silentMsFor(door));  // the node's own setting
  TEST_ASSERT_EQUAL(door, table.slotForPath("AA-BB-CC-DD-EE-01/1"));
  TEST_ASSERT_TRUE(table.findNode(DOOR) == door || table.findNode(DOOR) == hall);  // either of the node's sensors
  TEST_ASSERT_EQUAL_STRING("AA-BB-CC-DD-EE-01/1", table.slotName(door).c_str());
  CamSensorTable::Changes again = table.rescan(2000);  // nothing changed on storage
  TEST_ASSERT_FALSE(again.listChanged);
  TEST_ASSERT_FALSE(again.fresh[door - 1]);
}

static void the_table_reports_a_sensor_whose_folder_went() {
  CamConfig cfg;
  CamStorage storage(emptyFolder("cam_table_gone"));
  storage.makeDirs("AA-BB-CC-DD-EE-01/1");
  storage.makeDirs("AA-BB-CC-DD-EE-01/2");
  CamSensorTable table(cfg, storage);
  table.rescan(1000);
  int hall = table.findSlot(DOOR, 2);
  storage.removeTree("AA-BB-CC-DD-EE-01/2");
  CamSensorTable::Changes c = table.rescan(2000);
  TEST_ASSERT_TRUE(c.gone[hall - 1]);
  TEST_ASSERT_TRUE(c.listChanged);
  TEST_ASSERT_EQUAL(0, table.findSlot(DOOR, 2));
  TEST_ASSERT_EQUAL(1, table.sensorCount());
}

static void the_table_refuses_a_sensor_beyond_the_limit() {
  CamConfig cfg;
  CamStorage storage(emptyFolder("cam_table_full"));
  for (int i = 1; i <= CAM_MAX_SENSORS; i++) storage.makeDirs("AA-BB-CC-DD-EE-01/" + std::to_string(i));
  CamSensorTable table(cfg, storage);
  table.rescan(1000);
  TEST_ASSERT_EQUAL(CAM_MAX_SENSORS, table.sensorCount());
  TEST_ASSERT_FALSE(table.sensorFits("02-00-00-00-00-99/1"));          // a new one
  TEST_ASSERT_TRUE(table.sensorFits("AA-BB-CC-DD-EE-01/1/name.txt"));  // within one already there
  TEST_ASSERT_TRUE(table.sensorFits("plan.json"));                     // not a sensor's at all
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
  RUN_TEST(the_alarm_is_busy_while_an_alert_is_on);
  RUN_TEST(a_sensor_never_heard_is_reported_silent_once);
  RUN_TEST(a_silent_sensor_can_raise_the_alarm_when_asked_to);
  RUN_TEST(a_sensor_firing_too_often_is_reported_once);
  RUN_TEST(a_test_alert_takes_the_whole_path_even_disarmed);
  RUN_TEST(an_alert_waiting_for_the_nvr_is_replayed_after_a_restart);
  RUN_TEST(a_deleted_sensor_is_unknown_after_the_table_is_read_again);
  RUN_TEST(a_timed_disarm_that_ends_while_the_hub_is_off_is_over_at_the_start);
  RUN_TEST(the_start_is_logged_with_how_long_the_hub_was_off);
  RUN_TEST(the_start_waits_for_the_clock_before_it_is_logged);
  RUN_TEST(a_battery_that_will_run_out_within_a_week_is_reported_once);
  RUN_TEST(battery_readings_need_the_clock);
  RUN_TEST(the_table_reads_sensors_nodes_and_their_settings);
  RUN_TEST(the_table_reports_a_sensor_whose_folder_went);
  RUN_TEST(the_table_refuses_a_sensor_beyond_the_limit);
}
