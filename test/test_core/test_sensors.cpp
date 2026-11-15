// The sensor table, alerts and their holds, past events, delivery checks, and per-sensor and per-node settings.
#include "tests.h"

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
  TEST_ASSERT_TRUE(camParseNodeSettings("every=3600\nrepeat=4\n", s));
  TEST_ASSERT_EQUAL(3600, s.everyS);
  TEST_ASSERT_EQUAL(4, s.repeatS);
  TEST_ASSERT_EQUAL_STRING("every=3600\nrepeat=4\n", camNodeSettingsText(s).c_str());
  const char* bad[] = {"late=0\n", "late=86401\n", "late=\n", "late=-5\n", "late=1x\n", "late=99999999999\n", "hold=5\n", "silent=59\n", "silent=604801\n", "every=59\n", "every=86401\n", "repeat=0\n", "repeat=61\n"};
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

static void battery_forecast_follows_the_readings() {
  std::vector<CamBatteryPoint> points;
  const int64_t day = 86400, start = 1800000000;
  TEST_ASSERT_TRUE(camAddBatteryPoint(points, start, 80));
  TEST_ASSERT_FALSE(camAddBatteryPoint(points, start + 3600, 79));  // too soon after the last
  for (int d = 1; d <= 4; d++) TEST_ASSERT_TRUE(camAddBatteryPoint(points, start + d * day, (uint8_t)(80 - 2 * d)));
  TEST_ASSERT_EQUAL(36, camBatteryDaysLeft(points));  // 72% left at 2% a day
  std::vector<CamBatteryPoint> back;
  TEST_ASSERT_TRUE(camParseBatteryLog(camBatteryLogText(points), back));
  TEST_ASSERT_EQUAL(5, (int)back.size());
  TEST_ASSERT_EQUAL(72, back.back().percent);
  TEST_ASSERT_TRUE(camAddBatteryPoint(points, start + 5 * day, 100));  // a new battery
  TEST_ASSERT_EQUAL(1, (int)points.size());
  TEST_ASSERT_EQUAL(-1, camBatteryDaysLeft(points));
  std::vector<CamBatteryPoint> solar;
  for (int d = 0; d < 6; d++) camAddBatteryPoint(solar, start + d * day, (uint8_t)(d % 2 ? 70 : 72));
  TEST_ASSERT_EQUAL(-1, camBatteryDaysLeft(solar));  // not going down
  TEST_ASSERT_FALSE(camParseBatteryLog("1800000000 101\n", back));
  TEST_ASSERT_FALSE(camParseBatteryLog("soon 50\n", back));
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

void runSensorsTests() {
  RUN_TEST(delivery_is_checked_and_missed_alerts_kept_once);
  RUN_TEST(a_replay_stays_one_only_while_nothing_new_joins_it);
  RUN_TEST(past_events_share_records_only_as_they_would_live);
  RUN_TEST(a_full_past_event_queue_still_records_every_sensor);
  RUN_TEST(battery_forecast_follows_the_readings);
  RUN_TEST(hold_expires_and_retrigger_extends);
  RUN_TEST(hold_survives_millis_wrap);
  RUN_TEST(unknown_sensors_rejected);
  RUN_TEST(frame_cycles_between_active_sensors);
  RUN_TEST(shown_sensor_carries_direction_and_delay);
  RUN_TEST(motion_repeats_true_faster_than_nvr_latch);
  RUN_TEST(a_timed_disarm_ends_safely_after_a_restart);
  RUN_TEST(sensor_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(node_settings_round_trip_and_refuse_bad_values);
  RUN_TEST(quiet_hours_cross_midnight);
  RUN_TEST(a_sensor_can_have_its_own_hold);
}
