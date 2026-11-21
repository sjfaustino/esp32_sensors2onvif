// ESP-NOW: messages from the nodes arrive on the Wi-Fi task and wait in a queue for loop(), which owns all sensor state;
// messages to the nodes go out from loop().
#pragma once
#include <stdint.h>
#include "cam_server.h"

bool espNowBegin();  // in setup(), after Wi-Fi; false when ESP-NOW did not start
bool espNowRunning();
void espNowDeliver(SensorCamServer& server);  // once a pass: everything waiting, to the server, before it is polled
void sendToNode(const uint8_t mac[6], const uint8_t* data, int len);
uint32_t espNowDropped();  // arrived while the queue was full; their senders resend, since nothing confirmed them
