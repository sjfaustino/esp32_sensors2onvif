// The board's own decisions, from cam_board_rules.h: which Wi-Fi network to try and when the setup network opens, its
// password, what is kept through a restart, and the clock's time zone and restart minute. Each is what keeps the hub
// on its network or says why it restarted, and runs only on the board, so it is checked here.
#include "tests.h"
#include "cam_board_rules.h"
#include "cam_timing.h"

namespace {

// A hub off its network with both networks saved, its first try at the main one made at the start.
struct Wifi {
  CamWifiPlan plan;
  CamWifiPlan::Pass p;
  Wifi() {
    p.now = 1000;
    p.haveMain = p.haveBackup = true;
    plan.triedFromStart(p.now);
  }
  int at(uint32_t now) {
    p.now = now;
    int act = plan.step(p);
    p.justLost = false;
    if (act & CamWifiPlan::OPEN_SETUP) p.setupOpen = true;
    if (act & CamWifiPlan::CLOSE_SETUP) p.setupOpen = false;
    return act;
  }
};

}  // namespace

static void the_hub_tries_the_main_network_then_the_backup_then_opens_the_setup_network() {
  Wifi w;
  TEST_ASSERT_EQUAL(0, w.at(1000 + CAM_WIFI_TRY_MS));  // still giving the main network its time
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_BACKUP, w.at(1001 + CAM_WIFI_TRY_MS));
  TEST_ASSERT_EQUAL(1, w.plan.trying());
  uint32_t later = 1002 + 2 * CAM_WIFI_TRY_MS;
  TEST_ASSERT_EQUAL(CamWifiPlan::OPEN_SETUP | CamWifiPlan::TRY_MAIN, w.at(later));  // neither worked
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_BACKUP, w.at(later + CAM_WIFI_TRY_MS + 1));    // and round again, the setup network staying open
  TEST_ASSERT_TRUE(w.p.setupOpen);
}

// A router that turns the hub away for a moment, as after a restart, gets it back soon, a few times, before the backup.
static void a_network_that_says_come_back_is_tried_again_first() {
  Wifi w;
  w.p.turnedAway = true;
  TEST_ASSERT_EQUAL(0, w.at(1000 + CAM_WIFI_SAME_AFTER_MS - 1));
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_MAIN, w.at(1000 + CAM_WIFI_SAME_AFTER_MS));
  TEST_ASSERT_EQUAL(0, w.plan.trying());
  uint32_t t = 1000 + CAM_WIFI_SAME_AFTER_MS;
  for (int i = 1; i < CAM_WIFI_SAME_TRIES; i++) TEST_ASSERT_EQUAL(CamWifiPlan::TRY_MAIN, w.at(t += CAM_WIFI_SAME_AFTER_MS));
  TEST_ASSERT_EQUAL(0, w.at(t += CAM_WIFI_SAME_AFTER_MS));  // tried enough: it waits out its time as any try
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_BACKUP, w.at(t - CAM_WIFI_SAME_AFTER_MS + CAM_WIFI_TRY_MS + 1));
  TEST_ASSERT_EQUAL(1, w.plan.trying());
  w.p.turnedAway = false;
  w.p.connected = true;
  w.at(t + 2 * CAM_WIFI_TRY_MS);
  w.p.connected = false;
  w.p.justLost = true;
  w.p.turnedAway = true;
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_MAIN, w.at(t + 2 * CAM_WIFI_TRY_MS + 10));  // a new loss: its own tries again
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_MAIN, w.at(t + 2 * CAM_WIFI_TRY_MS + 10 + CAM_WIFI_SAME_AFTER_MS));
}

static void the_backup_that_joins_becomes_the_main_network_and_the_setup_network_closes() {
  Wifi w;
  w.at(1001 + CAM_WIFI_TRY_MS);  // trying the backup
  w.p.setupOpen = true;
  w.p.connected = true;
  TEST_ASSERT_EQUAL(CamWifiPlan::PROMOTE_BACKUP | CamWifiPlan::CLOSE_SETUP, w.at(2000 + CAM_WIFI_TRY_MS));
  TEST_ASSERT_EQUAL(0, w.plan.trying());
  TEST_ASSERT_EQUAL(0, w.at(3000 + CAM_WIFI_TRY_MS));  // once
}

static void a_hub_with_no_network_saved_opens_the_setup_network_and_tries_nothing() {
  Wifi w;
  w.p.haveMain = w.p.haveBackup = false;
  TEST_ASSERT_EQUAL(CamWifiPlan::OPEN_SETUP, w.at(2000));
  TEST_ASSERT_EQUAL(0, w.at(5 * CAM_WIFI_TRY_MS));
}

