#include <Arduino.h>  // first, before the network headers the hub's library brings

#include "hub_radio_test.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "hub_router.h"
#include "hub_wifi.h"

// For LR_TEST_MS only: then the modes go back as they were, by the hub's own clock, not by anything over the network, so
// a test that cut the hub off its network undoes itself and the hub reconnects as after any drop. Not kept through a
// restart either. Meanwhile it watches the Wi-Fi drops, the router's answers to the ping every 20 s, and the signal.
static const uint32_t LR_TEST_MS = 120000;
static const uint32_t LR_TEST_DELAY_MS = 1000;  // the change waits this long, so the reply that started it gets out first
struct LrTest {
  bool running = false, done = false, applied = false;
  uint32_t startedAt = 0, lastSample = 0;     // startedAt: when LR was allowed, or asked for until then
  uint8_t before = 0, during = 0, after = 0;  // the protocol bits: as found, as read back with LR, as put back
  esp_err_t setErr = ESP_OK, restoreErr = ESP_OK;
  uint32_t drops0 = 0, replies0 = 0, lost0 = 0, drops = 0, replies = 0, lost = 0;
  int rssiBefore = 0, rssiMin = 0, rssiMax = -127, samples = 0, offSamples = 0;
  long rssiSum = 0;
  bool connectedAtEnd = false;
};
static LrTest lr;

static void lrEnd() {
  lr.restoreErr = esp_wifi_set_protocol(WIFI_IF_STA, lr.before);
  esp_wifi_get_protocol(WIFI_IF_STA, &lr.after);
  lr.drops = wifiDrops() - lr.drops0;
  lr.replies = pingRepliesEver() - lr.replies0;
  lr.lost = pingLostEver() - lr.lost0;
  lr.connectedAtEnd = WiFi.status() == WL_CONNECTED;
  lr.running = false;
  lr.done = true;
  Serial.printf("Long Range test over: %u drops, %u router replies, %u lost; modes back to 0x%02X.\n", (unsigned)lr.drops, (unsigned)lr.replies,
                (unsigned)lr.lost, lr.after);
}

void runRadioTest() {
  if (!lr.running) return;
  uint32_t now = millis();
  if (!lr.applied) {
    if (now - lr.startedAt < LR_TEST_DELAY_MS) return;
    lr.applied = true;
    lr.startedAt = lr.lastSample = now;
    lr.setErr = esp_wifi_set_protocol(WIFI_IF_STA, lr.before | WIFI_PROTOCOL_LR);
    esp_wifi_get_protocol(WIFI_IF_STA, &lr.during);
    Serial.printf("Long Range test for %u s: modes 0x%02X -> 0x%02X (%s).\n", (unsigned)(LR_TEST_MS / 1000), lr.before, lr.during,
                  esp_err_to_name(lr.setErr));
    return;
  }
  if (now - lr.lastSample >= 1000) {
    lr.lastSample = now;
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      lr.rssiSum += rssi;
      lr.samples++;
      if (rssi < lr.rssiMin) lr.rssiMin = rssi;
      if (rssi > lr.rssiMax) lr.rssiMax = rssi;
    } else {
      lr.offSamples++;
    }
  }
  if (now - lr.startedAt >= LR_TEST_MS) lrEnd();
}

bool radioTest(bool start, CamJson& j) {
  if (start) {
    if (lr.running || WiFi.status() != WL_CONNECTED) return false;
    lr = LrTest();
    esp_wifi_get_protocol(WIFI_IF_STA, &lr.before);
    lr.drops0 = wifiDrops();
    lr.replies0 = pingRepliesEver();
    lr.lost0 = pingLostEver();
    lr.rssiBefore = WiFi.RSSI();
    lr.rssiMin = 0;
    lr.startedAt = millis();
    lr.running = true;  // runRadioTest allows LR a second from now, after this reply, and puts it back by the clock
  }
  j.str("state", !lr.running ? (lr.done ? "done" : "not run") : lr.applied ? "running" : "starting");
  if (!lr.running && !lr.done) return true;
  uint32_t elapsed = !lr.applied ? 0 : (lr.running ? millis() : lr.startedAt + LR_TEST_MS) - lr.startedAt;
  j.num("seconds", elapsed / 1000).num("of", LR_TEST_MS / 1000);
  j.str("set", esp_err_to_name(lr.setErr)).num("modesBefore", lr.before).num("modesDuring", lr.during).flag("lrTaken", lr.during & WIFI_PROTOCOL_LR);
  j.num("rssiBefore", lr.rssiBefore).num("samplesOn", lr.samples).num("samplesOff", lr.offSamples);
  j.num("rssiMin", lr.samples != 0, lr.rssiMin)
      .num("rssiMax", lr.samples != 0, lr.rssiMax)
      .num("rssiAverage", lr.samples != 0, lr.samples ? lr.rssiSum / lr.samples : 0);
  uint32_t drops = lr.running ? wifiDrops() - lr.drops0 : lr.drops, replies = lr.running ? pingRepliesEver() - lr.replies0 : lr.replies,
           lost = lr.running ? pingLostEver() - lr.lost0 : lr.lost;
  j.num("wifiDrops", drops).num("routerReplies", replies).num("routerLost", lost);
  if (lr.done) j.str("restore", esp_err_to_name(lr.restoreErr)).num("modesAfter", lr.after).flag("connectedAtEnd", lr.connectedAtEnd);
  j.flag("connectedNow", WiFi.status() == WL_CONNECTED);
  return true;
}
