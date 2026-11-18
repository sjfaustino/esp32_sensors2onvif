// When the hub restarts itself, and whether a new firmware is kept: CamRestartRules, driven with what the board would
// see. Each restart stops the hub hearing its sensors, so each rule is checked for when it must and must not fire.
#include "tests.h"
#include "cam_restart_rules.h"
#include "cam_timing.h"

namespace {

// A hub on its network, with memory, a router that answers, and nothing going on.
struct Hub {
  bool arpAnswers = true;
  int arpAsked = 0;
  CamRestartRules rules{[this] {
    arpAsked++;
    return arpAnswers;
  }};
  CamRestartRules::Pass p;
  Hub() {
    p.now = 1000;
    p.haveNetwork = true;
    p.connected = true;
    p.freeHeap = 100000;
    p.pingReplies = 1;
    p.lastPingReply = 1000;
  }
  // The loop's passes, every 100 ms, with the router answering while connected if it does; the first rule to fire.
  CamRestartRule run(uint32_t ms, bool routerAnswers = true) {
    for (uint32_t end = p.now + ms; (int32_t)(p.now - end) < 0;) {
      p.now += 100;
      if (p.connected && routerAnswers) p.lastPingReply = p.now;
      CamRestartRule rule = rules.check(p);
      if (rule != CAM_RULE_NONE) return rule;
    }
    return CAM_RULE_NONE;
  }
};

}  // namespace

static void a_working_hub_keeps_running() {
  Hub h;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(24UL * 3600 * 1000));
}

// Off the network five minutes, the hub starts clean; but not while someone is on the setup page, and not while it has
// no network to go back to.
static void off_wifi_five_minutes_restarts_but_not_during_setup() {
  Hub h;
  h.run(1000);
  h.p.connected = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(CAM_WIFI_LOST_RESTART_MS - 1000));
  TEST_ASSERT_TRUE(h.rules.lostAt() != 0);
  TEST_ASSERT_EQUAL(CAM_RULE_WIFI_LOST, h.run(2000));

  Hub back;  // back in time: the count starts again
  back.p.connected = false;
  back.run(CAM_WIFI_LOST_RESTART_MS - 1000);
  back.p.connected = true;
  back.run(100);
  TEST_ASSERT_EQUAL(0, back.rules.lostAt());
  back.p.connected = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, back.run(CAM_WIFI_LOST_RESTART_MS - 1000));

  Hub guest;
  guest.p.connected = false;
  guest.p.guest = true;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, guest.run(3 * CAM_WIFI_LOST_RESTART_MS));
  guest.p.guest = false;  // they leave: five minutes from then
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, guest.run(CAM_WIFI_LOST_RESTART_MS - 1000));
  TEST_ASSERT_EQUAL(CAM_RULE_WIFI_LOST, guest.run(2000));

  Hub fresh;  // never set up
  fresh.p.haveNetwork = false;
  fresh.p.connected = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, fresh.run(3 * CAM_WIFI_LOST_RESTART_MS));
}

static void low_memory_restarts_only_when_it_lasts() {
  Hub h;
  h.p.freeHeap = CAM_LOW_HEAP_BYTES - 1;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(CAM_LOW_HEAP_RESTART_MS - 1000));
  h.p.freeHeap = CAM_LOW_HEAP_BYTES;  // recovered: the count starts again
  h.run(100);
  h.p.freeHeap = CAM_LOW_HEAP_BYTES - 1;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(CAM_LOW_HEAP_RESTART_MS - 1000));
  TEST_ASSERT_EQUAL(CAM_RULE_LOW_MEMORY, h.run(2000));
}

static void a_stalled_server_restarts() {
  Hub h;
  h.p.stalledMs = CAM_STALLED_RESTART_MS;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(100));
  h.p.stalledMs = CAM_STALLED_RESTART_MS + 1;
  TEST_ASSERT_EQUAL(CAM_RULE_STALLED, h.run(100));
}

// Only a router that has answered counts as stopping, so one that ignores ping never causes restarts; and not during a
// firmware upload, whose data arriving shows the network works.
static void a_silent_router_restarts_once_it_has_answered() {
  Hub h;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(CAM_ROUTER_SILENT_RESTART_MS - 1000, false));
  TEST_ASSERT_EQUAL(CAM_RULE_ROUTER_SILENT, h.run(2000, false));

  Hub ignores;
  ignores.p.pingReplies = 0;
  ignores.p.pingLost = 100;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, ignores.run(10 * CAM_ROUTER_SILENT_RESTART_MS, false));

  Hub uploading;
  uploading.p.uploading = true;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, uploading.run(3 * CAM_ROUTER_SILENT_RESTART_MS, false));
  uploading.p.uploading = false;
  TEST_ASSERT_EQUAL(CAM_RULE_ROUTER_SILENT, uploading.run(100, false));
}

