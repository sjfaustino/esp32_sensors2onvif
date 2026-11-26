// The clock: from NTP or set by hand, in the time zone the Time page chose.
#pragma once
#include "cam_server.h"

void applyTime();           // the time zone and where the time comes from, as the settings say; in setup() and after a change
void keepTime();            // once a pass: stops the one-off NTP reading a manual time asks for after a power cut, once it has answered
bool clockSet();            // the clock has a real time
int localMinute();          // minutes after local midnight, or -1 when the clock is not set
int localWeekday();         // 0 for Sunday to 6, or -1 when the clock is not set
bool scheduledMinute();     // the minute the page set for the scheduled restart
void timeInfo(CamJson& j);  // the Time page's members
bool saveTime(const CamTime& t);
bool takeTimeRefused(int64_t& stepS);  // an NTP reading was refused since the last call, and how far it would have moved the clock
