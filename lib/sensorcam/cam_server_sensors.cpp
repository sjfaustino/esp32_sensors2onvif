// SensorCamServer: sensor reports, node health, the event history and the sensor table.
#include "cam_server.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_server_internal.h"

bool SensorCamServer::downloadingHistory() const {
  for (const HttpConn& c : http_)
    if (c.fd >= 0 && c.stream == &csvRead_) return true;
  return false;
}

void SensorCamServer::saveNow() {
  persist();
  writeAlive(millis_());  // a planned restart then knows exactly when the hub stopped
}

// Called before reports are confirmed, so a confirmed alert survives a power cut. True when everything is on storage.
bool SensorCamServer::persist() {
  bool events = log_.save(downloadingHistory());  // a failed save is tried again, since confirmations wait on it
  if (waitingDirty_ || pendingChanged_ || waitingSaveFailed_) saveWaiting();
  return events && !waitingSaveFailed_;
}

// Alerts that still need an NVR record: the past events waiting their turn, then the alerts whose recording has not yet
// been checked. After a restart they all play back as past events, because the NVR may never have got them.
// waiting.bin: "WAIT", version 1, the count, then 9 bytes each: MAC address, input, direction, and flags: 1 if it stands
// for several sensors, 2 if the NVR missed it and it waits for the NVR to listen again, 4 if it was logged as missed
// already.
void SensorCamServer::saveWaiting() {
  std::string out = "WAIT";
  auto add = [&](const uint8_t mac[6], uint8_t input, uint8_t direction, bool several, bool missed, bool replay) {
    out.append((const char*)mac, 6);
    out += (char)input;
    out += (char)direction;
    out += (char)((several ? 1 : 0) | (missed ? 2 : 0) | (replay ? 4 : 0));
  };
  out += (char)1;
  out += (char)0;  // the count, filled in below
  for (int n = 0; n < delayed_.count(); n++) add(delayed_.at(n).mac, delayed_.at(n).input, delayed_.at(n).direction, delayed_.at(n).several, false, delayed_.at(n).replay);
  const CamDelayedQueue& missed = delivery_.missed();
  for (int n = 0; n < missed.count(); n++) add(missed.at(n).mac, missed.at(n).input, 0, missed.at(n).several, true, true);
  for (int id = 1; id <= sensors_.count(); id++) {
    const CamPendingCheck& p = delivery_.pending(id);
    if (slots_[id - 1].used && p.on) add(p.mac, p.input, 0, p.several, false, p.replay);
  }
  out[5] = (char)((out.size() - 6) / 9);
  waitingSaveFailed_ = !storage_.writeFile(WAITING_FILE, out.data(), out.size());
  waitingDirty_ = pendingChanged_ = false;
}

void SensorCamServer::loadWaiting() {
  std::string in;
  if (!storage_.readFile(WAITING_FILE, in, 6 + 255 * 9) || in.size() < 6 || in.compare(0, 4, "WAIT") != 0 || in[4] != 1) return;
  size_t count = (uint8_t)in[5];
  if (in.size() != 6 + count * 9) return;
  uint32_t now = millis_();
  for (size_t i = 0; i < count; i++) {
    const uint8_t* p = (const uint8_t*)in.data() + 6 + i * 9;
    int id = findSlot(p, p[6]);
    if (!id) continue;  // deleted since
    if (p[8] & 2) delivery_.keepMissed(p, p[6], p[8] & 1, holdFor(id), now);
    else delayed_.add(p, p[6], p[7], false, now, holdFor(id), p[8] & 1, (p[8] & 4) != 0);  // old times mean nothing on a new millis() clock
  }
  waitingDirty_ = count > 0;
}

// Rewritten every 10 minutes while the clock is set, so after a power cut the hub can say how long it was off.
void SensorCamServer::writeAlive(uint32_t now) {
  aliveAt_ = now;
  aliveWritten_ = true;
  uint64_t t = (uint64_t)(int64_t)time(nullptr);
  char out[8];  // seconds since 1970, little-endian
  for (int i = 0; i < 8; i++) out[i] = (char)(t >> (8 * i));
  if ((int64_t)t >= (int64_t)CAM_CLOCK_VALID_AFTER) storage_.writeFile(ALIVE_FILE, out, sizeof(out));
}

