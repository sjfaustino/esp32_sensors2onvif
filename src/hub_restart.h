// Why and when the hub restarts itself, kept across the restart so Device Info and the heartbeat can say which rule
// caused it; and whether a new firmware is kept, once it has proved itself.
#pragma once
#include <stdint.h>
#include "cam_board_rules.h"  // RestartCause, and what is kept through a restart
#include "cam_restart_rules.h"
#include "cam_server.h"

void restartBegin(SensorCamServer** server);  // first in setup(): reads why the last start happened; server is saved before a restart
void requestRestart(RestartCause cause);      // after the reply has reached the browser; see restartWhenAsked
void restartWhenAsked();                      // once a pass
void restartFor(RestartCause cause);          // now, saving what the server holds first
void restartForRule(CamRestartRule rule);     // carries out what a restart rule decided
uint32_t lastRestartCause();                  // a RestartCause, when the last start was the hub's own restart; else 0
const char* lastRestartText();                // why the hub last started, as Device Info shows it
bool startWasPlanned();                       // asked for by someone, not a crash, a watchdog, a power cut or a fault

// A new firmware: whether it is still to prove itself, the mark an update leaves for the next start, and the check.
bool firmwareUnproven();
void confirmFirmware();                      // until this is called, any restart makes the bootloader go back to the previous firmware
void noteUpdateStarting(bool nvrListening);  // as an update starts: the new firmware must get the NVR listening again
void proveFirmware(const CamRestartRules::Pass& p, CamRestartRules& rules, bool espNowStarted);
