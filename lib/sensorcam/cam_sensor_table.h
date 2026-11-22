// The sensor table: a row for each sensor folder on storage and for each node folder, with what the hub knows of each,
// and the senders heard from that have no folder. CamAlarm keeps it up to date and is the only part that changes it;
// the others are given it const, to read.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "cam_config.h"
#include "cam_pairing.h"
#include "cam_sensors.h"
#include "cam_storage.h"

class CamSensorTable {
 public:
  struct Health {
    bool heard = false;
    uint32_t heardAt = 0;
    uint32_t watchedAt = 0;  // when the hub started expecting reports, so a node dead since before a restart is still noticed
    bool silent = false;
    uint8_t battery = CAM_BATTERY_UNKNOWN;
    bool counterSeen = false;
    uint32_t counter = 0;    // the last report number, used only to ignore resends
    uint32_t counterAt = 0;  // when it arrived
    bool counterActive = false;
    bool reported = false;  // the state the sensor last reported, for the event history
    int8_t rssi = 0;        // signal of the last ESP-NOW packet, in dBm; 0 until one arrives
    bool lowBattery = false;
    int16_t rssiAvg8 = 0;  // smoothed signal, in eighths of a dBm
    uint8_t rssiCount = 0;
    bool weak = false;
    uint32_t refused = 0;  // ESP-NOW messages refused, and why the last one was
    const char* refusal = "";
    CamBusyCount busy;  // its alerts in the last hour, to notice one firing far too often
  };
  // One per node folder: what is the same for each of its sensors. A sensor's Slot points to it.
  struct Node {
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    CamNodeSettings settings;  // from node.txt in the node's folder
    CamNodeInfo info;          // what the node last said about itself, from node.txt
    // From pairing.txt: its own key, which its messages must be signed with instead of the shared one.
    bool paired = false;
    std::string key;
    uint8_t publicKey[CAM_X25519_LEN] = {0};
    int64_t batteryAt = 0;  // the newest reading in battery.txt
    int daysLeft = -1;      // how long its battery will last, from that file; -1 unknown
    // Only in memory:
    uint32_t walkUntil = 0;                                   // the end of a walk test started on the page, on the hub's clock in seconds since 1970
    bool sealed = false;                                      // its last report was sealed, so no one listening could read it
    bool runningOut = false;                                  // its battery is forecast to run out within a week, and that was logged
    uint32_t updateOffset = 0, updateSize = 0, updateAt = 0;  // how far a firmware update has got, and when it last asked
    bool updating = false;
    // The numbers the hub last gave it for its next report, the newest and the one before. A paired node's report that
    // carries neither may be one recorded earlier and played back: it still counts as an alert, but not as the node
    // being there, and the hub asks the node to prove it is.
    uint32_t fresh = 0, freshBefore = 0;
  };
  struct Slot {
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
    std::string name;            // from sensor.txt in the sensor's folder
    CamSensorSettings settings;  // from sensor.txt
    int node = 0;                // its node, from 1
  };
  struct UnknownSender {  // sent a valid report but has no folder, so its reports are ignored
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t input = 0;
    int8_t rssi = 0;
    uint32_t heardAt = 0;
    uint32_t messages = 0;
  };
  static const int MAX_UNKNOWN = 8;
  using UnknownList = UnknownSender[MAX_UNKNOWN];

  // cfg holds the start-up settings, which never change; it and storage outlive the table.
  CamSensorTable(const CamConfig& cfg, CamStorage& storage);

  // ---- reading. A sensor's id runs from 1, as 0 means none; a node's number too, as Slot::node gives it. ----
  int findSlot(const uint8_t mac[6], uint8_t input) const;
  int findNode(const uint8_t mac[6]) const;  // any sensor of that node, or 0
  // These, and their writable twins below, are the only places that turn an id into a table position.
  const Slot& slotOf(int id) const { return slots_[id - 1]; }
  const Health& healthOf(int id) const { return health_[id - 1]; }
  const Node& nodeOf(int id) const { return nodes_[slotOf(id).node - 1]; }
  const Node& nodeAt(int number) const { return nodes_[number - 1]; }
  int sensorCount() const;
  const std::string& keyFor(const Node* n) const { return n && n->paired ? n->key : cfg_.espNowKey; }  // what its messages are signed with
  std::string slotName(int id) const;                                                                  // its folder, as "AA-BB-CC-DD-EE-FF/1"
  uint32_t silentMsFor(int id) const;
  int slotForPath(const std::string& path) const;  // the sensor whose folder this is, as "AA-BB-CC-DD-EE-FF/1", or 0
  bool sensorFits(const std::string& path) const;  // false when the path would add a sensor beyond the limit
  uint32_t sensorsTag() const;                     // changes when a sensor is added, deleted or renamed
  const UnknownList& unknown() const { return unknown_; }

  // ---- changing, for the alarm ----
  Slot& slotOf(int id) { return slots_[id - 1]; }
  Health& healthOf(int id) { return health_[id - 1]; }
  Node& nodeOf(int id) { return nodes_[slotOf(id).node - 1]; }
  Node& nodeAt(int number) { return nodes_[number - 1]; }
  Node* nodeByMac(const uint8_t mac[6]);  // nullptr for a node without a folder
  // Keeps the most recent senders, so a new sensor can be seen on the page before it has been added.
  void noteUnknown(const uint8_t mac[6], uint8_t input, int8_t rssi, uint32_t now);
  // What a rescan changed: the rows emptied, and every row emptied or newly filled, whose state elsewhere starts again.
  struct Changes {
    bool readable = false;  // false: the folders could not be listed, and nothing changed
    bool gone[CAM_MAX_SENSORS] = {false};
    bool fresh[CAM_MAX_SENSORS] = {false};
    bool listChanged = false;  // a sensor added, deleted or renamed
  };
  Changes rescan(uint32_t now);  // rebuilds the table from the folders on storage

 private:
  const CamConfig& cfg_;
  CamStorage& storage_;
  std::vector<Slot> slots_;
  std::vector<Node> nodes_;  // as many as there can be sensors
  std::vector<Health> health_;
  UnknownSender unknown_[MAX_UNKNOWN];
};