// While the hub is off its network, nothing reaches the NVR. Someone may be keeping it off on purpose.
void SensorCamServer::networkLost(uint32_t seconds) {
  hubEvent(CAM_EVENT_NETWORK_LOST, "", seconds, camEventMessage(CAM_EVENT_NETWORK_LOST, cfg_.hostName, "", seconds, false));
}

// Logged once the network is up, and once the clock is set or has had long enough to be.
void SensorCamServer::noteHubStart(uint32_t now) {
  time_t t = time(nullptr);
  bool clockOk = t >= CAM_CLOCK_VALID_AFTER;
  if (startNoted_ || !networkSeen_ || (!clockOk && now - beganAt_ < CAM_START_CLOCK_WAIT_MS)) return;
  startNoted_ = true;
  bool offKnown = clockOk && lastAlive_ >= CAM_CLOCK_VALID_AFTER && t >= lastAlive_ && (int64_t)(t - lastAlive_) < (int64_t)CAM_AGE_UNKNOWN;
  uint32_t off = offKnown ? (uint32_t)(t - lastAlive_) : CAM_AGE_UNKNOWN;
  std::string reason = cfg_.startReason.empty() ? std::string("unknown") : cfg_.startReason;
  hubEvent(CAM_EVENT_HUB_STARTED, reason, off, camEventMessage(CAM_EVENT_HUB_STARTED, cfg_.hostName, reason, off, false));
}

// The NVR is listening when it holds a subscription and either the last event pushed to it arrived, or, for one that
// pulls events, it asked for events within the last minute. With no NVR address set, any subscriber counts.
bool SensorCamServer::nvrListening(uint32_t now) const {
  uint32_t nvr = cfg_.nvrIp.empty() ? 0 : inet_addr(cfg_.nvrIp.c_str());
  for (int i = 0; i < SubTable::SIZE; i++) {
    const CamSub& s = subs_.at(i);
    if (!s.used || (nvr && s.peer != nvr)) continue;
    if (s.push ? !s.pushFailed : now - s.pulledAt < CAM_PULL_ALIVE_MS) return true;  // other requests from it do not count
  }
  return false;
}

#ifdef CAM_TEST_HOOKS
void SensorCamServer::setNvrGoneMs(uint32_t ms) {
  nvrGoneMs_ = ms;
  nvrSeenAt_ = millis_();
  nvrGone_ = false;
}
#endif

// A pending check is saved with the report that started its alert, not on its own, so an alert costs one write. One
// cleared since is still in the file; after a power cut it replays, which is a false alert rather than a missed one.
void SensorCamServer::startAlert(int id, bool delayed, uint32_t now, bool replay) {
  if (!cfg_.watchNvr) return;  // nothing would check it
  delivery_.started(id, slots_[id - 1].mac, slots_[id - 1].input, delayed, sensors_.several(id), replay, holdFor(id), now);
  pendingChanged_ = true;
}

// See CamDeliveryWatch. One the NVR did not get is logged and sent, because nothing else would show it is missing.
void SensorCamServer::checkNvr(uint32_t now) {
  bool listening = nvrListening(now);
  CamDelayed missed;
  while (listening && delivery_.takeMissed(missed)) {  // what the NVR missed plays now, as past events
    int id = findSlot(missed.mac, missed.input);
    if (id) delayed_.add(missed.mac, missed.input, missed.direction, false, now, holdFor(id), missed.several, true);
    waitingDirty_ = true;
  }
  if (listening) {
    nvrSeenAt_ = now;
    if (nvrGone_) hubEvent(CAM_EVENT_NVR_BACK, "", CAM_AGE_UNKNOWN, camEventMessage(CAM_EVENT_NVR_BACK, cfg_.hostName, "", CAM_AGE_UNKNOWN, false));
    nvrGone_ = false;
  } else if (!nvrGone_ && now - nvrSeenAt_ >= nvrGoneMs_) {
    hubEvent(CAM_EVENT_NVR_GONE, "", CAM_AGE_UNKNOWN, camEventMessage(CAM_EVENT_NVR_GONE, cfg_.hostName, "", CAM_AGE_UNKNOWN, false));
    nvrGone_ = true;  // after, so one that ran out of memory is tried again
  }
  notGot_.clear();
  delivery_.check(now, listening, [&](int id) { return slots_[id - 1].used && sensors_.active(id); }, notGot_);
  for (const CamDeliveryWatch::Missed& m : notGot_) {
    waitingDirty_ = true;
    if (!m.log || !slots_[m.id - 1].used) continue;  // a replay that failed again waits again, without a second entry
    SensorReport r;
    r.battery = health_[m.id - 1].battery;
    r.active = true;
    logEvent(m.id, r, m.delayed, CAM_EVENT_NOT_DELIVERED);
    notifyHook(m.id, CAM_EVENT_NOT_DELIVERED, m.delayed);
  }
}

