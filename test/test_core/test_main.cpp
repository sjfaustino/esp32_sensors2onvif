// The hub's unit tests: pio test -e native. Each file holds one subject's tests.
#include "tests.h"

void setUp() {}
void tearDown() {}

void runSensorsTests();
void runMessagesTests();
void runCryptoTests();
void runOnvifTests();
void runStorageTests();
void runTextTests();
void runAlarmTests();
void runRestartTests();
void runBoardTests();

int main() {
  UNITY_BEGIN();
  runSensorsTests();
  runMessagesTests();
  runCryptoTests();
  runOnvifTests();
  runStorageTests();
  runTextTests();
  runAlarmTests();
  runRestartTests();
  runBoardTests();
  return UNITY_END();
}
