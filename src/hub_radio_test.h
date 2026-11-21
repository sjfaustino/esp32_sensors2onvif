// The Long Range radio test: whether the hub's Wi-Fi keeps working with ESP-NOW's Long Range mode allowed beside the
// usual modes, as nodes far away would need.
#pragma once
#include "cam_server.h"

bool radioTest(bool start, CamJson& j);  // starts the test when asked and it can; the Radio test page's members
void runRadioTest();                     // once a pass
