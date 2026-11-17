// CamAlarm: sensor reports, node health, what is saved before confirming, the NVR delivery check, and the sensor table.
#include "cam_alarm.h"
#include "cam_number.h"
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

// The parts are the server's, with its defaults for any the board left out.
CamAlarm::CamAlarm(const CamConfig& cfg, CamChip& chip, CamRadio& radio, CamStore& store, CamStorage& storage, const CamNodeImages& images,
                   CamAlarmOutside& outside, CamMemoryFailures& failures, std::function<uint32_t()> millisFn)
    : cfg_(cfg),
      chip_(&chip),
      radio_(&radio),
      store_(&store),
      storage_(storage),
      images_(images),
      outside_(outside),
      millis_(millisFn),
      failures_(failures),
      sensors_(CAM_MAX_SENSORS, cfg.holdMs),
      log_(storage),
      slots_(CAM_MAX_SENSORS),
      nodes_(CAM_MAX_SENSORS),
      health_(CAM_MAX_SENSORS),
      sealNumber_(cfg.sealStart),
      armMode_(cfg.armMode),
      disarmedUntil_(cfg.disarmedUntil),
      healthAlarm_(cfg.healthAlarm),
      rearmMinutes_(cfg.rearmMinutes) {}

void CamAlarm::begin() {
  notGot_.reserve(CAM_MAX_SENSORS);
  rescan();
  uint32_t used = 0, total = 0;
  if (chip_->storageUsage(used, total)) log_.setFileMax(camLogFileMax(total, HISTORY_ROOM));  // a smaller history on small storage
  log_.load();
  loadWaiting();
  std::string alive;
  if (storage_.readFile(ALIVE_FILE, alive, 16) && alive.size() == 8) {  // seconds since 1970, little-endian
    uint64_t t = 0;
    for (int i = 7; i >= 0; i--) t = t << 8 | (uint8_t)alive[i];
    lastAlive_ = (time_t)(int64_t)t;
  }
  beganAt_ = nvrSeenAt_ = perimeterOffAt_ = interiorOffAt_ = millis_();
}

void CamAlarm::tick(uint32_t now) {
  rearmWhenDue(now);  // first, so an alert in this pass already counts
  sensors_.tick(now);
  serviceDelayed(now);
  // Latched, so a long-dead sensor stays silent when the millisecond counter wraps.
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    Health& h = health_[i];
    if (!slots_[i].used || h.silent || now - (h.heard ? h.heardAt : h.watchedAt) <= silentMsFor(i + 1)) continue;
    h.silent = true;
    healthAlert(i + 1, CAM_EVENT_SILENT, now);
  }
  for (Node& n : nodes_)  // a node that stopped asking has given up, or restarted with what it had
    if (n.updating && now - n.updateAt > CAM_UPDATE_IDLE_MS) n.updating = false;
  // Reports that arrived since the last pass are saved together, and only then confirmed.
  if (confirmCount_) {
    bool kept = persist();
    for (int i = 0; kept && radio_->hasEspNow() && i < confirmCount_; i++)
      radio_->espNowSend(confirmations_[i], confirmations_[i] + 7, confirmations_[i][6]);
    confirmCount_ = 0;  // unconfirmed, the sensors resend, and each resend tries the save again
  }
  if (log_.waiting()) log_.save(outside_.downloadingHistory());  // events from anything else: an addition to the log is cheap
  if (waitingDirty_) saveWaiting();                              // small, and it is what a restart would replay
  if (!startNoted_) noteHubStart(now);
  if (startNoted_ && (!aliveWritten_ || now - aliveAt_ >= CAM_ALIVE_EVERY_MS)) writeAlive(now);  // after the start is noted, which reads the old time
  if (cfg_.watchNvr) checkNvr(now);
  checkJamming(now);
}

int CamAlarm::webReport(const uint8_t mac[6], uint8_t input, const SensorReport& r) {
  int id = findSlot(mac, input);
  if (!applyReport(id, r, millis_())) return 404;
  if (r.battery != CAM_BATTERY_UNKNOWN) noteBattery(id, r.battery);
  return persist() ? 200 : 507;
}

int CamAlarm::sensorCount() const {
  int n = 0;
  for (const Slot& s : slots_) n += s.used;
  return n;
}

// Written straight into the event, without using the heap, so an alert is logged even when memory has run out.
void CamAlarm::logEvent(int id, const SensorReport& r, bool delayed, CamEventKind kind) {
  const Slot& s = slotRef(id);
  char sensor[sizeof(CamEvent::sensor)];
  snprintf(sensor, sizeof(sensor), "%02X-%02X-%02X-%02X-%02X-%02X/%u", s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5], (unsigned)s.input);
  addEvent(sensor, s.name.c_str(), r, delayed, kind);
}

