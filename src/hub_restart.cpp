#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_restart.h"
#include <esp_ota_ops.h>
#include <stddef.h>
#include "cam_timing.h"
#include "hub_settings.h"

static SensorCamServer** server_ = nullptr;
static bool pending = false;
static uint32_t requestedAt = 0;
static RestartCause requested = RESTART_UNKNOWN;
static uint32_t lastCause = 0;

// The core would otherwise accept every new firmware the moment it starts. loop() does that once it has proved itself.
extern "C" bool verifyRollbackLater() { return true; }

static bool unproven = false;

// The only object in the memory a restart leaves alone, so it stays at the same place from one firmware to the next;
// its layout and marks are in cam_board_rules.h.
RTC_NOINIT_ATTR static CamKept kept;

void restartBegin(SensorCamServer** server) {
  server_ = server;
  esp_ota_img_states_t state;
  unproven = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  lastCause = camReadKept(kept, esp_reset_reason() == ESP_RST_SW);
}

void requestRestart(RestartCause cause) {
  requested = cause;
  requestedAt = millis();
  pending = true;
}

void restartWhenAsked() {
  if (pending && millis() - requestedAt > CAM_RESTART_REPLY_MS) {
    confirmFirmware();  // someone used this firmware's pages to ask for this restart, so it must not count as a failure
    restartFor(requested);
  }
}

void restartFor(RestartCause cause) {
  if (server_ && *server_) (*server_)->saveNow();
  camKeepCause(kept, cause);
  ESP.restart();
}

void restartForRule(CamRestartRule rule) {
  switch (rule) {
    case CAM_RULE_NONE: return;
    case CAM_RULE_WIFI_LOST: return restartFor(RESTART_WIFI_LOST);
    case CAM_RULE_LOW_MEMORY: return restartFor(RESTART_LOW_MEMORY);
    case CAM_RULE_STALLED: return restartFor(RESTART_STALLED);
    case CAM_RULE_SCHEDULED: return restartFor(RESTART_SCHEDULED);
    case CAM_RULE_BAD_FIXED_ADDRESS: forgetStatic(); return restartFor(RESTART_BAD_FIXED_ADDRESS);
    case CAM_RULE_ROUTER_SILENT: return restartFor(RESTART_ROUTER_SILENT);
  }
}

uint32_t lastRestartCause() { return lastCause; }

bool startWasPlanned() { return camStartPlanned(esp_reset_reason() == ESP_RST_SW, lastCause); }

const char* lastRestartText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Power on or reset button";
    case ESP_RST_PANIC: return "Crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Low power supply voltage";
    case ESP_RST_SW: break;
    default: return "Other";
  }
  return camRestartText(lastCause);
}

bool firmwareUnproven() { return unproven; }

void confirmFirmware() {
  if (unproven && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) unproven = false;
}

void noteUpdateStarting(bool nvrListening) { camKeepUpdate(kept, nvrListening); }

// Keeps a new firmware once it has proved itself, or goes back to the previous one; see CamRestartRules::firmware.
void proveFirmware(const CamRestartRules::Pass& p, CamRestartRules& rules, bool espNowStarted) {
  if (!unproven || !server_ || !*server_) return;
  SensorCamServer* server = *server_;
  bool needNvr = camUpdateNeedsNvr(kept);
  CamFirmwareVerdict verdict = rules.firmware(p, espNowStarted, needNvr, server->nvrListeningNow());
  if (verdict == CAM_FIRMWARE_KEEP) {
    confirmFirmware();
    camForgetUpdate(kept);
    Serial.println("The new firmware has proved itself and is kept.");
  } else if (verdict == CAM_FIRMWARE_UNDO) {
    Serial.printf("The new firmware has not %s in 10 minutes; going back to the previous firmware.\n", !espNowStarted ? "started ESP-NOW"
                                                                                                       : !p.connected
                                                                                                           ? "stayed on the network"
                                                                                                           : "got the NVR listening again");
    server->saveNow();
    camForgetUpdate(kept);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}
