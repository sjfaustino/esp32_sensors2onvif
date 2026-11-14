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
void sampleMemory();  // each pass of the main loop: keeps the worst the free memory has been cut up

// What malloc can really hand out. ESP.getMaxAllocHeap() also counts instruction RAM, which strings cannot use.
uint32_t largestBlock();
uint32_t worstLargestBlock();  // the smallest largestBlock() has been since the start, checked each second
std::string macSerial();

size_t crashSize();
bool readCrash(size_t offset, char* out, size_t len);
bool deleteCrash();