// The hub's own events: in the history as sensor "hub", and sent as node problems.
void CamAlarm::hubEvent(CamEventKind kind, const std::string& detail, uint32_t offS, const std::string& message, bool flag) {
  SensorReport r;  // age 0: it happened when it was logged
  r.active = flag;
  addEvent("hub", detail.c_str(), r, false, kind, offS);
  outside_.notifyHubHook(kind, detail, message);
}

void CamAlarm::addEvent(const char* sensor, const char* text, const SensorReport& r, bool delayed, CamEventKind kind, uint32_t offS) {
  CamEvent e;
  e.received = chip_->wallTime();
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
  if (kind == CAM_EVENT_TEST_ALERT && e.received > lastTestAt_) lastTestAt_ = e.received;
  log_.add(e);
}

// Before a planned restart. The alerts waiting for an NVR record are written as they are: one whose delivery has been
// checked since it was saved would otherwise play again after the restart, as a past event.
void CamAlarm::saveNow() {
  log_.save(outside_.downloadingHistory());
  saveWaiting();
  writeAlive(millis_());  // a planned restart then knows exactly when the hub stopped
}

// Called before reports are confirmed, so a confirmed alert survives a power cut. True when everything is on storage.
bool CamAlarm::persist() {
  bool events = log_.save(outside_.downloadingHistory());  // a failed save is tried again, since confirmations wait on it
  if (waitingDirty_ || pendingChanged_ || waitingSaveFailed_) saveWaiting();
  return events && !waitingSaveFailed_;
}

// Alerts that still need an NVR record: the past events waiting their turn, then the alerts whose recording has not yet
// been checked. After a restart they all play back as past events, because the NVR may never have got them.
// waiting.bin: "WAIT", version 1, the count, then 9 bytes each: MAC address, input, direction, and flags: 1 if it stands
// for several sensors, 2 if the NVR missed it and it waits for the NVR to listen again, 4 if it was logged as missed
// already.
void CamAlarm::saveWaiting() {
  std::string out = "WAIT";
  auto add = [&](const uint8_t mac[6], uint8_t input, uint8_t direction, bool several, bool missed, bool replay) {
    out.append((const char*)mac, 6);
    out += (char)input;
    out += (char)direction;
    out += (char)((several ? 1 : 0) | (missed ? 2 : 0) | (replay ? 4 : 0));
  };
  out += (char)1;
  out += (char)0;  // the count, filled in below
  for (int n = 0; n < delayed_.count(); n++)
    add(delayed_.at(n).mac, delayed_.at(n).input, delayed_.at(n).direction, delayed_.at(n).several, false, delayed_.at(n).replay);
  const CamDelayedQueue& missed = delivery_.missed();
  for (int n = 0; n < missed.count(); n++) add(missed.at(n).mac, missed.at(n).input, 0, missed.at(n).several, true, true);
  for (int id = 1; id <= sensors_.count(); id++) {
    const CamPendingCheck& p = delivery_.pending(id);
    if (slotRef(id).used && p.on) add(p.mac, p.input, 0, p.several, false, p.replay);
  }
  out[5] = (char)((out.size() - 6) / 9);
  waitingSaveFailed_ = !storage_.writeFile(WAITING_FILE, out.data(), out.size());
  waitingDirty_ = pendingChanged_ = false;
}

void CamAlarm::loadWaiting() {
  std::string in;
  if (!storage_.readFile(WAITING_FILE, in, 6 + 255 * 9) || in.size() < 6 || in.compare(0, 4, "WAIT") != 0 || in[4] != 1) return;
  size_t count = (uint8_t)in[5];
  if (in.size() != 6 + count * 9) return;
  uint32_t now = millis_();
  for (size_t i = 0; i < count; i++) {
    const uint8_t* p = (const uint8_t*)in.data() + 6 + i * 9;
    int id = findSlot(p, p[6]);
    if (!id) continue;  // deleted since
    if (p[8] & 2)
      delivery_.keepMissed(p, p[6], p[8] & 1, holdFor(id), now);
    else
      delayed_.add(p, p[6], p[7], false, now, holdFor(id), p[8] & 1, (p[8] & 4) != 0);  // old times mean nothing on a new millis() clock
  }
  waitingDirty_ = count > 0;
}

// Rewritten every 10 minutes while the clock is set, so after a power cut the hub can say how long it was off.
void CamAlarm::writeAlive(uint32_t now) {
  aliveAt_ = now;
  aliveWritten_ = true;
  uint64_t t = (uint64_t)(int64_t)chip_->wallTime();
  char out[8];  // seconds since 1970, little-endian
  for (int i = 0; i < 8; i++) out[i] = (char)(t >> (8 * i));
  if ((int64_t)t >= (int64_t)CAM_CLOCK_VALID_AFTER) storage_.writeFile(ALIVE_FILE, out, sizeof(out));
}

// While the hub is off its network, nothing reaches the NVR. Someone may be keeping it off on purpose.
void CamAlarm::networkLost(uint32_t seconds) {
  hubEvent(CAM_EVENT_NETWORK_LOST, "", seconds, camEventMessage(CAM_EVENT_NETWORK_LOST, cfg_.hostName, "", seconds, false));
}

