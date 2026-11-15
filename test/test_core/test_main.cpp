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

int main() {
  UNITY_BEGIN();
  runSensorsTests();
  runMessagesTests();
  runCryptoTests();
  runOnvifTests();
  runStorageTests();
  runTextTests();
  return UNITY_END();
}
