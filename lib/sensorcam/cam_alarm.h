// The alarm: the sensor table, what each sensor reports and how it is doing, arming, the event history, the alerts
// waiting for an NVR record, and the check that each alert reached the NVR. It decides; SensorCamServer owns one and
// connects it to the network: the NVR, the web pages, the webhook and Home Assistant.
#pragma once
#include <stdint.h>
#include <time.h>
#include <functional>
#include <string>
#include <vector>
#include "cam_config.h"
#include "cam_delivery.h"
#include "cam_events.h"
#include "cam_node_images.h"
#include "cam_pairing.h"
#include "cam_sensor_table.h"
#include "cam_sensors.h"
#include "cam_storage.h"
#include "cam_timing.h"

// What the alarm needs from the rest of the hub.
class CamAlarmOutside {
 public:
  virtual ~CamAlarmOutside() = default;
  virtual bool nvrListening(uint32_t now) const = 0;                     // whether an alert started now would reach the NVR
  virtual bool downloadingHistory() const = 0;                           // the history is being sent, so its files must not be rotated
  virtual void notifyHook(int id, CamEventKind kind, bool delayed) = 0;  // a sensor's event, for the webhook
  virtual void notifyHubHook(CamEventKind kind, const std::string& detail, const std::string& message) = 0;
  virtual void sensorReset(int id) = 0;                 // sensor id is new or gone, so what was shown of it is forgotten
  virtual void sensorsRescanned(bool listChanged) = 0;  // the table was rebuilt; listChanged: a sensor added, deleted or renamed
};

class CamAlarm {
 public:
  using Health = CamSensorTable::Health;
  using Node = CamSensorTable::Node;
  using Slot = CamSensorTable::Slot;

  // cfg holds the start-up settings, which never change; what the pages change later is the part's own. It and the parts outlive the alarm.
  CamAlarm(const CamConfig& cfg, CamChip& chip, CamRadio& radio, CamStore& store, CamStorage& storage, CamSensorTable& table,
           const CamNodeImages& images, CamAlarmOutside& outside, CamMemoryFailures& failures, std::function<uint32_t()> millisFn);
  void begin();             // reads the sensor table, the history and the alerts that were waiting
  void tick(uint32_t now);  // alerts, health checks, saving and confirming: once a pass, before serving the network
  void networkUp() { networkSeen_ = true; }
  void networkLost(uint32_t seconds);  // on rejoining Wi-Fi after being off it, without a restart
  void saveNow();                      // writes any events not yet saved; before a restart
  // A report, node info, or a sealed one of those, from ESP-NOW; anything else is left alone. From the task that calls tick().
  void espNowReport(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi, bool sealed = false);
  // A report over HTTP, whose reply is its confirmation: 200 once it is on storage, 404 for no such sensor, 507 when it
  // could not be saved.
  int webReport(const uint8_t mac[6], uint8_t input, const SensorReport& r);

  // ---- arming: cam_alarm_arming.cpp ----
  bool armed() const { return armMode_ != CAM_DISARMED; }
  CamArmMode armMode() const { return armMode_; }
  int64_t disarmedUntil() const { return disarmedUntil_; }  // see camTimedDisarmOver; 0 for a disarm without an end
  // Arms away or home, or disarms for minutes (0 until armed again), saved first; from says where, for the history.
  // False when it could not be saved.
  bool setArmMode(CamArmMode mode, const std::string& from, uint32_t minutes = 0);
  // An alert from sensor id that takes the whole path, whatever the arm mode, and is logged as a test. False for no such sensor.
  bool testAlert(int id);
  long long disarmSecondsLeft(uint32_t now) const;   // -1 when not known
  time_t lastTestAt() const { return lastTestAt_; }  // the newest test alert, kept after it leaves the events in memory

  // ---- the sensors, as CamSensorTable holds them. A sensor's id runs from 1, as 0 means none. ----
  void rescan();                   // reads the table again from the folders on storage, and lets go of sensors gone
  bool updateReady(int id) const;  // a firmware update waits for this sensor's node
  void refuseSignature(int id);    // a message from this sensor's node had the wrong signature
  // A walk test on the node, until that time in seconds since 1970, or 0 to stop it. False for a node without a folder.
  bool setWalkTest(const uint8_t mac[6], uint32_t until);
  void updateAsked(int id, uint32_t size);      // sensor id's node asked for a part of a firmware image this size
  void updateReached(int id, uint32_t offset);  // and has got this far

  // ---- settings the pages change while the hub runs ----
  bool healthAlarm() const { return healthAlarm_; }  // a silent node or low battery also raises an NVR alarm
  void setHealthAlarm(bool on) { healthAlarm_ = on; }
  uint32_t rearmMinutes() const { return rearmMinutes_; }  // how long a disarm from Home Assistant lasts; 0 until armed again
  void setRearmMinutes(uint32_t minutes) { rearmMinutes_ = minutes; }

  const SensorBoard& board() const { return sensors_; }  // which alerts are on, for the NVR and the picture
  CamEventLog& log() { return log_; }
  const CamEventLog& log() const { return log_; }
  int delayedCount() const { return delayed_.count(); }  // past events waiting their turn

#ifdef CAM_TEST_HOOKS  // only in the PC builds, for the tests
  void endAlerts();
  void setNvrGoneMs(uint32_t ms);
  void setMinuteMs(uint32_t ms) { minuteMs_ = ms; }
  void setBusyAlerts(int n) { busyAlerts_ = n; }
  void setOverdueMs(uint32_t ms) { overdueMs_ = ms; }
#endif