// Logged once the network is up, and once the clock is set or has had long enough to be.
void CamAlarm::noteHubStart(uint32_t now) {
  time_t t = chip_->wallTime();
  bool clockOk = t >= CAM_CLOCK_VALID_AFTER;
  if (startNoted_ || !networkSeen_ || (!clockOk && now - beganAt_ < CAM_START_CLOCK_WAIT_MS)) return;
  startNoted_ = true;
  bool offKnown = clockOk && lastAlive_ >= CAM_CLOCK_VALID_AFTER && t >= lastAlive_ && (int64_t)(t - lastAlive_) < (int64_t)CAM_AGE_UNKNOWN;
  uint32_t off = offKnown ? (uint32_t)(t - lastAlive_) : CAM_AGE_UNKNOWN;
  std::string reason = cfg_.startReason.empty() ? std::string("unknown") : cfg_.startReason;
  hubEvent(CAM_EVENT_HUB_STARTED, reason, off, camEventMessage(CAM_EVENT_HUB_STARTED, cfg_.hostName, reason, off, false), !cfg_.startPlanned);
  // A crash loop otherwise shows only as the hub missing for a moment now and then. The starts are counted from the
  // history, so it needs the clock.
  if (cfg_.startPlanned || !clockOk) return;
  int starts = 0;
  for (int n = 0; n < log_.count(); n++) {
    const CamEvent& e = log_.recent(n);
    starts += e.kind == CAM_EVENT_HUB_STARTED && e.active && e.received >= t - 3600;
  }
  if (starts < RESTART_LOOP_STARTS) return;
  std::string detail = std::to_string(starts) + " times in an hour";
  hubEvent(CAM_EVENT_RESTART_LOOP, detail, CAM_AGE_UNKNOWN, camEventMessage(CAM_EVENT_RESTART_LOOP, cfg_.hostName, detail, CAM_AGE_UNKNOWN, false));
}

#ifdef CAM_TEST_HOOKS
void CamAlarm::setNvrGoneMs(uint32_t ms) {
  nvrGoneMs_ = ms;
  nvrSeenAt_ = millis_();
  nvrGone_ = false;
}
#endif

// A pending check is saved with the report that started its alert, not on its own, so an alert costs one write. One
// cleared since is still in the file; after a power cut it replays, which is a false alert rather than a missed one.
void CamAlarm::startAlert(int id, bool delayed, uint32_t now, bool replay) {
  if (!cfg_.watchNvr) return;  // nothing would check it
  delivery_.started(id, slotRef(id).mac, slotRef(id).input, delayed, sensors_.several(id), replay, holdFor(id), now);
  pendingChanged_ = true;
}

// See CamDeliveryWatch. One the NVR did not get is logged and sent, because nothing else would show it is missing.
void CamAlarm::checkNvr(uint32_t now) {
  bool listening = outside_.nvrListening(now);
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
  delivery_.check(now, listening, [&](int id) { return slotRef(id).used && sensors_.active(id); }, notGot_);
  for (const CamDeliveryWatch::Missed& m : notGot_) {
    waitingDirty_ = true;
    if (!m.log || !slotRef(m.id).used) continue;  // a replay that failed again waits again, without a second entry
    SensorReport r;
    r.battery = healthRef(m.id).battery;
    r.active = true;
    logEvent(m.id, r, m.delayed, CAM_EVENT_NOT_DELIVERED);
    outside_.notifyHook(m.id, CAM_EVENT_NOT_DELIVERED, m.delayed);
  }
}

void CamAlarm::healthAlert(int id, CamEventKind kind, uint32_t now) {
  SensorReport r;
  r.battery = healthRef(id).battery;
  logEvent(id, r, false, kind);
  outside_.notifyHook(id, kind, false);
  if (!healthAlarm_) return;
  interruptPastEvent(now);
  sensors_.trigger(id, now, 0, false, holdFor(id));
}

uint32_t CamAlarm::holdFor(int id) const {
  uint32_t seconds = slotRef(id).settings.holdS;
  return seconds ? seconds * 1000 : cfg_.holdMs;
}

bool CamAlarm::quietNow(int id) const { return camQuiet(slotRef(id).settings, chip_->localMinute()); }