static void someone_on_the_setup_network_stops_the_tries_until_they_leave() {
  Wifi w;
  w.p.setupOpen = true;
  w.p.setupStations = 1;
  TEST_ASSERT_EQUAL(CamWifiPlan::STOP_TRYING, w.at(2000));  // the radio leaves the network it was trying, once
  TEST_ASSERT_TRUE(w.plan.guest());
  TEST_ASSERT_EQUAL(0, w.at(10 * CAM_WIFI_TRY_MS));  // no tries while they are there, however long
  w.p.setupStations = 0;
  TEST_ASSERT_FALSE(w.at(10 * CAM_WIFI_TRY_MS + 1) & CamWifiPlan::STOP_TRYING);
  TEST_ASSERT_FALSE(w.plan.guest());
  TEST_ASSERT_TRUE(w.at(12 * CAM_WIFI_TRY_MS) & (CamWifiPlan::TRY_MAIN | CamWifiPlan::TRY_BACKUP));  // and the tries go on
}

static void a_lost_connection_tries_the_main_network_at_once() {
  Wifi w;
  w.p.connected = true;
  w.at(1500);
  w.p.connected = false;
  w.p.justLost = true;
  TEST_ASSERT_EQUAL(CamWifiPlan::TRY_MAIN, w.at(1600));
  TEST_ASSERT_EQUAL(0, w.at(1600 + CAM_WIFI_TRY_MS));  // its time counts from then
}

static void the_setup_network_password_is_the_compiled_one_or_the_hub_password_or_none() {
  std::string out;
  TEST_ASSERT_EQUAL(CAM_SETUP_LOCKED, camSetupKey("compiled1", "hub-password", out));
  TEST_ASSERT_EQUAL_STRING("compiled1", out.c_str());
  TEST_ASSERT_EQUAL(CAM_SETUP_LOCKED, camSetupKey("", "hub-password", out));
  TEST_ASSERT_EQUAL_STRING("hub-password", out.c_str());
  TEST_ASSERT_EQUAL(CAM_SETUP_LOCKED, camSetupKey("short", "hub-password", out));  // a compiled one too short is not used
  TEST_ASSERT_EQUAL(CAM_SETUP_OPEN, camSetupKey("", "", out));                     // a blank hub
  TEST_ASSERT_EQUAL(CAM_SETUP_OFF, camSetupKey("", "seven77", out));               // never open for a hub that has a password
}

// A blank hub's open setup network only after its button, for a while, and while someone is on it; others as ever.
static void a_blank_hubs_open_setup_network_waits_for_its_button() {
  CamSetupButton b;
  TEST_ASSERT_FALSE(camSetupMayOpen(CAM_SETUP_OPEN, b, 1000, 0));  // not pressed
  TEST_ASSERT_TRUE(camSetupMayOpen(CAM_SETUP_LOCKED, b, 1000, 0));
  TEST_ASSERT_FALSE(camSetupMayOpen(CAM_SETUP_OFF, b, 1000, 0));
  b.pressed = true;
  b.pressedAt = 0xFFFFF000u;  // pressed just before the millisecond clock wraps
  TEST_ASSERT_TRUE(camSetupMayOpen(CAM_SETUP_OPEN, b, 0x1000, 0));
  TEST_ASSERT_TRUE(camSetupMayOpen(CAM_SETUP_OPEN, b, 0xFFFFF000u + CAM_OPEN_SETUP_MS - 1, 0));
  TEST_ASSERT_FALSE(camSetupMayOpen(CAM_SETUP_OPEN, b, 0xFFFFF000u + CAM_OPEN_SETUP_MS, 0));  // its time is up
  TEST_ASSERT_TRUE(camSetupMayOpen(CAM_SETUP_OPEN, b, 0xFFFFF000u + CAM_OPEN_SETUP_MS, 1));   // but not while someone is on it
  CamSetupButton none;
  none.fitted = false;
  TEST_ASSERT_TRUE(camSetupMayOpen(CAM_SETUP_OPEN, none, 1000, 0));  // a board without one opens it as ever
}

