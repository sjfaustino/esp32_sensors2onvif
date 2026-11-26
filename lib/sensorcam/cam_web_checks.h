// The checks the API's handlers share, of what a page sends: in cam_settings.cpp, beside the table of saved settings.
#pragma once
#include <stdint.h>
#include <string>
#include "cam_config.h"

bool parseIpv4(const std::string& s, uint32_t& out);  // four numbers 0 to 255, dot apart
bool validAddress(const CamAddress& a);               // a fixed address that can work: its mask, its gateway on its network
bool wholeNumber(const std::string& s, long long& out);
// One to three NTP server names, comma-separated, tried in turn: each letters, digits, dots and hyphens.
bool validNtpServer(const std::string& s);
int camNtpServers(const std::string& s, std::string out[3]);  // the names, in order; 0 for a list that is not valid
bool validHubName(const std::string& s);
bool validOffset(long long minutes);  // a time zone: whole quarter hours from UTC-12 to UTC+14
bool validToken(const std::string& s);
bool validHookUrl(const std::string& url);
bool validMqttText(const std::string& s);
bool settingInt(const char* key, const std::string& value, int& out);  // a setting's number, by CAM_SETTINGS, for one kept as an int
