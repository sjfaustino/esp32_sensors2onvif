#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_espnow.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/queue.h>

struct SensorMessage {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t len;
  uint8_t data[CAM_SEALED_EXTRA + CAM_NODE_INFO_MAX];  // the longest the hub reads, sealed node info; anything longer is dropped
};
// A message longer than the queue's room is dropped without a word, so each one the hub reads must fit.
static_assert(sizeof(SensorMessage::data) >= S2O_REPORT_MAX, "a report with every target must fit");
static_assert(sizeof(SensorMessage::data) >= CAM_HELLO_LEN, "a pairing hello must fit");
static_assert(sizeof(SensorMessage::data) >= CAM_SEALED_EXTRA + S2O_REPORT_MAX, "a sealed report with every target must fit");

static QueueHandle_t queue = nullptr;
static volatile uint32_t dropped = 0;
static bool running = false;

// Runs on the Wi-Fi task, so it only queues.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (len < 1) return;
  if (len > (int)sizeof(m.data)) {  // counted too: a node sending what this hub cannot read would else look only silent
    dropped = dropped + 1;
    return;
  }
  memcpy(m.data, data, len);
  m.len = (uint8_t)len;
  memcpy(m.mac, info->src_addr, 6);
  m.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  if (xQueueSend(queue, &m, 0) != pdTRUE) dropped = dropped + 1;
}

bool espNowBegin() {
  queue = xQueueCreate(32, sizeof(SensorMessage));
  running = queue && esp_now_init() == ESP_OK;
  if (running) esp_now_register_recv_cb(onEspNow);
  return running;
}

bool espNowRunning() { return running; }

void espNowDeliver(SensorCamServer& server) {
  SensorMessage m;
  while (queue && xQueueReceive(queue, &m, 0) == pdTRUE) server.espNowPacket(m.mac, m.data, m.len, m.rssi);
}

// ESP-NOW sends only to registered peers, and holds at most 20, so when it is full the first in its list makes room.
void sendToNode(const uint8_t mac[6], const uint8_t* data, int len) {
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;  // the channel Wi-Fi is on
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_ERR_ESPNOW_FULL) {
      esp_now_peer_info_t oldest;
      if (esp_now_fetch_peer(true, &oldest) == ESP_OK) esp_now_del_peer(oldest.peer_addr);
      esp_now_add_peer(&peer);
    }
  }
  esp_now_send(mac, data, len);
}

uint32_t espNowDropped() { return dropped; }
