#include "device_info.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <nvs.h>
#include "config.h"
#include "hub_settings.h"

static const uint32_t CHIP_MEMORY = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;  // the chip's own, as malloc uses it
uint32_t largestBlock() { return heap_caps_get_largest_free_block(CHIP_MEMORY); }
uint32_t largestAnyBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT); }

std::string macSerial() {
  uint64_t mac = ESP.getEfuseMac();
  char b[13];
  snprintf(b, sizeof(b), "%012llx", (unsigned long long)mac);
  return b;
}

// ---- Crash reports: after a crash the chip saves one to flash, kept until deleted or replaced by the next ----
static size_t crashBytes = 0;  // checked once at start, because checking reads the whole report
static String crashSummary = "None saved";

static void loadCrash() {
  size_t addr = 0;
  if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&addr, &crashBytes) != ESP_OK) {
    crashBytes = 0;
    return;
  }
  esp_core_dump_summary_t* s = new esp_core_dump_summary_t();
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    char running[APP_ELF_SHA256_SZ] = "", at[12];
    esp_app_get_elf_sha256(running, sizeof(running));
    snprintf(at, sizeof(at), "0x%08lx", (unsigned long)s->exc_pc);
    bool same = strncmp(running, (const char*)s->app_elf_sha256, sizeof(running) - 1) == 0;
    crashSummary = String("In task ") + s->exc_task + " at " + at + (same ? ", in this firmware" : ", in an earlier firmware");
  } else {
    crashSummary = "Saved, but it could not be read";
  }
  delete s;
}

size_t crashSize() { return crashBytes; }

bool readCrash(size_t offset, char* out, size_t len) {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  return p && offset + len <= crashBytes && esp_partition_read(p, offset, out, len) == ESP_OK;
}

bool deleteCrash() {
  if (esp_core_dump_image_erase() != ESP_OK) return false;
  crashBytes = 0;
  crashSummary = "None saved";
  return true;
}

// ---- memory ----

static uint32_t programBytes = 0;
// The worst the free memory has been cut up, which decides whether a large reply can be built. Free memory alone does
// not show it.
static uint32_t smallestBlock = UINT32_MAX;

static TaskHandle_t loopTask = nullptr;  // setup() runs on the loop's task, so this is it

void deviceInfoBegin() {
  loopTask = xTaskGetCurrentTaskHandle();
  programBytes = ESP.getSketchSize();  // verifies the whole firmware, which stalls the hub for 0.4 s, so only once
  loadCrash();
}

// FreeRTOS keeps the least since the task began; on the ESP32 it counts bytes.
uint32_t loopStackLeast() { return loopTask ? (uint32_t)uxTaskGetStackHighWaterMark(loopTask) : 0; }

uint32_t worstLargestBlock() { return smallestBlock == UINT32_MAX ? largestBlock() : smallestBlock; }

// The day's low, since the last daily check: a fall that recovers leaves nothing else to see.
static uint32_t dayLow = UINT32_MAX;
static int64_t dayLowAt = 0;
static uint16_t dayLowWeb = 0, dayLowVideo = 0;

static uint32_t sinceTaken = UINT32_MAX;  // the least since the week of health last asked, a few seconds ago
static uint32_t sinceStart = UINT32_MAX;
static bool dayStarted = false;  // a daily check has started a new day; until then the day's low is since the start
static bool watching = false;

// The least free memory in the last second. The heap keeps its least free at every allocation, from the start or from
// when it is told to begin again; so a fall that lasts a moment, as a web request's does, is caught, which a look at
// the free memory now and then would miss. Read, then begun again, each second.
static uint32_t secondLow() {
  uint32_t low = watching ? heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) : ESP.getMinFreeHeap();
  if (watching) heap_caps_monitor_local_minimum_free_size_stop();
  watching = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
  return low;
}