// present: the report shows the sensor is there now. One that may have been recorded and played back still raises its
// alert, as a false alert is better than a missed one, but says nothing of the node being there or of its battery.
bool CamAlarm::applyReport(int id, const SensorReport& r, uint32_t now, bool present) {
  if (id < 1 || id > sensors_.count()) return false;
  Health& h = healthRef(id);
  if (present) {
    h.heard = true;
    h.heardAt = now;
    h.silent = false;
  }
  bool batteryWentLow = false;
  if (present && r.battery != CAM_BATTERY_UNKNOWN) {  // a report without a reading must not erase the last one
    h.battery = r.battery;
    if (r.battery <= LOW_BATTERY_PERCENT && !h.lowBattery) {
      h.lowBattery = true;
      batteryWentLow = true;
    } else if (r.battery >= LOW_BATTERY_PERCENT + 5) {  // the gap stops a reading near the limit from alerting again and again
      h.lowBattery = false;
    }
  }

  uint32_t lateAfter = nodeRef(id).settings.lateS ? nodeRef(id).settings.lateS : CAM_PAST_AFTER_S;
  bool late = r.age > lateAfter;  // an unknown age counts as late
  bool wasActive = sensors_.active(id);
  bool wasReported = h.reported;
  h.reported = r.active;
  bool quiet = r.active && quietNow(id);                                               // logged, but the NVR is not told
  CamEventKind heldKind = r.active && !quiet ? heldAs(id, r, now) : CAM_EVENT_REPORT;  // the same, for the arm mode
  bool disarmed = heldKind != CAM_EVENT_REPORT;
  bool held = quiet || disarmed;
  if (r.active && late && !held) {
    queueDelayed(id, r, now);
  } else if (r.active && !held) {
    interruptPastEvent(now);
    // Starts the alert, or extends it, so activations closer together than the hold make one NVR record.
    sensors_.trigger(id, now, reportDirection(r), false, holdFor(id));
    if (!wasActive) startAlert(id, false, now);
  }
  // A clear report does not end the alert early, for the same reason: the alert ends when its hold runs out.
  // Repeats and heartbeats that change nothing would push real events out of the log.
  // An activation is logged unless it only repeats one whose alert is still running; a sensor that never reports clear
  // still has each new alert logged.
  if (late || (r.active ? !wasReported || !wasActive : wasReported))
    logEvent(id, r, late, quiet ? CAM_EVENT_QUIET_ALERT : disarmed ? heldKind : CAM_EVENT_REPORT);
  if (r.active && !held && (late || !wasActive)) outside_.notifyHook(id, CAM_EVENT_REPORT, late);
  if (batteryWentLow) healthAlert(id, CAM_EVENT_LOW_BATTERY, now);
  // Each activation, as logged above, counts; held ones too, since the sensor fired all the same.
  if (r.active && (late || !wasReported || !wasActive) && h.busy.note(now, busyAlerts_)) {
    logEvent(id, r, false, CAM_EVENT_BUSY_SENSOR);
    outside_.notifyHook(id, CAM_EVENT_BUSY_SENSOR, false);
  }
  return true;
}

// A live alert must not share its NVR record with a past event, which would weave a stale picture into the recording of
// something happening now. The past event stops and goes back in the queue, to play in full once the live one is over.
void CamAlarm::interruptPastEvent(uint32_t now) {
  int id = playingId_;
  playingId_ = 0;
  if (!id || !sensors_.pastEvent(id)) return;
  sensors_.clear(id);
  delivery_.forget(id);  // it never finished, so it is not checked; it plays again instead
  delayed_.add(playing_.mac, playing_.input, playing_.direction, false, now, holdFor(id), playing_.several, playing_.replay);
  waitingDirty_ = true;
}

// A late alert waits for its own NVR record; CamDelayedQueue decides which alerts share one.
void CamAlarm::queueDelayed(int id, const SensorReport& r, uint32_t now) {
  const Slot& s = slotRef(id);
  bool known = r.age != CAM_AGE_UNKNOWN && r.age < 1000000;  // the millisecond clock covers 49 days
  delayed_.add(s.mac, s.input, reportDirection(r), known, known ? now - r.age * 1000 : now, holdFor(id));
  waitingDirty_ = true;
}

#ifdef CAM_TEST_HOOKS
void CamAlarm::endAlerts() {
  for (int id = 1; id <= sensors_.count(); id++) sensors_.clear(id);
}
#endif

