// PC build of the firmware library for tools/e2e_test.py. Binds to loopback only.
// Usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD]
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include "cam_server.h"

static uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: program HOLD_MS STORAGE_FOLDER [WEB_PASSWORD]\n");
    return 1;
  }
  CamConfig cfg;
  cfg.loopbackOnly = true;
  cfg.onvifPort = 18899;
  cfg.rtspPort = 18554;
  cfg.discoveryPort = 13702;
  cfg.serial = "0123456789ab";
  cfg.holdMs = (uint32_t)atol(argv[1]);
  cfg.fsRoot = argv[2];
  if (argc > 3) cfg.webPassword = argv[3];
  SensorCamServer server(cfg, [] { return std::string("127.0.0.1"); }, nowMs);
  if (!server.begin()) {
    fprintf(stderr, "could not bind test ports\n");
    return 1;
  }
  server.networkUp();
  printf("ready\n");
  fflush(stdout);
  for (;;) server.poll(20);
}
