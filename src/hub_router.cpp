#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_router.h"
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <lwip/etharp.h>
#include <ping/ping_sock.h>
#include "cam_timing.h"

static volatile uint32_t replies = 0, lost = 0, lastReply = 0;
static volatile uint32_t repliesEver = 0, lostEver = 0;
static esp_ping_handle_t session = nullptr;
static uint32_t target = 0;

void startGatewayPing() {
  replies = 0;  // the restart rule waits for a reply, which also brings the reply time up to date
  lost = 0;
  uint32_t gateway = WiFi.gatewayIP();
  if (session && gateway == target) return;
  if (session) {
    esp_ping_stop(session);
    esp_ping_delete_session(session);
    session = nullptr;
  }
  target = gateway;
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = gateway;
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = CAM_ROUTER_PING_MS;
  config.timeout_ms = CAM_ROUTER_PING_WAIT_MS;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = [](esp_ping_handle_t, void*) {
    lastReply = millis();
    replies = replies + 1;
    repliesEver = repliesEver + 1;
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t, void*) {
    lost = lost + 1;
    lostEver = lostEver + 1;
  };
  if (esp_ping_new_session(&config, &callbacks, &session) == ESP_OK) esp_ping_start(session);
}

struct ArpQuery {
  struct netif* network;
  ip4_addr_t address;
  bool found;
};

static esp_err_t findInArpTable(void* arg) {
  ArpQuery* q = (ArpQuery*)arg;
  struct eth_addr* mac;
  const ip4_addr_t* ip;
  q->found = q->network && etharp_find_addr(q->network, &q->address, &mac, &ip) >= 0;
  return ESP_OK;
}

bool routerAnswersArp() {
  ArpQuery q = {(struct netif*)esp_netif_get_netif_impl(WiFi.STA.netif()), {}, false};
  q.address.addr = (uint32_t)WiFi.gatewayIP();
  return esp_netif_tcpip_exec(findInArpTable, &q) != ESP_OK || q.found;  // unable to ask is not evidence of a bad address
}

uint32_t pingReplies() { return replies; }
uint32_t pingLost() { return lost; }
uint32_t lastPingReply() { return lastReply; }
uint32_t pingRepliesEver() { return repliesEver; }
uint32_t pingLostEver() { return lostEver; }