void SensorCamServer::healthAlert(int id, CamEventKind kind, uint32_t now) {
  SensorReport r;
  r.battery = health_[id - 1].battery;
  logEvent(id, r, false, kind);
  notifyHook(id, kind, false);
  if (!cfg_.healthAlarm) return;
  interruptPastEvent(now);
  sensors_.trigger(id, now, 0, false, holdFor(id));
}

// Written straight into the event, without using the heap, so an alert is logged even when memory has run out.
void SensorCamServer::logEvent(int id, const SensorReport& r, bool delayed, CamEventKind kind) {
  const Slot& s = slots_[id - 1];
  char sensor[sizeof(CamEvent::sensor)];
  snprintf(sensor, sizeof(sensor), "%02X-%02X-%02X-%02X-%02X-%02X/%u", s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5], (unsigned)s.input);
  addEvent(sensor, s.name.c_str(), r, delayed, kind);
}

// The hub's own events: in the history as sensor "hub", and sent as node problems.
void SensorCamServer::hubEvent(CamEventKind kind, const std::string& detail, uint32_t offS, const std::string& message) {
  SensorReport r;  // age 0: it happened when it was logged
  addEvent("hub", detail.c_str(), r, false, kind, offS);
  notifyHubHook(kind, detail, message);
}

void SensorCamServer::addEvent(const char* sensor, const char* text, const SensorReport& r, bool delayed, CamEventKind kind, uint32_t offS) {
  CamEvent e;
  e.received = time(nullptr);
  e.age = r.age;
  snprintf(e.sensor, sizeof(e.sensor), "%s", sensor);
  camCopyText(e.text, sizeof(e.text), text);
  e.offS = offS;
  e.kind = kind;
  e.active = r.active;
  e.delayed = delayed;
  e.battery = r.battery;
  e.targetCount = r.targetCount;
  for (int i = 0; i < CAM_MAX_TARGETS; i++) e.targets[i] = r.targets[i];
  log_.add(e);
}

uint32_t SensorCamServer::holdFor(int id) const {
  uint32_t seconds = slots_[id - 1].settings.holdS;
  return seconds ? seconds * 1000 : cfg_.holdMs;
}

bool SensorCamServer::quietNow(int id) const { return camQuiet(slots_[id - 1].settings, cfg_.localMinute ? cfg_.localMinute() : -1); }

bool SensorCamServer::applyReport(int id, const SensorReport& r, uint32_t now) {
  if (id < 1 || id > sensors_.count()) return false;
  Health& h = health_[id - 1];
  h.heard = true;
  h.heardAt = now;
  h.silent = false;
  bool batteryWentLow = false;
  if (r.battery != CAM_BATTERY_UNKNOWN) {  // a report without a reading must not erase the last one
    h.battery = r.battery;
    if (r.battery <= LOW_BATTERY_PERCENT && !h.lowBattery) {
      h.lowBattery = true;
      batteryWentLow = true;
    } else if (r.battery >= LOW_BATTERY_PERCENT + 5) {  // the gap stops a reading near the limit from alerting again and again
      h.lowBattery = false;
    }
  }

  uint32_t lateAfter = slots_[id - 1].node.lateS ? slots_[id - 1].node.lateS : CAM_PAST_AFTER_S;
  bool late = r.age > lateAfter;  // an unknown age counts as late
  bool wasActive = sensors_.active(id);
  bool wasReported = h.reported;
  h.reported = r.active;
  bool quiet = r.active && quietNow(id);  // logged, but the NVR is not told
  if (r.active && late && !quiet) {
    queueDelayed(id, r, now);
  } else if (r.active && !quiet) {
    interruptPastEvent(now);
    // Starts the alert, or extends it, so activations closer together than the hold make one NVR record.
    sensors_.trigger(id, now, reportDirection(r), false, holdFor(id));
    if (!wasActive) startAlert(id, false, now);
  }
  // A clear report does not end the alert early, for the same reason: the alert ends when its hold runs out.
  // Repeats and heartbeats that change nothing would push real events out of the log.
  // An activation is logged unless it only repeats one whose alert is still running; a sensor that never reports clear
  // still has each new alert logged.
  if (late || (r.active ? !wasReported || !wasActive : wasReported)) logEvent(id, r, late, quiet ? CAM_EVENT_QUIET_ALERT : CAM_EVENT_REPORT);
  if (r.active && !quiet && (late || !wasActive)) notifyHook(id, CAM_EVENT_REPORT, late);
  if (batteryWentLow) healthAlert(id, CAM_EVENT_LOW_BATTERY, now);
  return true;
}

