#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_restart.h"
#include <esp_ota_ops.h>
#include "cam_timing.h"
#include "hub_settings.h"

static const char* const RESTART_TEXT[RESTART_CAUSES] = {"Software restart",
                                                         "WiFi lost for 5 minutes",
                                                         "Low memory",
                                                         "Server stalled",
                                                         "Router stopped answering",
                                                         "New WiFi settings",
                                                         "New web, port or address settings",
                                                         "Fixed address could not reach the router",
                                                         "Scheduled restart",
                                                         "Settings reset",
                                                         "Restart button",
                                                         "Firmware update",
                                                         "Settings restored",
                                                         "New keys"};

static SensorCamServer** server_ = nullptr;
static bool pending = false;
static uint32_t requestedAt = 0;
static RestartCause requested = RESTART_UNKNOWN;
static uint32_t lastCause = 0;

// The core would otherwise accept every new firmware the moment it starts. loop() does that once it has proved itself.
extern "C" bool verifyRollbackLater() { return true; }

static bool unproven = false;

// What is kept through a restart, in the memory a restart leaves alone: why the hub restarted itself, and, set as an
// update starts, whether the NVR was listening then, so the new firmware must get it listening again before it is kept.
// One object, the only one there, so it stays at the same place from one firmware to the next; and marked, so what a
// power cut leaves, or a firmware that kept these elsewhere, is told apart from what this one wrote. Change the mark
// with the layout.
struct Kept {
  uint32_t mark;
  uint32_t cause;  // a RestartCause
  uint32_t updateMark;
  uint32_t needNvr;
};
static const uint32_t KEPT_MARK = 0x4B505431;    // "KPT1"
static const uint32_t UPDATE_MARK = 0x50524F46;  // "PROF"
RTC_NOINIT_ATTR static Kept kept;

void restartBegin(SensorCamServer** server) {
  server_ = server;
  esp_ota_img_states_t state;
  unproven = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  bool ours = kept.mark == KEPT_MARK;
  lastCause = esp_reset_reason() == ESP_RST_SW && ours && kept.cause < RESTART_CAUSES ? kept.cause : RESTART_UNKNOWN;
  if (!ours) kept.updateMark = 0;  // not this firmware's, so not a proof either
  kept.mark = KEPT_MARK;
  kept.cause = RESTART_UNKNOWN;
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
  kept.mark = KEPT_MARK;
  kept.cause = cause;
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

bool startWasPlanned() {
  if (esp_reset_reason() != ESP_RST_SW) return false;
  switch (lastCause) {
    case RESTART_NEW_WIFI:
    case RESTART_NEW_SETTINGS:
    case RESTART_SCHEDULED:
    case RESTART_SETTINGS_RESET:
    case RESTART_BUTTON:
    case RESTART_FIRMWARE:
    case RESTART_RESTORED:
    case RESTART_NEW_KEYS: return true;
    default: return false;
  }
}

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
  return RESTART_TEXT[lastCause < RESTART_CAUSES ? lastCause : RESTART_UNKNOWN];
}

bool firmwareUnproven() { return unproven; }

void confirmFirmware() {
  if (unproven && esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) unproven = false;
}

void noteUpdateStarting(bool nvrListening) {
  kept.updateMark = UPDATE_MARK;
  kept.needNvr = nvrListening;
}

// Keeps a new firmware once it has proved itself, or goes back to the previous one; see CamRestartRules::firmware.
void proveFirmware(const CamRestartRules::Pass& p, CamRestartRules& rules, bool espNowStarted) {
  if (!unproven || !server_ || !*server_) return;
  SensorCamServer* server = *server_;
  bool needNvr = kept.updateMark == UPDATE_MARK && kept.needNvr;
  CamFirmwareVerdict verdict = rules.firmware(p, espNowStarted, needNvr, server->nvrListeningNow());
  if (verdict == CAM_FIRMWARE_KEEP) {
    confirmFirmware();
    kept.updateMark = 0;
    Serial.println("The new firmware has proved itself and is kept.");
  } else if (verdict == CAM_FIRMWARE_UNDO) {
    Serial.printf("The new firmware has not %s in 10 minutes; going back to the previous firmware.\n", !espNowStarted ? "started ESP-NOW"
                                                                                                       : !p.connected
                                                                                                           ? "stayed on the network"
                                                                                                           : "got the NVR listening again");
    server->saveNow();
    kept.updateMark = 0;
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}
