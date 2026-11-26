// Wi-Fi: the main and backup networks, the setup network the hub opens when neither works, and a fixed address. The
// saved networks are in settings, from hub_settings.h.
#pragma once
#include <stdint.h>
#include <string>
#include "cam_server.h"

void wifiBegin();  // in setup(): the radio's settings, and the first try at the main network
// Off the network: keep asking, on the main network then the backup, with the setup network open behind them. True while
// someone is on the setup network. Once a pass; justLost: the connection went since the last pass.
bool keepOnNetwork(bool connected, bool justLost);
void serveSetupNetwork();  // once a pass: its name lookups, which send every name to the setup page

bool onNetwork();  // joined, and with an address: joined without one counts as off
bool setupIsOpen();
bool usingStaticAddress();  // a fixed address, not DHCP, on the network the hub is on
uint32_t wifiDrops();       // since the start, for the heartbeat and the radio test
uint8_t wifiDropReason();   // the last drop's, as Wi-Fi gives it

// For the pages.
void networksInfo(CamJson& j);  // the setup page's: the saved networks and those in range
bool saveWifi(const std::string& ssid, const std::string& password, bool backup);
void addressInfo(CamJson& j);
bool saveAddress(const CamAddress& a);