// A live alert must not share its NVR record with a past event, which would weave a stale picture into the recording of
// something happening now. The past event stops and goes back in the queue, to play in full once the live one is over.
void SensorCamServer::interruptPastEvent(uint32_t now) {
  int id = playingId_;
  playingId_ = 0;
  if (!id || !sensors_.pastEvent(id)) return;
  sensors_.clear(id);
  delivery_.forget(id);  // it never finished, so it is not checked; it plays again instead
  delayed_.add(playing_.mac, playing_.input, playing_.direction, false, now, holdFor(id), playing_.several, playing_.replay);
  waitingDirty_ = true;
}

// A late alert waits for its own NVR record; CamDelayedQueue decides which alerts share one.
void SensorCamServer::queueDelayed(int id, const SensorReport& r, uint32_t now) {
  const Slot& s = slots_[id - 1];
  bool known = r.age != CAM_AGE_UNKNOWN && r.age < 1000000;  // the millisecond clock covers 49 days
  delayed_.add(s.mac, s.input, reportDirection(r), known, known ? now - r.age * 1000 : now, holdFor(id));
  waitingDirty_ = true;
}

#ifdef CAM_TEST_HOOKS
void SensorCamServer::endAlerts() {
  for (int id = 1; id <= sensors_.count(); id++) sensors_.clear(id);
}
#endif

// Keeps the most recent senders, so a new sensor can be seen on the page before it has been added.
void SensorCamServer::noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi) {
  uint32_t now = millis_();
  UnknownSender* entry = nullptr;
  UnknownSender* spare = &unknown_[0];  // a free row, or else the one silent for longest
  for (UnknownSender& u : unknown_) {
    if (u.used && u.input == input && memcmp(u.mac, mac, 6) == 0) {
      entry = &u;
      break;
    }
    if (spare->used && (!u.used || now - u.heardAt > now - spare->heardAt)) spare = &u;
  }
  if (!entry) {
    entry = spare;
    *entry = UnknownSender();
    entry->used = true;
    memcpy(entry->mac, mac, 6);
    entry->input = input;
  }
  entry->rssi = rssi;
  entry->heardAt = now;
  entry->messages++;
}

// Checking, starting and confirming an alert use no heap, so they work even when memory has run out. Anything else that
// runs out of memory drops only itself: the hub keeps running, and a report left unconfirmed is sent again.
void SensorCamServer::espNowPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  try {
    espNowReport(mac, data, len, rssi);
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
}