// Keeps the most recent senders, so a new sensor can be seen on the page before it has been added.
void CamAlarm::noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi) {
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

void CamAlarm::espNowReport(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi, bool sealed) {
  if (!sealed && len >= 1 && data[0] == CAM_SEALED) {
    sealedPacket(mac, data, len, rssi);
    return;
  }
  if (len >= 1 && data[0] == CAM_NODE_INFO) {
    nodeInfoPacket(mac, data, len);
    return;
  }
  SensorReport r;
  if (!parseSensorPacket(data, len, r)) return;
  // A paired node signs with its own key, and only that key is accepted from it; the others share the ESP-NOW key.
  int id = findSlot(mac, r.input);
  Node* node = id ? &nodeRef(id) : nullptr;
  const std::string& key = keyFor(node);
  CamPacketCheck check = camCheckPacket(mac, data, len, key, r);
  if (check == CAM_PACKET_INVALID) return;
  if (id == 0) {
    noteUnknown(mac, r.input, rssi);
    return;
  }
  Health& h = healthRef(id);
  // The report number is never a reason to refuse a correctly signed report: a sensor that restarts its count must
  // not lose an alert.
  if (check == CAM_PACKET_BAD_SIGNATURE) return refuseSignature(id);
  noteSignal(id, rssi);
  node->sealed = sealed;
  // A report recorded and played back would otherwise keep a stolen node looking alive. A paired node echoes the number
  // from the hub's last confirmation; one that does not is asked to prove it is there, with a new number. Nodes on the
  // shared key are taken at their word, as they have no such number.
  bool present = !node->paired || (r.fresh && (r.fresh == node->fresh || r.fresh == node->freshBefore));
  // A resend repeats the report a moment later, and the hub ignores it. Only a short while, so a restarted sensor that
  // happens to reuse the last number is still heard.
  uint32_t now = millis_();
  bool resend = h.counterSeen && h.counter == r.counter && h.counterActive == r.active && now - h.counterAt < CAM_RESEND_WINDOW_MS;
  if (!resend) {
    h.counterSeen = true;
    h.counter = r.counter;
    h.counterAt = now;
    h.counterActive = r.active;
    applyReport(id, r, now, present);
    if (node->paired) {  // a new number for the next report; the one before stays good, for a confirmation that was lost
      node->freshBefore = node->fresh;
      node->fresh = freshNumber();
    }
  }
  // Confirmed once it is on storage, so a power cut cannot lose an alert its sensor has stopped resending. Again for a
  // resend, whose sender evidently missed the first confirmation. poll() saves all that arrived together, then sends.
  // A sealed report is confirmed sealed, as the confirmation repeats what it said.
  if (confirmCount_ < MAX_CONFIRMATIONS) {
    uint8_t* slot = confirmations_[confirmCount_++];
    memcpy(slot, mac, 6);
    CamForNode extra = forNode(id);
    extra.prove = node->paired && !present;
    if (sealed) {
      uint8_t body[CAM_CONFIRMATION_BODY];
      camConfirmationBody(r, extra, body);
      slot[6] = (uint8_t)camSeal(mac, sealNumber_++, body, sizeof(body), key, true, slot + 7);
    } else {
      slot[6] = CAM_CONFIRMATION_LEN;
      camConfirmationInto(mac, r, extra, key, slot + 7);
    }
  }
  // After the confirmation is queued, as this may need memory, and an alert must not wait on it.
  if (!resend && present && r.battery != CAM_BATTERY_UNKNOWN) noteBattery(id, r.battery);
}

bool CamAlarm::setWalkTest(const uint8_t mac[6], uint32_t until) {
  Node* node = nodeByMac(mac);
  if (node) node->walkUntil = until;
  return node != nullptr;
}

void CamAlarm::updateAsked(int id, uint32_t size) {
  Node& node = nodeRef(id);
  node.updating = true;
  node.updateAt = millis_();
  node.updateSize = size;
}

void CamAlarm::updateReached(int id, uint32_t offset) { nodeRef(id).updateOffset = offset; }

// Counted, and shown on the Nodes and pairing page: a node that keeps failing has a wrong key, or someone is trying keys.
void CamAlarm::refuseSignature(int id) {
  healthRef(id).refused++;
  healthRef(id).refusal = "wrong signature";
}

// Random numbers without a source of them on every build: the hub's seal numbers, which start at random, mixed.
uint32_t CamAlarm::freshNumber() {
  uint32_t x = sealNumber_++ * 2654435761u ^ millis_() ^ 0x9E3779B9u;
  x ^= x >> 16;
  x *= 0x85EBCA6Bu;
  x ^= x >> 13;
  return x ? x : 1;  // 0 means none
}

CamAlarm::Node* CamAlarm::nodeByMac(const uint8_t mac[6]) {
  for (Node& n : nodes_)
    if (n.used && memcmp(n.mac, mac, 6) == 0) return &n;
  return nullptr;
}

// Only a node the hub knows can be opened, with its key; inside is a report or node info, handled as if sent plain.
void CamAlarm::sealedPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi) {
  int id = findNode(mac);
  if (!id) return;
  uint8_t inner[CAM_SEALED_MAX_INNER + CAM_SIGNATURE_LEN];
  int n = camOpenSealed(mac, data, len, keyFor(&nodeRef(id)), false, inner, sizeof(inner));
  if (n < 0) return refuseSignature(id);
  if (n == 0 || (inner[0] != CAM_REPORT && inner[0] != CAM_NODE_INFO)) return;
  espNowReport(mac, inner, n, rssi, true);
}

// What a node gets with each confirmation: the hub's time, its settings from the page, a walk test, and whether a
// firmware update is ready. No heap, as it is part of confirming an alert.
CamForNode CamAlarm::forNode(int id) const {
  const Slot& s = slotRef(id);
  const Node& node = nodeRef(id);
  CamForNode e;
  time_t t = chip_->wallTime();
  if (t >= CAM_CLOCK_VALID_AFTER) e.hubTime = (uint32_t)t;
  e.everyS = node.settings.everyS;
  // Repeats must come more often than the alert lasts, or the alert would lapse between them: below the shortest hold
  // of the node's sensors.
  uint32_t repeat = node.settings.repeatS;
  for (int i = 0; repeat && i < CAM_MAX_SENSORS; i++) {
    if (!slots_[i].used || memcmp(slots_[i].mac, s.mac, 6) != 0) continue;
    uint32_t hold = holdFor(i + 1) / 1000;
    if (repeat >= hold) repeat = hold > 1 ? hold - 1 : 1;
  }
  e.repeatS = (uint16_t)repeat;
  e.walkUntil = e.hubTime && node.walkUntil > e.hubTime ? node.walkUntil : 0;
  e.update = updateReady(id);
  e.fresh = node.fresh;
  return e;
}