// Checked once a connection with a fixed address has had time: no ping reply and no ARP from the router means the
// address is wrong, and the hub forgets it, because nothing else could reach the hub again.
static void a_fixed_address_that_reaches_nothing_is_forgotten() {
  Hub h;
  h.p.usingStatic = true;
  h.p.pingReplies = 0;
  h.p.pingLost = 5;
  h.arpAnswers = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(CAM_STATIC_CHECK_MS - 1000, false));
  TEST_ASSERT_EQUAL(0, h.arpAsked);
  TEST_ASSERT_EQUAL(CAM_RULE_BAD_FIXED_ADDRESS, h.run(2000, false));

  Hub arp;  // a router that ignores ping but answers ARP is there
  arp.p.usingStatic = true;
  arp.p.pingReplies = 0;
  arp.p.pingLost = 5;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, arp.run(3 * CAM_STATIC_CHECK_MS, false));
  TEST_ASSERT_EQUAL(1, arp.arpAsked);  // once a connection

  Hub dhcp;
  dhcp.p.pingReplies = 0;
  dhcp.p.pingLost = 5;
  dhcp.arpAnswers = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, dhcp.run(3 * CAM_STATIC_CHECK_MS, false));
  TEST_ASSERT_EQUAL(0, dhcp.arpAsked);
}

// The scheduled restart waits while an alert is on, live or past, so it never cuts an NVR record short.
static void a_scheduled_restart_waits_for_alerts_to_end() {
  Hub h;
  h.run(CAM_SCHEDULE_AFTER_START_MS);
  h.p.scheduledMinute = true;
  h.p.alarmBusy = true;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(60000));
  h.p.scheduledMinute = false;  // the minute passes while the alert goes on
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(5 * 60000));
  TEST_ASSERT_TRUE(h.rules.scheduledWaiting());
  h.p.alarmBusy = false;
  TEST_ASSERT_EQUAL(CAM_RULE_SCHEDULED, h.run(100));

  Hub quiet;
  quiet.run(CAM_SCHEDULE_AFTER_START_MS);
  quiet.p.scheduledMinute = true;
  TEST_ASSERT_EQUAL(CAM_RULE_SCHEDULED, quiet.run(100));
}

// An alert that will not end does not hold the restart for ever: after 30 minutes it gives up until the next time.
static void a_scheduled_restart_gives_up_after_waiting_long() {
  Hub h;
  h.run(CAM_SCHEDULE_AFTER_START_MS);
  h.p.scheduledMinute = true;
  h.p.alarmBusy = true;
  h.run(60000);
  h.p.scheduledMinute = false;
  h.run(CAM_SCHEDULE_WAIT_MS);
  TEST_ASSERT_FALSE(h.rules.scheduledWaiting());
  h.p.alarmBusy = false;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(60000));
  h.p.scheduledMinute = true;  // the next time
  TEST_ASSERT_EQUAL(CAM_RULE_SCHEDULED, h.run(100));
}

// Soon after a start it is still the minute that caused it, which must not restart the hub again.
static void no_scheduled_restart_soon_after_a_start() {
  Hub h;
  h.p.scheduledMinute = true;
  TEST_ASSERT_EQUAL(CAM_RULE_NONE, h.run(60000));
}

static void a_new_firmware_is_kept_once_it_has_done_the_work() {
  Hub h;
  h.p.connected = false;
  h.run(5000);
  h.p.connected = true;
  h.run(CAM_FIRMWARE_PROOF_MS - 1000);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_WAIT, h.rules.firmware(h.p, true, false, false));
  h.run(2000);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_KEEP, h.rules.firmware(h.p, true, false, false));
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_WAIT, h.rules.firmware(h.p, false, false, false));  // ESP-NOW not running
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_WAIT, h.rules.firmware(h.p, true, true, false));    // the NVR listened before, not yet now
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_KEEP, h.rules.firmware(h.p, true, true, true));
}

static void a_new_firmware_that_cannot_prove_itself_is_undone() {
  Hub h;
  h.run(CAM_FIRMWARE_PROOF_DEADLINE_MS - 2000);  // the clock started at 1 s
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_WAIT, h.rules.firmware(h.p, true, true, false));
  h.run(2000);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_UNDO, h.rules.firmware(h.p, true, true, false));

  Hub setup;  // nothing to prove it on: kept after a minute
  setup.p.haveNetwork = false;
  setup.p.connected = false;
  setup.run(CAM_FIRMWARE_PROOF_MS - 1000);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_WAIT, setup.rules.firmware(setup.p, false, false, false));
  setup.run(2000);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_KEEP, setup.rules.firmware(setup.p, false, false, false));
}

void runRestartTests() {
  RUN_TEST(a_working_hub_keeps_running);
  RUN_TEST(off_wifi_five_minutes_restarts_but_not_during_setup);
  RUN_TEST(low_memory_restarts_only_when_it_lasts);
  RUN_TEST(a_stalled_server_restarts);
  RUN_TEST(a_silent_router_restarts_once_it_has_answered);
  RUN_TEST(a_fixed_address_that_reaches_nothing_is_forgotten);
  RUN_TEST(a_scheduled_restart_waits_for_alerts_to_end);
  RUN_TEST(a_scheduled_restart_gives_up_after_waiting_long);
  RUN_TEST(no_scheduled_restart_soon_after_a_start);
  RUN_TEST(a_new_firmware_is_kept_once_it_has_done_the_work);
  RUN_TEST(a_new_firmware_that_cannot_prove_itself_is_undone);
}