void SensorCamServer::espNowReport(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  SensorReport r;
  CamPacketCheck check = camCheckPacket(mac, data, len, cfg_.espNowKey, r);
  if (check == CAM_PACKET_INVALID) return;
  int id = findSlot(mac, r.input);
  if (id == 0) {
    noteUnknown(mac, r.input, rssi);
    return;
  }
  Health& h = health_[id - 1];
  // The report number is never a reason to refuse a correctly signed report: a sensor that restarts its count must
  // not lose an alert. The price is that a recorded report could be played back to make a false alert.
  const char* refusal = check == CAM_PACKET_BAD_SIGNATURE                    ? "wrong signature"
                        : check == CAM_PACKET_UNSIGNED && !cfg_.espNowUnsigned ? "not signed"
                                                                               : nullptr;
  if (refusal) {
    h.refused++;
    h.refusal = refusal;
    return;
  }
  noteSignal(id, rssi);
  // A resend repeats the report a moment later, and the hub ignores it. Only a short while, so a restarted sensor that
  // happens to reuse the last number is still heard.
  uint32_t now = millis_();
  bool resend = h.counterSeen && h.counter == r.counter && h.counterActive == r.active && now - h.counterAt < CAM_RESEND_WINDOW_MS;
  if (!resend) {
    h.counterSeen = true;
    h.counter = r.counter;
    h.counterAt = now;
    h.counterActive = r.active;
    applyReport(id, r, now);
  }
  // Confirmed once it is on storage, so a power cut cannot lose an alert its sensor has stopped resending. Again for a
  // resend, whose sender evidently missed the first confirmation. poll() saves all that arrived together, then sends.
  if (confirmCount_ < MAX_CONFIRMATIONS) {
    uint8_t* slot = confirmations_[confirmCount_++];
    memcpy(slot, mac, 6);
    camConfirmationInto(mac, r, cfg_.espNowKey, slot + 6);
  }
}

// Smoothed, so a single weak packet does not count. Weak after a few, and strong again only with some margin.
void SensorCamServer::noteSignal(int id, int8_t rssi) {
  Health& h = health_[id - 1];
  h.rssi = rssi;
  if (rssi == 0) return;  // not measured
  h.rssiAvg8 = h.rssiCount ? (int16_t)(h.rssiAvg8 + (rssi * 8 - h.rssiAvg8) / 4) : (int16_t)(rssi * 8);
  if (h.rssiCount < 255) h.rssiCount++;
  if (!h.weak && h.rssiCount >= 3 && h.rssiAvg8 < WEAK_SIGNAL_DBM * 8) {
    h.weak = true;
    SensorReport r;
    r.battery = h.battery;
    logEvent(id, r, false, CAM_EVENT_WEAK_SIGNAL);
    notifyHook(id, CAM_EVENT_WEAK_SIGNAL, false);
  } else if (h.weak && h.rssiAvg8 > (WEAK_SIGNAL_DBM + 5) * 8) {
    h.weak = false;
  }
}

// Late alerts play back one at a time so each becomes its own NVR record.
void SensorCamServer::serviceDelayed(uint32_t now) {
  // The flag keeps the comparison valid: an old timestamp alone would look like the future after 24.8 days.
  if (delayedGap_ && (int32_t)(now - nextDelayedAt_) >= 0) delayedGap_ = false;
  CamDelayed d;
  if (sensors_.anyActive() || delayedGap_ || !delayed_.take(d)) return;
  waitingDirty_ = true;
  int id = findSlot(d.mac, d.input);
  if (id) {  // it may have been deleted while waiting
    sensors_.trigger(id, now, d.direction, true, holdFor(id), d.several);
    playingId_ = id;
    playing_ = d;
    startAlert(id, true, now, d.replay);
  }
  nextDelayedAt_ = now + (id ? holdFor(id) : cfg_.holdMs) + CAM_PAST_GAP_MS;
  delayedGap_ = true;
}

std::string SensorCamServer::handleSensor(const std::string& target, int& status) {
  if (!cfg_.sensorToken.empty() && queryParam(target, "token") != cfg_.sensorToken) {
    status = 403;
    return camJsonError("bad token");
  }
  uint8_t mac[6];
  std::string input = queryParam(target, "input");
  int id = camParseMac(queryParam(target, "mac"), mac) ? findSlot(mac, input.empty() ? 1 : (uint8_t)atoi(input.c_str())) : 0;
  std::string state = queryParam(target, "state");
  SensorReport r;
  r.active = !(state == "clear" || state == "off");
  std::string age = queryParam(target, "age");
  r.age = age == "unknown" ? CAM_AGE_UNKNOWN : (uint32_t)strtoul(age.c_str(), nullptr, 10);
  std::string battery = queryParam(target, "battery");
  if (!battery.empty()) r.battery = camBatteryPercent(atoi(battery.c_str()));
  std::string distance = queryParam(target, "distance"), direction = queryParam(target, "direction");
  if (!distance.empty() || !direction.empty()) {
    int d = atoi(direction.c_str());
    r.targetCount = 1;
    if (!distance.empty()) r.targets[0].distanceCm = (uint16_t)atoi(distance.c_str());
    r.targets[0].direction = (uint8_t)(d >= 0 && d < CAM_DIRECTIONS ? d : 0);
  }
  if (!applyReport(id, r, millis_())) {
    status = 404;
    return camJsonError("unknown sensor");
  }
  // The reply is the confirmation, so it waits for storage as an ESP-NOW confirmation does.
  if (!persist()) {
    status = 507;
    return camJsonError("the alert was received but could not be saved; send it again");
  }
  status = 200;
  return JSON_OK;
}