// Offered while the node runs another version, unless it already tried this one and had it undone, or is saving its
// battery: an update keeps it awake for a minute or so.
bool CamAlarm::updateReady(int id) const {
  const CamNodeInfo& info = nodeRef(id).info;
  const CamNodeImage* f = images_.forChip(info.chip);
  return f && !info.version.empty() && info.version != f->version && info.refused != f->number && !info.saving;
}

// A reading every few hours goes to battery.txt in the node's folder, and from those comes how long the battery will last.
// Needs the clock. Anything that runs out of memory here only skips this reading.
void CamAlarm::noteBattery(int id, uint8_t percent) {
  time_t t = chip_->wallTime();
  Node& node = nodeRef(id);
  if (t < CAM_CLOCK_VALID_AFTER || (node.batteryAt && t >= node.batteryAt && t - node.batteryAt < CAM_BATTERY_POINT_EVERY_S)) return;
  try {
    std::string path = camMacText(node.mac) + "/battery.txt", text;
    std::vector<CamBatteryPoint> points;
    if (!storage_.readFile(path, text, 8192) || !camParseBatteryLog(text, points)) points.clear();
    bool added = camAddBatteryPoint(points, t, percent);
    if (added) {
      text = camBatteryLogText(points);
      storage_.writeFile(path, text.data(), text.size());
    }
    int days = camBatteryDaysLeft(points);
    node.batteryAt = points.empty() ? t : points.back().t;
    node.daysLeft = days;
    int first = findNode(node.mac);  // logged once for the node, on its first sensor
    if (days >= 0 && days < 7 && !node.runningOut) {
      node.runningOut = true;
      SensorReport r;
      r.battery = percent;
      logEvent(first, r, false, CAM_EVENT_BATTERY_RUNNING_OUT);
      outside_.notifyHook(first, CAM_EVENT_BATTERY_RUNNING_OUT, false);
    } else if (days < 0 || days > 14) {
      node.runningOut = false;
    }
  } catch (const std::bad_alloc&) {
    failures_.note();
  }
}

// Kept on storage only when it changes, so the silent time it sets survives a restart of the hub. A restart for a fault,
// such as a brownout from a weak battery or a bad wire, is logged, as it shows a node failing before it goes silent.
void CamAlarm::nodeInfoPacket(const uint8_t mac[6], const uint8_t* data, int len) {
  int id = findNode(mac);
  if (!id) return;
  CamNodeInfo info;
  Node& node = nodeRef(id);
  CamPacketCheck check = camCheckNodeInfo(mac, data, len, keyFor(&node), info);
  if (check == CAM_PACKET_INVALID) return;
  if (check == CAM_PACKET_BAD_SIGNATURE) return refuseSignature(id);
  CamNodeInfo old = node.info;
  if (info.responseMs == CAM_RESPONSE_UNKNOWN) info.responseMs = old.responseMs;
  node.info = info;
  // Now running the image it was fetching: the update is over.
  if (node.updating && images_.forChip(info.chip) && images_.forChip(info.chip)->version == info.version) node.updating = false;
  if (info == old) return;
  std::string text = camNodeInfoText(info);
  storage_.writeFile(camMacText(mac) + "/about.txt", text.data(), text.size());
  bool known = !old.version.empty();  // otherwise this is the first the hub hears, and its restarts are old news
  if (known && info.restarts != old.restarts && camRestartIsFault(info.restart)) {
    SensorReport r;
    r.battery = healthRef(id).battery;
    logEvent(id, r, false, CAM_EVENT_NODE_FAULT);
    outside_.notifyHook(id, CAM_EVENT_NODE_FAULT, false);
  }
}

