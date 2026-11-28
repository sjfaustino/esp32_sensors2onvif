// Device Info, the memory survey behind it, and the crash report the chip saves.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "cam_json.h"

// What Device Info shows that only the main loop knows.
struct DeviceStatus {
  bool firmwareUnproven;
  const char* lastRestart;
  uint32_t memoryFailures;
  uint32_t espNowDropped;
};

void deviceInfoBegin();  // once, at start: measures the firmware and reads any saved crash report
void deviceRows(CamJson& j, const DeviceStatus& status);
// Each pass of the main loop: keeps the worst the free memory has been cut up, and the least it has been since the last
// daily check, with the web and video connections open then.
void sampleMemory(int webOpen, int videoOpen);
void memoryDayLow(uint32_t& lowFree, int64_t& lowAt, uint16_t& lowWeb, uint16_t& lowVideo);
void memoryDayStart();
uint32_t memoryLowTaken();       // the least free memory since the last call
uint32_t memoryLowSinceStart();  // and since the start
uint32_t loopStackLeast();       // the least the main loop's stack has had free since the start, in bytes

// The largest piece of the chip's own memory malloc can hand out. ESP.getMaxAllocHeap() also counts instruction RAM,
// which strings cannot use. PSRAM is apart, as its 8 MB in one piece would hide how cut up the chip's memory is.
uint32_t largestBlock();
uint32_t largestAnyBlock();    // and of all the memory, PSRAM too, which takes the allocations of 4 KB and more
uint32_t worstLargestBlock();  // the smallest largestBlock() has been since the start, checked each second
std::string macSerial();

size_t crashSize();
bool readCrash(size_t offset, char* out, size_t len);
bool deleteCrash();