std::string SensorCamServer::statusJson(uint32_t now) {
  int known = 0;
  for (int id = 1; id <= sensors_.count(); id++) known += slots_[id - 1].used;
  return jsonObject([&](CamJson& j) {
    j.num("sensors", known).array("active");
    for (int id = 1; id <= sensors_.count(); id++)
      if (sensors_.active(id)) j.str(slotName(id));
    j.end().num("subscribers", subs_.count()).num("streams", streamCount()).num("delayed_waiting", delayed_.count()).array("heard");
    for (int id = 1; id <= sensors_.count(); id++) {
      const Health& h = health_[id - 1];
      if (!h.heard) continue;
      j.object().str("sensor", slotName(id)).num("last_heard_s", (now - h.heardAt) / 1000).num("battery", h.battery != CAM_BATTERY_UNKNOWN, h.battery).flag("silent", h.silent).end();
    }
    j.end().num("uptime_s", now / 1000);
  });
}

void camCopyText(char* out, size_t size, const std::string& text) { camCopyText(out, size, text.c_str()); }

void camCopyText(char* out, size_t size, const char* text) {
  if (!size) return;
  size_t len = strlen(text);
  size_t n = len < size - 1 ? len : size - 1;
  if (n < len)
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;  // text[n] continues a character, so stop before that character starts
  memcpy(out, text, n);
  out[n] = 0;
}

// One event of the Events page's list.
static void eventJson(std::string& out, const CamEvent& e) {
  CamJson j(out);
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
  bool ageKnown = e.age != CAM_AGE_UNKNOWN;
  bool hub = strcmp(e.sensor, "hub") == 0;
  j.object().str("sensor", e.sensor).str("name", hub ? "" : e.text).str("event", camEventName(e.kind, e.active, e.delayed));
  if (hub) j.str("detail", e.text);
  j.num("kind", e.kind).flag("active", e.active).flag("delayed", e.delayed);
  j.str("received", clockOk, clockOk ? iso(e.received) : std::string());
  j.str("happened", clockOk && ageKnown, clockOk && ageKnown ? iso(e.received - (time_t)e.age) : std::string());
  j.num("age_s", ageKnown, e.age).num("battery", e.battery != CAM_BATTERY_UNKNOWN, e.battery);
  if (e.kind == CAM_EVENT_HUB_STARTED || e.kind == CAM_EVENT_NETWORK_LOST) j.num("off_s", e.offS != CAM_AGE_UNKNOWN, e.offS);
  j.array("targets");
  for (int i = 0; i < e.targetCount; i++)
    j.object().num("distance_cm", e.targets[i].distanceCm != CAM_DISTANCE_UNKNOWN, e.targets[i].distanceCm).num("direction", e.targets[i].direction).end();
  j.end().end();
}

// Measured an event at a time first, then built at its exact size, so it needs one block no larger than itself.
std::string SensorCamServer::eventsJson(bool* whole) {
  int count = log_.count() < CamEventLog::RECENT ? log_.count() : CamEventLog::RECENT;
  size_t sizes[CamEventLog::RECENT];
  std::string one;
  for (int n = 0; n < count; n++) {
    one.clear();
    eventJson(one, log_.recent(n));
    sizes[n] = one.size();
  }
  auto total = [&](int shown) {  // the brackets, the line break and the commas too
    size_t bytes = 3;
    for (int n = 0; n < shown; n++) bytes += sizes[n] + (n ? 1 : 0);
    return bytes;
  };
  int shown = count;
  while (shown > 0 && !roomFor(total(shown))) shown /= 2;  // the newest that fit in the memory there is
  if (whole) *whole = shown == count;
  std::string out;
  out.reserve(total(shown));
  out += '[';
  for (int n = 0; n < shown; n++) {
    if (n) out += ',';
    eventJson(out, log_.recent(n));
  }
  out += "]\n";
  return out;
}