 private:
  uint32_t holdFor(int id) const;
  bool applyReport(int id, const SensorReport& report, uint32_t now, bool present = true);
  void logEvent(int id, const SensorReport& report, bool delayed, CamEventKind kind = CAM_EVENT_REPORT);
  void addEvent(const char* sensor, const char* text, const SensorReport& report, bool delayed, CamEventKind kind, uint32_t offS = CAM_AGE_UNKNOWN);
  void hubEvent(CamEventKind kind, const std::string& detail, uint32_t offS, const std::string& message, bool flag = false);
  void checkNvr(uint32_t now);
  void noteHubStart(uint32_t now);
  static const int RESTART_LOOP_STARTS = 3;                                  // unplanned starts within an hour that count as restarting often
  void startAlert(int id, bool delayed, uint32_t now, bool replay = false);  // an alert that is a new NVR record, to be checked as delivered
  void healthAlert(int id, CamEventKind kind, uint32_t now);
  bool persist();
  void saveWaiting();
  void loadWaiting();
  void writeAlive(uint32_t now);
  void serviceDelayed(uint32_t now);
  void noteSignal(int id, int8_t rssi);
  void checkJamming(uint32_t now);
  void queueDelayed(int id, const SensorReport& r, uint32_t now);
  void interruptPastEvent(uint32_t now);
  bool quietNow(int id) const;
  // How an alert is logged when the arm mode holds it back from the NVR, or CAM_EVENT_REPORT when it counts.
  CamEventKind heldAs(int id, const SensorReport& r, uint32_t now) const;
  void rearmWhenDue(uint32_t now);
  void nodeInfoPacket(const uint8_t mac[6], const uint8_t* data, int len);
  void sealedPacket(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi);
  CamForNode forNode(int id) const;  // what goes to a node with each confirmation
  void noteBattery(int id, uint8_t percent);
  uint32_t freshNumber();

  const CamConfig& cfg_;
  CamChip* chip_;
  CamRadio* radio_;
  CamStore* store_;
  CamStorage& storage_;
  CamSensorTable& table_;  // the alarm is the one part that changes it
  const CamNodeImages& images_;
  CamAlarmOutside& outside_;
  std::function<uint32_t()> millis_;
  CamMemoryFailures& failures_;

  SensorBoard sensors_;
  CamEventLog log_;

  // Waiting to be sent once the reports are saved: 6 bytes of MAC address, then the message. A fixed table, so an alert is
  // confirmed even when memory has run out.
  static const int MAX_CONFIRMATIONS = 32;
  uint8_t confirmations_[MAX_CONFIRMATIONS][7 + CAM_SEALED_EXTRA + CAM_CONFIRMATION_LEN];  // MAC, length, message
  int confirmCount_ = 0;
  uint32_t sealNumber_ = 0;  // for the hub's sealed messages; set at random at the start

  std::vector<CamDeliveryWatch::Missed> notGot_;  // reserved at the start, so checking deliveries never asks for memory
  bool waitingDirty_ = false;                     // the alerts waiting for an NVR record changed since they were saved
  bool waitingSaveFailed_ = false;
  CamDelayedQueue delayed_;
  int playingId_ = 0;  // the sensor whose past event is playing, and that past event
  CamDelayed playing_;
  CamDeliveryWatch delivery_;    // whether each alert reached the NVR, and those it missed
  bool pendingChanged_ = false;  // a new alert awaits its check; saved with the report that started it
  uint32_t nextDelayedAt_ = 0;
  bool delayedGap_ = false;
  uint32_t nvrSeenAt_ = 0;  // when the NVR was last listening, or the hub started
  bool nvrGone_ = false;
  uint32_t nvrGoneMs_ = CAM_NVR_GONE_MS;  // a test may shorten it
  CamJamWatch jam_;
  time_t lastTestAt_ = 0;
  int busyAlerts_ = CAM_BUSY_ALERTS;  // a test may change these two
  uint32_t overdueMs_ = 0;

  // Disarmed, alerts are logged but not sent to the NVR or the webhook, as in quiet hours. Armed home, interior sensors
  // are logged but not sent to the NVR. Kept across restarts.
  CamArmMode armMode_;
  int64_t disarmedUntil_;
  bool healthAlarm_;
  uint32_t rearmMinutes_;
  // When perimeter sensors stopped counting (the hub was disarmed), and interior ones (it left armed away).
  uint32_t perimeterOffAt_ = 0, interiorOffAt_ = 0;
  // A timed disarm: when it ends on the millisecond clock, known when it began in this run.
  bool disarmEndKnown_ = false;
  uint32_t disarmEndsAt_ = 0;
  uint32_t minuteMs_ = 60000;

  uint32_t beganAt_ = 0;
  bool networkSeen_ = false;
  bool startNoted_ = false;
  time_t lastAlive_ = 0;  // the last time the previous run knew it was running, from storage; 0 when unknown
  uint32_t aliveAt_ = 0;
  bool aliveWritten_ = false;
};