// See CamJamWatch. Only nodes that say how often they report, or are told to, that have reported since the hub started,
// and that are not silent already, which a node gone for longer is.
void CamAlarm::checkJamming(uint32_t now) {
  int count = (int)nodes_.size() < CAM_MAX_SENSORS ? (int)nodes_.size() : CAM_MAX_SENSORS;
  bool heard[CAM_MAX_SENSORS] = {false}, silent[CAM_MAX_SENSORS], overdue[CAM_MAX_SENSORS] = {false};
  uint32_t last[CAM_MAX_SENSORS] = {0};
  int first[CAM_MAX_SENSORS] = {0};  // the node's first sensor, for an NVR alarm
  for (bool& s : silent) s = true;
  for (int id = 1; id <= CAM_MAX_SENSORS; id++) {
    const Slot& s = slotRef(id);
    int n = s.node - 1;
    if (!s.used || n < 0 || n >= count) continue;
    const Health& h = healthRef(id);
    if (!first[n]) first[n] = id;
    silent[n] = silent[n] && h.silent;
    if (h.heard && (!heard[n] || (int32_t)(h.heardAt - last[n]) > 0)) last[n] = h.heardAt;
    heard[n] = heard[n] || h.heard;
  }
  for (int n = 0; n < count; n++) {
    const Node& node = nodes_[n];
    uint32_t every = node.info.everyS ? node.info.everyS : node.settings.everyS;
    uint32_t late = overdueMs_ ? overdueMs_ : camOverdueMs(every);
    overdue[n] = node.used && every && first[n] && heard[n] && !silent[n] && now - last[n] > late;
  }
  int together = jam_.update(overdue, count, now);
  if (!together) return;
  std::string detail = std::to_string(together) + " nodes";
  hubEvent(CAM_EVENT_JAMMING, detail, CAM_AGE_UNKNOWN, camEventMessage(CAM_EVENT_JAMMING, cfg_.hostName, detail, CAM_AGE_UNKNOWN, false));
  if (!healthAlarm_) return;  // as for a silent node: the NVR records it as an alert from each of them
  interruptPastEvent(now);
  for (int n = 0; n < count; n++)
    if (jam_.together(n) && first[n]) sensors_.trigger(first[n], now, 0, false, holdFor(first[n]));
}

// Smoothed, so a single weak packet does not count. Weak after a few, and strong again only with some margin.
void CamAlarm::noteSignal(int id, int8_t rssi) {
  Health& h = healthRef(id);
  h.rssi = rssi;
  if (rssi == 0) return;  // not measured
  h.rssiAvg8 = h.rssiCount ? (int16_t)(h.rssiAvg8 + (rssi * 8 - h.rssiAvg8) / 4) : (int16_t)(rssi * 8);
  if (h.rssiCount < 255) h.rssiCount++;
  if (!h.weak && h.rssiCount >= 3 && h.rssiAvg8 < WEAK_SIGNAL_DBM * 8) {
    h.weak = true;
    SensorReport r;
    r.battery = h.battery;
    logEvent(id, r, false, CAM_EVENT_WEAK_SIGNAL);
    outside_.notifyHook(id, CAM_EVENT_WEAK_SIGNAL, false);
  } else if (h.weak && h.rssiAvg8 > (WEAK_SIGNAL_DBM + 5) * 8) {
    h.weak = false;
  }
}