void sampleMemory(int webOpen, int videoOpen) {
  static uint32_t sampledAt = 0;
  if (millis() - sampledAt < 1000) return;
  sampledAt = millis();
  uint32_t block = largestBlock();
  if (block < smallestBlock) smallestBlock = block;
  uint32_t free = secondLow();
  if (free < sinceTaken) sinceTaken = free;
  if (free < sinceStart) sinceStart = free;
  if (free < dayLow) {
    dayLow = free;
    time_t now = time(nullptr);
    dayLowAt = now >= CAM_CLOCK_VALID_AFTER ? (int64_t)now : 0;
    dayLowWeb = (uint16_t)webOpen;
    dayLowVideo = (uint16_t)videoOpen;
  }
}

void memoryDayLow(uint32_t& lowFree, int64_t& lowAt, uint16_t& lowWeb, uint16_t& lowVideo) {
  lowFree = dayLow == UINT32_MAX ? ESP.getFreeHeap() : dayLow;
  lowAt = dayLowAt;
  lowWeb = dayLowWeb;
  lowVideo = dayLowVideo;
}

void memoryDayStart() {
  dayLow = UINT32_MAX;
  dayStarted = true;
}

uint32_t memoryLowTaken() {
  uint32_t low = sinceTaken == UINT32_MAX ? ESP.getFreeHeap() : sinceTaken;
  sinceTaken = UINT32_MAX;
  return low;
}

// While the heap is watched a second at a time, its own least is only that second's.
uint32_t memoryLowSinceStart() { return sinceStart == UINT32_MAX ? ESP.getMinFreeHeap() : sinceStart; }

// How the memory is cut up: the largest free pieces and the largest pieces in use, to find what holds memory apart.
// The walk holds the heap's lock, so it only fills fixed arrays and allocates nothing.
struct HeapPieces {
  static const int TOP = 8;
  uint32_t freeTop[TOP] = {}, usedTop[TOP] = {};
  uint32_t freeCount = 0, usedCount = 0, usedFrom1k = 0;
  uint32_t bucketCount[5] = {}, bucketBytes[5] = {};  // in use: under 64 B, 256 B, 1 KB, 4 KB, and larger
};

static void keepLargest(uint32_t* top, uint32_t size) {
  for (int i = 0; i < HeapPieces::TOP; i++) {
    if (size <= top[i]) continue;
    for (int k = HeapPieces::TOP - 1; k > i; k--) top[k] = top[k - 1];
    top[i] = size;
    return;
  }
}

static bool countPiece(walker_heap_into_t, walker_block_info_t block, void* user) {
  HeapPieces& p = *(HeapPieces*)user;
  if (block.used) {
    p.usedCount++;
    p.usedFrom1k += block.size >= 1024;
    int b = block.size < 64 ? 0 : block.size < 256 ? 1 : block.size < 1024 ? 2 : block.size < 4096 ? 3 : 4;
    p.bucketCount[b]++;
    p.bucketBytes[b] += block.size;
    keepLargest(p.usedTop, block.size);
  } else {
    p.freeCount++;
    keepLargest(p.freeTop, block.size);
  }
  return true;
}

static String pieceList(const uint32_t* top) {
  String out;
  for (int i = 0; i < HeapPieces::TOP && top[i]; i++) out += (i ? ", " : "") + String(top[i] / 102.4f / 10, 1);
  return out + " KB";
}

static String uptimeText() {
  uint64_t s = esp_timer_get_time() / 1000000ULL;
  char b[32];
  snprintf(b, sizeof(b), "%lud %02luh %02lum %02lus", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600), (unsigned long)(s % 3600 / 60),
           (unsigned long)(s % 60));
  return b;
}

