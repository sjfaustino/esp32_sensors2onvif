// For a computer without libFuzzer: runs fuzz_parsers.cpp's target on each file given, then on as many randomly changed
// copies of them as asked. It finds far less than libFuzzer, which follows the code it reaches, but shows the target
// builds and runs. tools/fuzz.sh uses libFuzzer where it can.
//   standalone [-random N] file...
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int main(int argc, char** argv) {
  long rounds = 0;
  std::vector<std::vector<uint8_t>> seeds;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-random") && i + 1 < argc) {
      rounds = atol(argv[++i]);
      continue;
    }
    FILE* f = fopen(argv[i], "rb");
    if (!f) continue;
    std::vector<uint8_t> d;
    int ch;
    while ((ch = fgetc(f)) != EOF) d.push_back((uint8_t)ch);
    fclose(f);
    LLVMFuzzerTestOneInput(d.data(), d.size());
    seeds.push_back(d);
  }
  if (seeds.empty()) seeds.push_back({0});
  srand(12345);
  for (long r = 0; r < rounds; r++) {
    std::vector<uint8_t> d = seeds[rand() % seeds.size()];
    int changes = 1 + rand() % 8;
    for (int c = 0; c < changes; c++) {
      int how = rand() % 4;
      size_t at = d.empty() ? 0 : 1 + rand() % (d.size() > 1 ? d.size() - 1 : 1);  // the first byte picks the parser
      if (how == 0 && at < d.size()) d[at] = (uint8_t)rand();
      if (how == 1 && at < d.size()) d.erase(d.begin() + at);
      if (how == 2) d.insert(d.begin() + (at < d.size() ? at : d.size()), (uint8_t)rand());
      if (how == 3 && d.size() > 1) d.resize(1 + rand() % d.size());
    }
    LLVMFuzzerTestOneInput(d.data(), d.size());
  }
  printf("%zu files and %ld changed copies, no fault\n", seeds.size(), rounds);
  return 0;
}