// Late alerts play back one at a time so each becomes its own NVR record.
void CamAlarm::serviceDelayed(uint32_t now) {
  // The flag keeps the comparison valid: an old timestamp alone would look like the future after 24.8 days.
  if (delayedGap_ && (int32_t)(now - nextDelayedAt_) >= 0) delayedGap_ = false;
  CamDelayed d;
  // Played to an NVR that is not listening, one would only be logged as missed and wait again: after a power cut that
  // is every one, as the hub is back before the NVR reconnects.
  if (cfg_.watchNvr && !outside_.nvrListening(now)) return;
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

int CamAlarm::findNode(const uint8_t mac[6]) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

int CamAlarm::findSlot(const uint8_t mac[6], uint8_t input) const {
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (slots_[i].used && slots_[i].input == input && memcmp(slots_[i].mac, mac, 6) == 0) return i + 1;
  return 0;
}

// The node's own setting, or else worked out from how often the node says it reports, or else the hub's default.
uint32_t CamAlarm::silentMsFor(int id) const {
  const Node& n = nodeRef(id);
  uint32_t auto_ = camAutoSilentS(n.info.everyS);
  return n.settings.silentS ? n.settings.silentS * 1000 : auto_ ? auto_ * 1000 : cfg_.silentMs;
}

// The input number in a path such as AA-BB-CC-DD-EE-01/2 or AA-BB-CC-DD-EE-01/2/file, slash being the first '/'.
static bool pathInput(const std::string& path, size_t slash, uint8_t& input) {
  return camNumber(path.substr(slash + 1, path.find('/', slash + 1) - slash - 1), input);
}

int CamAlarm::slotForPath(const std::string& path) const {
  uint8_t mac[6], input = 0;
  size_t slash = path.find('/');
  if (camClassifyPath(path) != CAM_PATH_SENSOR || !camParseMac(path.substr(0, slash), mac) || !pathInput(path, slash, input)) return 0;
  return findSlot(mac, input);
}

std::string CamAlarm::slotName(int id) const { return camMacText(slotRef(id).mac) + "/" + std::to_string(slotRef(id).input); }

bool CamAlarm::sensorFits(const std::string& path) const {
  uint8_t mac[6];
  size_t slash = path.find('/');
  if (slash == std::string::npos || !camParseMac(path.substr(0, slash), mac)) return true;  // not inside a sensor's folder
  uint8_t input = 0;
  if (pathInput(path, slash, input) && findSlot(mac, input)) return true;
  for (const Slot& s : slots_)
    if (!s.used) return true;
  return false;
}

// What Home Assistant is told of the sensors: which there are, and their names.
uint32_t CamAlarm::sensorsTag() const {
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

void CamAlarm::rescan() {
  uint32_t before = sensorsTag();
  bool seen[CAM_MAX_SENSORS] = {false}, nodeSeen[CAM_MAX_SENSORS] = {false};
  // A folder that cannot be read changes nothing: forgetting its sensors would ignore their alerts as coming from
  // unknown senders until the next change. A deleted folder is still noticed, as it is simply not listed.
  std::vector<CamFsEntry> folders;
  if (!storage_.list("", folders)) return;
  for (const CamFsEntry& s : folders) {
    uint8_t mac[6];
    if (!s.dir || !camParseMac(s.name, mac)) continue;
    // The node's entry, kept across rescans for what is only in memory; or a free one.
    Node* node = nodeByMac(mac);
    for (int i = 0; !node && i < CAM_MAX_SENSORS; i++) {
      if (nodes_[i].used) continue;
      nodes_[i] = Node();
      nodes_[i].used = true;
      memcpy(nodes_[i].mac, mac, 6);
      node = &nodes_[i];
    }
    if (!node) continue;  // more nodes than there can be sensors: none of its sensors could have a place
    int index = (int)(node - nodes_.data());
    nodeSeen[index] = true;
    std::vector<CamFsEntry> inputs;
    if (!storage_.list(s.name, inputs)) {
      for (int i = 0; i < CAM_MAX_SENSORS; i++) seen[i] = seen[i] || (slots_[i].used && memcmp(slots_[i].mac, mac, 6) == 0);
      continue;
    }
    std::string text;
    CamNodeSettings settings;  // a damaged file means the defaults
    if (!storage_.readFile(s.name + "/settings.txt", text, 256) || !camParseNodeSettings(text, settings)) settings = CamNodeSettings();
    node->settings = settings;
    CamNodeInfo info;  // likewise: a damaged file is as if the node had said nothing
    if (!storage_.readFile(s.name + "/about.txt", text, 256) || !camParseNodeInfo(text, info)) info = CamNodeInfo();
    info.responseMs = node->info.responseMs;  // only in memory
    node->info = info;
    std::vector<CamBatteryPoint> battery;  // and a damaged battery log is no forecast
    if (!storage_.readFile(s.name + "/battery.txt", text, 8192) || !camParseBatteryLog(text, battery)) battery.clear();
    node->batteryAt = battery.empty() ? 0 : battery.back().t;
    node->daysLeft = camBatteryDaysLeft(battery);
    // A damaged pairing file leaves the node unpaired, so its reports are checked with the shared key and refused: it
    // shows on the Nodes and pairing page as refused, rather than being trusted with a key that may not be its own.
    uint8_t nodeKey[CAM_NODE_KEY_LEN];
    node->paired = storage_.readFile(s.name + "/pairing.txt", text, 256) && camParsePairing(text, node->publicKey, nodeKey);
    node->key = node->paired ? std::string((const char*)nodeKey, CAM_NODE_KEY_LEN) : std::string();
    for (const CamFsEntry& e : inputs) {
      if (!e.dir || camClassifyPath(s.name + "/" + e.name) != CAM_PATH_SENSOR) continue;
      uint8_t input = 0;
      camNumber(e.name, input);  // a sensor folder's name is always 1 to 255
      int id = findSlot(mac, input);
      for (int i = 0; id == 0 && i < CAM_MAX_SENSORS; i++) {
        if (slots_[i].used) continue;
        slots_[i].used = true;
        memcpy(slots_[i].mac, mac, 6);
        slots_[i].input = input;
        health_[i] = Health();
        health_[i].watchedAt = millis_();
        outside_.sensorReset(i + 1);
        id = i + 1;
      }
      if (!id) continue;
      seen[id - 1] = true;
      std::string folder = s.name + "/" + e.name, name;
      slotRef(id).name = storage_.readFile(folder + "/name.txt", name, 64) ? name : std::string();
      CamSensorSettings sensor;  // a damaged file means the defaults
      if (!storage_.readFile(folder + "/settings.txt", text, 256) || !camParseSensorSettings(text, sensor)) sensor = CamSensorSettings();
      slotRef(id).settings = sensor;
      slotRef(id).node = index + 1;
    }
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++) {
    if (nodes_[i].used && !nodeSeen[i]) nodes_[i] = Node();
    if (!slots_[i].used || seen[i]) continue;
    sensors_.clear(i + 1);
    delivery_.forget(i + 1);
    slots_[i] = Slot();
    health_[i] = Health();
    outside_.sensorReset(i + 1);
  }
  outside_.sensorsRescanned(sensorsTag() != before);  // a sensor or a name changed, so Home Assistant is told again
}
