// The sensor nodes' side of the hub that is not about alerts: pairing them with public keys (cam_pairing.h), and the
// firmware files they update themselves from over ESP-NOW (cam_node_images.h). SensorCamServer owns one, passes it
// the nodes' hello and firmware messages, and serves its web pages.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_config.h"
#include "cam_node_images.h"
#include "cam_pairing.h"
#include "cam_storage.h"

class CamNodes {
 public:
  // It and the others outlive the nodes; images are the hub's node firmware files, which the alarm reads too.
  CamNodes(const CamConfig& cfg, CamRadio& radio, CamStorage& storage, CamAlarm& alarm, const CamSensorTable& table, CamNodeImages& images,
           std::function<uint32_t()> millisFn)
      : cfg_(cfg), radio_(&radio), storage_(storage), alarm_(alarm), table_(table), millis_(millisFn), images_(images) {}
  void begin();  // reads the firmware files, and works out the hub's public key: tens of milliseconds on the board, once

  // ESP-NOW messages from the nodes. From the task that calls the server's poll().
  void hello(const uint8_t mac[6], const uint8_t* data, int len, int8_t rssi);  // a node asking to pair, or for the hub's channel
  void firmwareAsk(const uint8_t mac[6], const uint8_t* data, int len);         // answered at once, part by part

  // ---- pairing ----
  bool pairingReady() const { return pairingReady_; }  // the hub has a pairing key
  bool pairingOpen(uint32_t now) const { return pairingOpen_ && (int32_t)(pairingUntil_ - now) > 0; }
  void openPairing(bool open, uint32_t now);  // open for a while, or closed
  void forgetQuiet(uint32_t now);             // nodes waiting for approval that have stopped asking
  // Writes the node's key, then makes its inputs' folders. False, with why, when it cannot.
  bool approve(const uint8_t mac[6], std::string& error);
  void reject(const uint8_t mac[6]);
  std::string pairingJson();

  // ---- the firmware files ----
  CamNodeImages& images() { return images_; }
  const CamNodeImages& images() const { return images_; }

 private:
  static const int MAX_PAIR_REQUESTS = 8;
  struct PairRequest {  // a node that said hello while pairing was open, waiting for approval
    bool used = false;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t inputs = 1;
    uint8_t publicKey[CAM_X25519_LEN] = {0};
    uint32_t heardAt = 0;
    int8_t rssi = 0;
  };

  const CamConfig& cfg_;
  CamRadio* radio_;
  CamStorage& storage_;
  CamAlarm& alarm_;
  const CamSensorTable& table_;
  std::function<uint32_t()> millis_;
  CamNodeImages& images_;
  bool pairingReady_ = false;
  uint8_t hubPrivate_[CAM_X25519_LEN] = {0}, hubPublic_[CAM_X25519_LEN] = {0};
  bool pairingOpen_ = false;
  uint32_t pairingUntil_ = 0;
  PairRequest pairRequests_[MAX_PAIR_REQUESTS];
};