void deviceRows(CamJson& j, const DeviceStatus& status) {
  auto add = [&](const char* label, const String& value) { j.array().str(label).str(value.c_str()).end(); };
  add("Device name", settings.hubName);
  add("Firmware version", FIRMWARE_VERSION);
  add("Firmware built", __DATE__ " " __TIME__);
  add("Firmware check", status.firmwareUnproven                ? "New firmware, being checked"
                        : esp_ota_get_last_invalid_partition() ? "The last update failed and was undone"
                                                               : "Good");
  add("Serial number", macSerial().c_str());
  add("Chip", String(ESP.getChipModel()) + " revision " + ESP.getChipRevision());
  add("CPU", String(ESP.getChipCores()) + " cores at " + ESP.getCpuFreqMHz() + " MHz");
  add("Flash size", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
  if (ESP.getPsramSize())
    add("PSRAM", String(ESP.getPsramSize() / (1024 * 1024)) + " MB, " + ESP.getFreePsram() / 1024 + " KB free, the largest free block " +
                     ESP.getMaxAllocPsram() / 1024 + " KB");
  else
    add("PSRAM", String("None"));
  add("Program size", String(programBytes / 1024) + " KB of " + esp_ota_get_running_partition()->size / 1024 + " KB");
  add("Arduino core", ESP_ARDUINO_VERSION_STR);
  add("ESP-IDF", esp_get_idf_version());
  add("Uptime", uptimeText());
  add("Last restart", status.lastRestart);
  add("Last crash", crashSummary);
  add("Free memory", String(ESP.getFreeHeap() / 1024) + " KB");
  add("Lowest free memory", String(memoryLowSinceStart() / 1024) + " KB");
  add("Least free stack of the main loop", String(loopStackLeast()) + " of " + getArduinoLoopTaskStackSize() + " bytes");
  {
    uint32_t low;
    int64_t at;
    uint16_t web, video;
    memoryDayLow(low, at, web, video);
    char when[24] = "";
    if (at) {
      time_t t = (time_t)at;
      struct tm local;
      localtime_r(&t, &local);
      strftime(when, sizeof(when), " at %H:%M", &local);
    }
    // Since the daily check, which starts a new day's; on a hub without one, since the start, as the row above, but
    // with when and what was open.
    String value = String(low / 1024) + " KB" + when + ", with " + web + " web and " + video + " video connections open";
    if (dayStarted)
      add("Lowest free memory since the daily check", value);
    else
      add("When the free memory was lowest", value);
  }
  add("Largest free block", String(largestBlock() / 1024) + " KB");
  add("Smallest largest free block", String(worstLargestBlock() / 1024) + " KB since the start, checked each second");
  HeapPieces pieces;
  heap_caps_walk(CHIP_MEMORY, countPiece, &pieces);  // the chip's own memory, as the rows above
  add("Free memory pieces", String(pieces.freeCount) + "; the largest " + pieceList(pieces.freeTop));
  add("Memory in use", String(pieces.usedCount) + " pieces, " + pieces.usedFrom1k + " of 1 KB or more; the largest " + pieceList(pieces.usedTop));
  add("Memory in use by size", String("under 64 B: ") + pieces.bucketCount[0] + " = " + pieces.bucketBytes[0] / 1024 +
                                   " KB; 64-255 B: " + pieces.bucketCount[1] + " = " + pieces.bucketBytes[1] / 1024 +
                                   " KB; 256 B-1 KB: " + pieces.bucketCount[2] + " = " + pieces.bucketBytes[2] / 1024 +
                                   " KB; 1-4 KB: " + pieces.bucketCount[3] + " = " + pieces.bucketBytes[3] / 1024 +
                                   " KB; larger: " + pieces.bucketCount[4] + " = " + pieces.bucketBytes[4] / 1024 + " KB");
  uint32_t shortTimes = status.memoryFailures;
  add("Work dropped for lack of memory", String((unsigned)shortTimes) + (shortTimes ? " times since the start; the hub kept running" : ""));
  add("ESP-NOW messages dropped while busy", String((unsigned)status.espNowDropped) + (status.espNowDropped ? ", which their sensors resend" : ""));
  nvs_stats_t nvs;
  if (nvs_get_stats(nullptr, &nvs) == ESP_OK) add("Settings storage", String(nvs.used_entries) + " of " + nvs.total_entries + " entries used");
  add("WiFi network", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("Not connected"));
  add("WiFi signal", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : String("-"));
  add("IP address", WiFi.localIP().toString());
  add("MAC address", WiFi.macAddress());
}
