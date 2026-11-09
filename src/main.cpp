#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_now.h>
#include <freertos/queue.h>
#include <ping/ping_sock.h>
#include "cam_server.h"
#include "config.h"

static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

static std::string macSerial() {
  uint64_t mac = ESP.getEfuseMac();
  char b[13];
  snprintf(b, sizeof(b), "%012llx", (unsigned long long)mac);
  return b;
}

static CamConfig makeConfig() {
  CamConfig c;
  c.onvifPort = ONVIF_PORT;
  c.rtspPort = RTSP_PORT;
  c.holdMs = SENSOR_HOLD_MS;
  c.sensorToken = SENSOR_TOKEN;
  c.webPassword = WEB_PASSWORD;
  // Wi-Fi and lwIP fail badly when RAM runs out, so a large request must leave them room.
  // A request for 0 bytes asks only whether new work is safe, which needs less headroom.
  c.canAllocate = [](size_t n) { return n == 0 ? ESP.getFreeHeap() >= 10 * 1024 : ESP.getMaxAllocHeap() >= n + 1024 && ESP.getFreeHeap() >= n + 16 * 1024; };
  c.fsUsage = [](uint32_t& used, uint32_t& total) {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
  };
  c.serial = macSerial();
  return c;
}

static const uint32_t WIFI_RESTART_MS = 5UL * 60UL * 1000UL;
static const uint32_t LOW_HEAP_BYTES = 8 * 1024;
static const uint32_t LOW_HEAP_RESTART_MS = 30000;
static volatile uint32_t wifiDrops = 0;
static volatile uint8_t wifiDropReason = 0;

// Survives a software restart, so the heartbeat can say which rule caused it: 1 Wi-Fi, 2 low RAM, 3 server stalled, 4 router silent.
RTC_NOINIT_ATTR static uint32_t restartCause;
static uint32_t lastRestartCause = 0;
static void restartFor(uint32_t cause) {
  restartCause = cause;
  ESP.restart();
}

static const uint32_t PING_LOST_RESTART_MS = 90000;
static volatile uint32_t pingReplies = 0, pingLost = 0, lastPingReply = 0;
static esp_ping_handle_t pingSession = nullptr;

// Wi-Fi can report "connected" while the network stack answers nothing, so the router is asked directly.
static void startGatewayPing() {
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.target_addr.type = IPADDR_TYPE_V4;
  config.target_addr.u_addr.ip4.addr = (uint32_t)WiFi.gatewayIP();
  config.count = ESP_PING_COUNT_INFINITE;
  config.interval_ms = 20000;
  config.timeout_ms = 3000;
  esp_ping_callbacks_t callbacks = {};
  callbacks.on_ping_success = [](esp_ping_handle_t, void*) {
    pingReplies = pingReplies + 1;
    lastPingReply = millis();
  };
  callbacks.on_ping_timeout = [](esp_ping_handle_t, void*) { pingLost = pingLost + 1; };
  if (esp_ping_new_session(&config, &callbacks, &pingSession) == ESP_OK) esp_ping_start(pingSession);
}

static SensorCamServer* server;
static bool wasConnected = false;

struct SensorMessage {
  uint8_t mac[6];
  SensorReport report;
};
static QueueHandle_t sensorQueue;

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (!parseSensorPacket(data, len, m.report)) return;
  memcpy(m.mac, info->src_addr, 6);
  xQueueSend(sensorQueue, &m, 0);
}

void setup() {
  lastRestartCause = esp_reset_reason() == ESP_RST_SW ? restartCause : 0;
  restartCause = 0;
  Serial.begin(115200);
  if (strlen(WIFI_SSID) == 0) {
    // Repeats because native USB serial misses anything printed before the PC reattaches.
    while (true) {
      Serial.println("No Wi-Fi settings: copy include/secrets.example.h to include/secrets.h and rebuild.");
      delay(2000);
    }
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save loses multicast and adds latency
  WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    wifiDrops = wifiDrops + 1;
    wifiDropReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, NTP_SERVER);

  // Formats on first use. Without storage the hub still runs, using its built-in pictures.
  if (!LittleFS.begin(true)) Serial.println("LittleFS could not start; using built-in pictures.");

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    Serial.println("Could not open the ONVIF or RTSP port; restarting.");
    delay(2000);
    ESP.restart();
  }

  sensorQueue = xQueueCreate(32, sizeof(SensorMessage));
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onEspNow);
  else Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    server->networkUp();
    if (!pingSession) startGatewayPing();
  }
  wasConnected = connected;

  // An unattended hub must not stay off the network: keep asking, then start clean.
  static uint32_t lostAt = 0, retryAt = 0;
  if (connected) {
    lostAt = 0;
  } else {
    if (lostAt == 0) lostAt = millis() | 1;
    if (millis() - retryAt > 30000) {
      retryAt = millis();
      WiFi.reconnect();
    }
    if (millis() - lostAt > WIFI_RESTART_MS) restartFor(1);
  }

  // Out of RAM the network stack stops working while this loop carries on, so the loop watchdog cannot see it.
  static uint32_t lowSince = 0;
  if (ESP.getFreeHeap() >= LOW_HEAP_BYTES) {
    lowSince = 0;
  } else {
    if (lowSince == 0) lowSince = millis() | 1;
    if (millis() - lowSince > LOW_HEAP_RESTART_MS) restartFor(2);
  }
  if (server->stalledMs(millis()) > 30000) restartFor(3);  // the loop runs but the server has stopped serving
  // Only after the router has answered at least once, so a router that ignores ping cannot cause restarts.
  if (connected && pingReplies > 0 && millis() - lastPingReply > PING_LOST_RESTART_MS) restartFor(4);

  static uint32_t printedAt = 0;
  if (millis() - printedAt > 10000) {
    printedAt = millis();
    Serial.printf("ONVIF on %s:%u, ESP-NOW to %s on Wi-Fi channel %d, status %d, RSSI %d, drops %u, last drop reason %u, free heap %u, largest block %u, last reset reason %d cause %u, ping ok %u lost %u, %s\n",
                  WiFi.localIP().toString().c_str(), ONVIF_PORT, WiFi.macAddress().c_str(), WiFi.channel(), (int)WiFi.status(), WiFi.RSSI(), (unsigned)wifiDrops,
                  (unsigned)wifiDropReason, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), (int)esp_reset_reason(), (unsigned)lastRestartCause, (unsigned)pingReplies, (unsigned)pingLost, server->debugLine().c_str());
  }

  SensorMessage m;
  while (xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->espNowInput(m.mac, m.report);
  server->poll(20);
}