static void what_is_kept_through_a_restart_reads_back_only_when_this_layout_wrote_it() {
  CamKept k;
  memset(&k, 0xA5, sizeof(k));  // what a power cut leaves
  TEST_ASSERT_EQUAL(RESTART_UNKNOWN, camReadKept(k, true));
  TEST_ASSERT_FALSE(camUpdateNeedsNvr(k));
  camKeepCause(k, RESTART_FIRMWARE);
  camKeepUpdate(k, true);
  TEST_ASSERT_EQUAL(RESTART_FIRMWARE, camReadKept(k, true));  // the next start
  TEST_ASSERT_TRUE(camUpdateNeedsNvr(k));                     // the update still to prove itself
  TEST_ASSERT_EQUAL(RESTART_UNKNOWN, camReadKept(k, true));   // read once: a restart that writes no cause is not this one
  camForgetUpdate(k);
  TEST_ASSERT_FALSE(camUpdateNeedsNvr(k));
  camKeepCause(k, RESTART_BUTTON);
  TEST_ASSERT_EQUAL(RESTART_UNKNOWN, camReadKept(k, false));  // a crash or power cut after it: not the hub's own restart
  camKeepCause(k, 999);
  TEST_ASSERT_EQUAL(RESTART_UNKNOWN, camReadKept(k, true));  // a cause this firmware does not know
  k.mark = 0x12345678;                                       // another layout's, kept by the firmware before
  k.updateMark = CAM_UPDATE_MARK;
  k.needNvr = 1;
  camReadKept(k, true);
  TEST_ASSERT_FALSE(camUpdateNeedsNvr(k));
}

static void a_start_is_planned_only_after_the_hubs_own_restarts_that_someone_asked_for() {
  TEST_ASSERT_TRUE(camStartPlanned(true, RESTART_FIRMWARE));
  TEST_ASSERT_TRUE(camStartPlanned(true, RESTART_BUTTON));
  TEST_ASSERT_FALSE(camStartPlanned(false, RESTART_FIRMWARE));  // a crash after it
  TEST_ASSERT_FALSE(camStartPlanned(true, RESTART_WIFI_LOST));  // one of its restarts for a fault
  TEST_ASSERT_FALSE(camStartPlanned(true, RESTART_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("Firmware update", camRestartText(RESTART_FIRMWARE));
  TEST_ASSERT_EQUAL_STRING("Software restart", camRestartText(999));
}

static void the_time_zone_is_written_as_posix_wants_it() {
  TEST_ASSERT_EQUAL_STRING("<+0000>-0:00", camPosixTz(0, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("<+0000>-0:00<+0100>,M3.5.0/1,M10.5.0/2", camPosixTz(0, 1).c_str());     // Lisbon, London
  TEST_ASSERT_EQUAL_STRING("<+0100>-1:00<+0200>,M3.5.0/2,M10.5.0/3", camPosixTz(60, 1).c_str());    // Paris, Berlin
  TEST_ASSERT_EQUAL_STRING("<-0500>+5:00<-0400>,M3.2.0/2,M11.1.0/2", camPosixTz(-300, 2).c_str());  // New York
  TEST_ASSERT_EQUAL_STRING("<+0530>-5:30", camPosixTz(330, 0).c_str());                             // India
}

static void the_scheduled_restart_minute_is_its_day_and_minute_with_the_clock_set() {
  CamSchedule s;
  s.on = true;
  s.day = 3;
  s.minute = 240;
  TEST_ASSERT_TRUE(camScheduledMinute(s, 3, 240));
  TEST_ASSERT_FALSE(camScheduledMinute(s, 4, 240));
  TEST_ASSERT_FALSE(camScheduledMinute(s, 3, 241));
  TEST_ASSERT_FALSE(camScheduledMinute(s, -1, -1));  // a clock not set
  s.day = -1;
  TEST_ASSERT_TRUE(camScheduledMinute(s, 0, 240));  // every day
  s.on = false;
  TEST_ASSERT_FALSE(camScheduledMinute(s, 0, 240));
}

void runBoardTests() {
  RUN_TEST(the_hub_tries_the_main_network_then_the_backup_then_opens_the_setup_network);
  RUN_TEST(a_network_that_says_come_back_is_tried_again_first);
  RUN_TEST(the_backup_that_joins_becomes_the_main_network_and_the_setup_network_closes);
  RUN_TEST(a_hub_with_no_network_saved_opens_the_setup_network_and_tries_nothing);
  RUN_TEST(someone_on_the_setup_network_stops_the_tries_until_they_leave);
  RUN_TEST(a_lost_connection_tries_the_main_network_at_once);
  RUN_TEST(the_setup_network_password_is_the_compiled_one_or_the_hub_password_or_none);
  RUN_TEST(what_is_kept_through_a_restart_reads_back_only_when_this_layout_wrote_it);
  RUN_TEST(a_start_is_planned_only_after_the_hubs_own_restarts_that_someone_asked_for);
  RUN_TEST(the_time_zone_is_written_as_posix_wants_it);
  RUN_TEST(the_scheduled_restart_minute_is_its_day_and_minute_with_the_clock_set);
  RUN_TEST(a_blank_hubs_open_setup_network_waits_for_its_button);
}