std::string SensorCamServer::eventsTag() const {
  uint32_t h = 2166136261u;  // FNV-1a over the newest event: with the count of events added, it changes with the list
  if (log_.count()) {
    uint8_t r[CAM_EVENT_RECORD];
    camEventEncode(log_.recent(0), r);
    for (uint8_t b : r) h = (h ^ b) * 16777619u;
  }
  char tag[32];
  snprintf(tag, sizeof(tag), "\"%u-%08x\"", (unsigned)log_.added(), (unsigned)h);
  return tag;
}

int SensorCamServer::findSlot(const uint8_t mac[6], uint8_t input) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && slots_[i].input == input && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

std::string SensorCamServer::slotName(int id) const {
  return camMacText(slots_[id - 1].mac) + "/" + std::to_string(slots_[id - 1].input);
}

bool SensorCamServer::sensorFits(const std::string& path) const {
  uint8_t mac[6];
  size_t slash = path.find('/');
  if (slash == std::string::npos || !camParseMac(path.substr(0, slash), mac)) return true;  // not inside a sensor's folder
  if (findSlot(mac, (uint8_t)atoi(path.c_str() + slash + 1))) return true;
  for (const Slot& s : slots_)
    if (!s.used) return true;
  return false;
}

// What Home Assistant is told of the sensors: which there are, and their names.
uint32_t SensorCamServer::sensorsTag() const {
  uint32_t h = 2166136261u;  // FNV-1a
  auto add = [&](const void* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 16777619u;
  };
  for (const Slot& s : slots_) {
    add(&s.used, 1);
    add(s.mac, 6);
    add(&s.input, 1);
    add(s.name.data(), s.name.size() + 1);
  }
  return h;
}

void SensorCamServer::rescan() {
  uint32_t before = sensorsTag();
  bool seen[CAM_MAX_SENSORS] = {false};
  std::vector<CamFsEntry> nodes;
  storage_.list("", nodes);
  for (const CamFsEntry& s : nodes) {
    uint8_t mac[6];
    if (!s.dir || !camParseMac(s.name, mac)) continue;
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    CamNodeSettings node;  // a damaged file means the defaults
    std::string nodeText;
    if (!storage_.readFile(s.name + "/settings.txt", nodeText, 256) || !camParseNodeSettings(nodeText, node)) node = CamNodeSettings();
    for (const CamFsEntry& e : inputs) {
      if (!e.dir || camClassifyPath(s.name + "/" + e.name) != CAM_PATH_SENSOR) continue;
      uint8_t input = (uint8_t)atoi(e.name.c_str());
      int id = findSlot(mac, input);
      for (int i = 0; id == 0 && i < CAM_MAX_SENSORS; i++) {
        if (slots_[i].used) continue;
        slots_[i].used = true;
        memcpy(slots_[i].mac, mac, 6);
        slots_[i].input = input;
        health_[i] = Health();
        health_[i].watchedAt = millis_();
        mqttShown_[i] = MqttShown();
        id = i + 1;
      }
      if (id) {
        seen[id - 1] = true;
        std::string folder = s.name + "/" + e.name, name, text;
        slots_[id - 1].name = storage_.readFile(folder + "/name.txt", name, 64) ? name : std::string();
        CamSensorSettings settings;  // a damaged file means the defaults
        if (!storage_.readFile(folder + "/settings.txt", text, 256) || !camParseSensorSettings(text, settings)) settings = CamSensorSettings();
        slots_[id - 1].settings = settings;
        slots_[id - 1].node = node;
      }
    }
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    if (!slots_[i].used || seen[i]) continue;
    sensors_.clear(i + 1);
    delivery_.forget(i + 1);
    slots_[i] = Slot();
    health_[i] = Health();
    mqttShown_[i] = MqttShown();
  }
  frameKey_.clear();
  if (sensorsTag() != before) mqtt_.discovery = 0;  // a sensor or a name changed, so Home Assistant is told again
}
