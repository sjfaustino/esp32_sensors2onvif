// Whether the router answers: Wi-Fi can report "connected" while the network stack answers nothing, so the router is
// pinged directly, and asked over ARP, which a router that ignores ping must still answer.
#pragma once
#include <stdint.h>

void startGatewayPing();  // on every connection: replies count only from this one, and the backup network has another router
bool routerAnswersArp();  // for CamRestartRules: a fixed address that reaches no router is forgotten

// The ping's counts, set by its task. Since this connection, and the reply's time, for the restart rules; and ever,
// never reset by a new connection, for the radio test.
uint32_t pingReplies();
uint32_t pingLost();
uint32_t lastPingReply();
uint32_t pingRepliesEver();
uint32_t pingLostEver();
