// The ESP-NOW messages, through the hub's names for them, and the parsers of everything else that arrives from outside.
#include "tests.h"

static void signed_messages_are_checked() {
  const uint8_t mac[6] = {0x84, 0xF7, 0x03, 0xDF, 0x03, 0x62};
  uint8_t p[17 + 8] = {2, 1, 1, 90, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  std::string sig = camPacketSignature(mac, p, 17, "key-1");
  memcpy(p + 17, sig.data(), 8);
  SensorReport r;
  TEST_ASSERT_EQUAL(CAM_PACKET_SIGNED, camCheckPacket(mac, p, sizeof(p), "key-1", r));
  TEST_ASSERT_EQUAL(0x01020304u, r.counter);
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "key-2", r));
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "", r));
  const uint8_t other[6] = {0x84, 0xF7, 0x03, 0xDF, 0x03, 0x63};
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(other, p, sizeof(p), "key-1", r));  // copied to another node
  p[3] = 10;                                                                                     // altered after signing
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckPacket(mac, p, sizeof(p), "key-1", r));
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckPacket(mac, p, 13, "key-1", r));  // without its signature
  p[0] = 1;
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckPacket(mac, p, sizeof(p), "key-1", r));  // not a report
}

static void node_info_is_read_signed_and_kept() {
  const uint8_t mac[6] = {2, 0, 0, 0, 0, 9};
  const std::string key = "node-key";
  // every 7200 s, saving, a brownout, 3 restarts, 180 ms, an ESP32-C6, refused image 5, then the texts
  uint8_t m[80] = {CAM_NODE_INFO, 0x20, 0x1C, 0, 0, 1, CAM_RESTART_BROWNOUT, 3, 0, 180, 0, 13, 0, 5, 0, 0, 0, 3, 'P', 'I', 'R', 2, 'v', '1'};
  int len = 24;
  memcpy(m + len, camPacketSignature(mac, m, len, key).data(), CAM_SIGNATURE_LEN);
  CamNodeInfo info;
  TEST_ASSERT_EQUAL(CAM_PACKET_SIGNED, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN, key, info));
  TEST_ASSERT_EQUAL(7200, info.everyS);
  TEST_ASSERT_TRUE(info.saving);
  TEST_ASSERT_EQUAL(CAM_RESTART_BROWNOUT, info.restart);
  TEST_ASSERT_TRUE(camRestartIsFault(info.restart));
  TEST_ASSERT_EQUAL_STRING("brownout", camRestartName(info.restart));
  TEST_ASSERT_EQUAL(3, info.restarts);
  TEST_ASSERT_EQUAL(180, info.responseMs);
  TEST_ASSERT_EQUAL_STRING("ESP32-C6", camChipName(info.chip).c_str());
  TEST_ASSERT_EQUAL(5, info.refused);
  TEST_ASSERT_EQUAL_STRING("PIR", info.sensor.c_str());
  TEST_ASSERT_EQUAL_STRING("v1", info.version.c_str());
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN, "other", info));
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN - 1, key, info));  // a text runs past the end
  m[17] = 30;                                                                                               // longer than a text may be
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN, key, info));
  m[17] = 3, m[19] = '\n';
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN, key, info));
  m[19] = 'I', m[6] = CAM_RESTART_KINDS;  // a reason this hub does not know
  memcpy(m + len, camPacketSignature(mac, m, len, key).data(), CAM_SIGNATURE_LEN);
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckNodeInfo(mac, m, len + CAM_SIGNATURE_LEN, key, info));

  CamNodeInfo back;
  info.sensor = "PIR";
  TEST_ASSERT_TRUE(camParseNodeInfo(camNodeInfoText(info), back));
  TEST_ASSERT_TRUE(back == info);
  TEST_ASSERT_FALSE(camParseNodeInfo("colour=red\n", back));

  TEST_ASSERT_EQUAL(0, camAutoSilentS(0));
  TEST_ASSERT_EQUAL(300 + 600, camAutoSilentS(300));     // at least 10 minutes over
  TEST_ASSERT_EQUAL(7200 + 1800, camAutoSilentS(7200));  // or a quarter over
  TEST_ASSERT_EQUAL(CAM_MAX_SILENT_S, camAutoSilentS(30 * 86400));
}

static void battery_outside_0_to_100_is_unknown() {
  uint8_t p[] = {2, 2, 1, 100, 4, 3, 2, 1, 44, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // the last 8 stand for the signature
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(100, r.battery);
  p[3] = 101;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, r.battery);
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, camBatteryPercent(300));
  TEST_ASSERT_EQUAL(CAM_BATTERY_UNKNOWN, camBatteryPercent(-1));
  TEST_ASSERT_EQUAL(0, camBatteryPercent(0));
}

static void parses_report_with_no_targets() {
  // a report, input, active, battery, counter 0x01020304, age 300, fresh 0x0A0B0C0D, no targets, then 8 bytes for the
  // signature, unchecked here
  const uint8_t p[] = {2, 2, 1, 80, 4, 3, 2, 1, 44, 1, 0, 0, 0x0D, 0x0C, 0x0B, 0x0A, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(2, r.input);
  TEST_ASSERT_TRUE(r.active);
  TEST_ASSERT_EQUAL(80, r.battery);
  TEST_ASSERT_EQUAL_UINT32(0x01020304, r.counter);
  TEST_ASSERT_EQUAL_UINT32(300, r.age);
  TEST_ASSERT_EQUAL_UINT32(0x0A0B0C0D, r.fresh);
  TEST_ASSERT_EQUAL(0, r.targetCount);
}

static void parses_three_targets_and_picks_nearest() {
  const uint8_t p[] = {2,    1,    1, 0xFF, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0x90, 0x01, 1,  // 400 cm, approaching
                       0x96, 0x00, 2,                                                              // 150 cm, moving away
                       0xFF, 0xFF, 3,                                                              // unknown distance
                       0,    0,    0, 0,    0, 0, 0, 0};                                           // the signature, unchecked here
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(p, sizeof(p), r));
  TEST_ASSERT_EQUAL(3, r.targetCount);
  TEST_ASSERT_EQUAL_UINT16(400, r.targets[0].distanceCm);
  TEST_ASSERT_EQUAL_UINT16(150, r.targets[1].distanceCm);
  TEST_ASSERT_EQUAL_UINT16(CAM_DISTANCE_UNKNOWN, r.targets[2].distanceCm);
  TEST_ASSERT_EQUAL(2, reportDirection(r));
}

static void rejects_malformed_reports() {
  const uint8_t good[] = {2, 1, 1, 80, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 10, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  SensorReport r;
  TEST_ASSERT_TRUE(parseSensorPacket(good, sizeof(good), r));
  uint8_t bad[sizeof(good)];
  struct {
    int index;
    uint8_t value;
  } changes[] = {{0, 1}, {0, 3}, {1, 0}, {2, 2}, {16, 4}, {16, 2}, {19, 5}};
  for (auto& c : changes) {
    memcpy(bad, good, sizeof(good));
    bad[c.index] = c.value;
    TEST_ASSERT_FALSE(parseSensorPacket(bad, sizeof(bad), r));
  }
  TEST_ASSERT_FALSE(parseSensorPacket(good, sizeof(good) - 1, r));
  TEST_ASSERT_FALSE(parseSensorPacket(good, 5, r));
}

static void confirmation_is_signed_for_that_sensor() {
  uint8_t mac[6] = {2, 0, 0, 0, 0, 1}, other[6] = {2, 0, 0, 0, 0, 2};
  SensorReport r;
  r.input = 2;
  r.active = true;
  r.counter = 0x01020304;
  CamForNode e;
  e.hubTime = 0x0A0B0C0D;
  e.everyS = 3600;
  e.repeatS = 4;
  e.walkUntil = 0x0A0B0C0D + 600;
  e.update = true;
  e.prove = true;
  e.fresh = 0x5EED0001;
  std::string c = camConfirmation(mac, r, e, "key");
  TEST_ASSERT_EQUAL(CAM_CONFIRMATION_LEN, (int)c.size());
  TEST_ASSERT_EQUAL_MEMORY("\x03\x02\x01\x04\x03\x02\x01\x0D\x0C\x0B\x0A\x10\x0E\x00\x00\x04\x00\x65\x0E\x0B\x0A\x03\x01\x00\xED\x5E", c.data(),
                           CAM_CONFIRMATION_BODY);
  TEST_ASSERT_TRUE(c.substr(CAM_CONFIRMATION_BODY) == camPacketSignature(mac, (const uint8_t*)c.data(), CAM_CONFIRMATION_BODY, "key"));
  TEST_ASSERT_FALSE(c.substr(CAM_CONFIRMATION_BODY) == camPacketSignature(other, (const uint8_t*)c.data(), CAM_CONFIRMATION_BODY, "key"));
}

// The expected bytes were worked out in Python from the description in docs/protocol.md.
static void sealed_messages_hide_what_they_say() {
  const uint8_t mac[6] = {2, 0, 0, 0, 0, 0x55};
  const std::string key(20, '\x22');
  const uint8_t report[17] = {CAM_REPORT, 1, 1, 80, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t sealed[64], opened[64];
  int len = camSeal(mac, 0x11223344, report, sizeof(report), key, false, sealed);
  TEST_ASSERT_EQUAL(CAM_SEALED_EXTRA + 17 + CAM_SIGNATURE_LEN, len);
  char hex[2 * 64 + 1] = "";
  for (int i = 0; i < len; i++) snprintf(hex + 2 * i, 3, "%02x", sealed[i]);
  TEST_ASSERT_EQUAL_STRING("0744332211bd9133a3e584d49750a42c9ee767185a68f21fe3b7906fbb0b", hex);
  int n = camOpenSealed(mac, sealed, len, key, false, opened, sizeof(opened));
  TEST_ASSERT_EQUAL(17 + CAM_SIGNATURE_LEN, n);
  SensorReport r;
  TEST_ASSERT_EQUAL(CAM_PACKET_SIGNED, camCheckPacket(mac, opened, n, key, r));  // reads as if it had been sent plain
  TEST_ASSERT_EQUAL(7, r.counter);
  TEST_ASSERT_EQUAL(n, camOpenSealed(mac, sealed, len, key, true, opened, sizeof(opened)));
  TEST_ASSERT_NOT_EQUAL(0, memcmp(opened, report, 17));  // read as from the hub, the stream differs
  sealed[6] ^= 1;
  TEST_ASSERT_EQUAL(-1, camOpenSealed(mac, sealed, len, key, false, opened, sizeof(opened)));
  TEST_ASSERT_EQUAL(0, camOpenSealed(mac, report, sizeof(report), key, false, opened, sizeof(opened)));  // not sealed
}

static void firmware_goes_in_signed_parts() {
  const uint8_t mac[6] = {2, 0, 0, 0, 0, 0x55};
  const std::string key = "node-key";
  uint8_t ask[CAM_FIRMWARE_ASK_LEN] = {CAM_FIRMWARE_ASK, 9, 0, 0, 0, 0x90, 1, 0, 0};
  memcpy(ask + 9, camPacketSignature(mac, ask, 9, key).data(), CAM_SIGNATURE_LEN);
  CamFirmwareAsk a;
  TEST_ASSERT_EQUAL(CAM_PACKET_SIGNED, camCheckFirmwareAsk(mac, ask, sizeof(ask), key, a));
  TEST_ASSERT_EQUAL(9, a.image);
  TEST_ASSERT_EQUAL(400, a.offset);
  TEST_ASSERT_EQUAL(CAM_PACKET_BAD_SIGNATURE, camCheckFirmwareAsk(mac, ask, sizeof(ask), "other", a));
  TEST_ASSERT_EQUAL(CAM_PACKET_INVALID, camCheckFirmwareAsk(mac, ask, sizeof(ask) - 1, key, a));
  uint8_t data[CAM_FIRMWARE_PART_MAX + 10], out[CAM_FIRMWARE_PART_LEN_MAX];
  memset(data, 0xAB, sizeof(data));
  int len = camFirmwarePart(mac, 9, 1000, 400, data, sizeof(data), key, out);
  TEST_ASSERT_EQUAL(CAM_FIRMWARE_PART_LEN_MAX, len);  // cut to the most a part holds
  TEST_ASSERT_EQUAL_MEMORY("\x09\x09\x00\x00\x00\xE8\x03\x00\x00\x90\x01\x00\x00", out, CAM_FIRMWARE_PART_HEAD);
  TEST_ASSERT_TRUE(std::string((const char*)out + len - CAM_SIGNATURE_LEN, CAM_SIGNATURE_LEN) ==
                   camPacketSignature(mac, out, len - CAM_SIGNATURE_LEN, key));
}

// Every parser of what arrives from outside, fed seeds and many changed copies of them: none may throw, which on the
// board would stop the hub, or read past what it was given.
static void parsers_survive_any_input() {
  uint32_t seed = 12345;
  auto next = [&]() {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
  };
  const std::string seeds[] = {
      "<s:Envelope><s:Header><wsse:Security><wsse:UsernameToken><wsse:Username>admin</wsse:Username><wsse:Password Type=\"x#PasswordDigest\">"
      "abc=</wsse:Password><wsse:Nonce>bm9uY2U=</wsse:Nonce><wsu:Created>2026-01-01T00:00:00Z</wsu:Created></wsse:UsernameToken></wsse:Security>"
      "</s:Header><s:Body><tev:Subscribe><wsnt:ConsumerReference><wsa:Address>http://10.0.0.5:8080/e</wsa:Address></wsnt:ConsumerReference>"
      "<wsnt:InitialTerminationTime>PT1M</wsnt:InitialTerminationTime></tev:Subscribe></s:Body></s:Envelope>",
      "GET /api/file?path=AA-BB-CC-DD-EE-01/1/active.h264&x=%41%zz HTTP/1.1\r\nHost: 10.0.0.2:80\r\nAuthorization: Basic YWRtaW46cHc=\r\n"
      "Content-Length: 12\r\nOrigin: http://10.0.0.2\r\n\r\n",
      "Digest username=\"admin\", realm=\"x\", nonce=\"abc\", uri=\"/\", response=\"0123\"",
      "late=45\nsilent=600\nevery=3600\nrepeat=4\n",
      "hold=10\nquiet=60-120\nzone=interior\n",
      "every=7200\nsaving=0\nrestart=1\nrestarts=0\nchip=13\nrefused=0\nsensor=RCWL-0516\nversion=3f2a1b0\n",
      "1800000000 80\n1800086400 78\n",
      "public=7b4e909bbe7ffe44c465a220037d608ee35897d31ef972f07f74892cb0f73f13\nkey=88f21f0c93e2f07a7bf6d9d98db1c593e73ae3f7\n",
      std::string("\x02\x01\x01\x50\x07\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x90\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00", 28),
      std::string("\x06\x20\x1c\x00\x00\x01\x06\x03\x00\xb4\x00\x0d\x00\x05\x00\x00\x00\x03PIR\x02v1\x00\x00\x00\x00\x00\x00\x00\x00", 32),
      std::string("\x30\x0f\x00\x05topic{\"a\":\"ARM\"}", 17),
      std::string("\x00\x00\x00\x01\x67\x42\xc0\x1e\xd9\x00\xa0\x2f\xf9\x61\x00\x00\x00\x01\x68\xce\x3c\x80\x00\x00\x00\x01\x65\x88\x84", 29),
  };
  uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
  int thrown = 0;
  for (const std::string& base : seeds) {
    for (int round = 0; round < 3000; round++) {
      std::string in = base;
      int changes = 1 + (int)(next() % 4);
      for (int c = 0; c < changes && !in.empty(); c++) {
        size_t at = next() % in.size();
        switch (next() % 5) {
          case 0: in[at] = (char)next(); break;
          case 1: in.erase(at, 1 + next() % 8); break;
          case 2: in.insert(at, 1, "<>&;:=/\"'\r\n\x00\xff"[next() % 14]); break;
          case 3: in.resize(at); break;
          default: in.insert(at, in.substr(next() % in.size(), next() % 16)); break;
        }
      }
      try {
        soapAction(in);
        soapConsumerAddress(in);
        xmlFirstText(in, "Password");
        camWsseValid(in, "admin", "pw");
        HttpUrl url;
        parseHttpUrl(in, url);
        headerValue(in, "authorization");
        queryParam(in, "path");
        camAuthParam(in, "nonce");
        camBase64Decode(in);
        camKnownHost(in, "10.0.0.2", "hub.local", "192.168.4.1", "a.lan,b");
        camClassifyPath(in);
        uint8_t m[6];
        camParseMac(in, m);
        CamNodeSettings node;
        camParseNodeSettings(in, node);
        CamSensorSettings sensor;
        camParseSensorSettings(in, sensor);
        CamNodeInfo info;
        camParseNodeInfo(in, info);
        std::vector<CamBatteryPoint> points;
        camParseBatteryLog(in, points);
        uint8_t pub[32], key[20];
        camParsePairing(in, pub, key);
        camBackupValueValid("net.web", in);
        const uint8_t* d = (const uint8_t*)in.data();
        int len = (int)in.size();
        SensorReport r;
        camCheckPacket(mac, d, len, "k", r);
        camCheckNodeInfo(mac, d, len, "k", info);
        CamFirmwareAsk ask;
        camCheckFirmwareAsk(mac, d, len, "k", ask);
        uint8_t opened[CAM_SEALED_MAX_INNER + CAM_SIGNATURE_LEN];
        camOpenSealed(mac, d, len, "k", false, opened, sizeof(opened));
        uint8_t inputs;
        camParseHello(d, len, inputs, pub);
        camMqttPacketSize(d, in.size());
        CamMqttMessage msg;
        camMqttReadPublish(d, in.size(), msg);
        CamNal nals[3];
        camParseFrame(d, in.size(), nals);
        S2oPart part;
        s2oReadPart(d, len, part);
        uint8_t state, channel;
        s2oReadAnswer(d, len, state, channel, pub);
        S2oForNode forNode;
        s2oReadConfirmation(d, len, r, forNode);
        S2oInfo s2oInfo;
        s2oReadInfo(d, len, s2oInfo);
        s2oOpen({(const uint8_t*)"k", 1}, mac, d, len, true, opened, sizeof(opened));
      } catch (...) {
        thrown++;
      }
    }
  }
  TEST_ASSERT_EQUAL(0, thrown);
}

// A node's hardware: in its info and in the hub's answer, the confirmation's flag for it, and the pins each chip allows.
static void a_nodes_hardware_travels_and_is_checked() {
  S2oHardware hw;
  hw.number = 7;
  hw.sensor = S2O_SENSOR_HC_SR501;
  hw.sensorPin = 3;
  hw.tamperPin = 4;
  hw.batteryPin = 1;
  hw.cell = S2O_CELL_LITHIUM;
  uint8_t key[20], mac[6] = {2, 0, 0, 0, 0, 9}, msg[S2O_HARDWARE_LEN];
  memset(key, 0x31, sizeof(key));
  S2oKey k{key, sizeof(key)};
  s2oHardwareMessage(k, mac, hw, msg);
  S2oHardware back;
  TEST_ASSERT_TRUE(s2oSignatureOk(k, mac, msg, sizeof(msg)));
  TEST_ASSERT_TRUE(s2oReadHardware(msg, S2O_HARDWARE_BODY, back));
  TEST_ASSERT_TRUE(back == hw);
  msg[5] = S2O_SENSOR_KINDS;  // a sensor this code does not know
  TEST_ASSERT_FALSE(s2oReadHardware(msg, S2O_HARDWARE_BODY, back));
  uint8_t ask[S2O_HARDWARE_ASK_LEN];
  s2oHardwareAsk(k, mac, ask);
  TEST_ASSERT_EQUAL(S2O_HARDWARE_ASK, ask[0]);
  TEST_ASSERT_TRUE(s2oSignatureOk(k, mac, ask, sizeof(ask)));

  // In the info: after the texts, and read back; an info from before hardware settings has none, and still reads.
  S2oInfo info;
  s2oSetText(info.sensor, "HC-SR501");
  s2oSetText(info.version, "v0.2.0");
  uint8_t body[S2O_INFO_MAX];
  int plain = s2oInfoBody(info, body);
  S2oInfo read;
  TEST_ASSERT_TRUE(s2oReadInfo(body, plain, read));
  TEST_ASSERT_FALSE(read.hasHardware);
  info.hasHardware = true;
  info.hardware = hw;
  int with = s2oInfoBody(info, body);
  TEST_ASSERT_EQUAL(plain + S2O_HARDWARE_FIELDS, with);
  TEST_ASSERT_TRUE(s2oReadInfo(body, with, read));
  TEST_ASSERT_TRUE(read.hasHardware && read.hardware == hw);
  TEST_ASSERT_FALSE(s2oReadInfo(body, with - 1, read));  // cut short

  // The confirmation's flag.
  S2oReport r;
  S2oForNode e, got;
  e.hardware = true;
  uint8_t conf[S2O_CONFIRMATION_BODY];
  s2oConfirmationBody(r, e, conf);
  TEST_ASSERT_TRUE(s2oReadConfirmation(conf, sizeof(conf), r, got) && got.hardware && !got.update && !got.prove);

  // The pins: valid on a C6, refused where they cannot work.
  const char* why = nullptr;
  TEST_ASSERT_TRUE(s2oHardwareValid(hw, S2O_CHIP_ESP32C6, &why));
  TEST_ASSERT_NULL(why);
  S2oHardware bad = hw;
  bad.sensorPin = 9;  // the C6's BOOT button, which cannot wake it
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32C6, &why));
  TEST_ASSERT_NOT_NULL(why);
  bad = hw;
  bad.sensorPin = 2;  // the C3 reads GPIO 2 as it starts
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32C3, nullptr));
  bad = hw;
  bad.batteryPin = 7;  // not an ADC1 pin on a C6
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32C6, nullptr));
  bad = hw;
  bad.cell = S2O_CELL_NONE;  // a battery pin with no kind of battery
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32C6, nullptr));
  bad = hw;
  bad.tamperPin = bad.sensorPin;
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32C6, nullptr));
  bad = hw;
  bad.sensorPin = 4, bad.tamperPin = 13, bad.batteryPin = 34, bad.sensor = S2O_SENSOR_GENERIC_LOW;
  TEST_ASSERT_FALSE(s2oHardwareValid(bad, S2O_CHIP_ESP32, nullptr));  // a classic ESP32 wakes on all its pins one way
  bad.sensor = S2O_SENSOR_RCWL0516;
  TEST_ASSERT_TRUE(s2oHardwareValid(bad, S2O_CHIP_ESP32, nullptr));

  // As the hub keeps it, in settings.txt and about.txt.
  S2oHardware text;
  TEST_ASSERT_EQUAL_STRING("7,4,3,4,1,1", camHardwareText(hw).c_str());
  TEST_ASSERT_TRUE(camParseHardware("7,4,3,4,1,1", text) && text == hw);
  TEST_ASSERT_FALSE(camParseHardware("7,4,3,4,1", text));
  TEST_ASSERT_FALSE(camParseHardware("7,4,3,4,1,1,1", text));
  TEST_ASSERT_FALSE(camParseHardware("7,4,300,4,1,1", text));
  CamNodeSettings s;
  TEST_ASSERT_TRUE(camParseNodeSettings("every=600\nhw=7,4,3,4,1,1\n", s));
  TEST_ASSERT_TRUE(s.hwSet && s.hw == hw && s.everyS == 600);
  TEST_ASSERT_EQUAL_STRING("every=600\nhw=7,4,3,4,1,1\n", camNodeSettingsText(s).c_str());
}

// A node firmware tried on one node first reaches the others only a day after that node runs it.
static void a_node_firmware_tried_first_waits_a_day_for_the_others() {
  CamNodeImage f;
  TEST_ASSERT_TRUE(f.passed(0));  // no pilot: every node at once
  f.pilot = "02-00-00-00-00-55";
  TEST_ASSERT_FALSE(f.passed(1800000000));  // the pilot has not run it yet
  f.pilotRan = 1800000000;
  TEST_ASSERT_FALSE(f.passed(1800000000 + CAM_PILOT_S - 1));
  TEST_ASSERT_TRUE(f.passed(1800000000 + CAM_PILOT_S));
  TEST_ASSERT_FALSE(f.passed(0));  // without the hub's clock, never sooner
}

void runMessagesTests() {
  RUN_TEST(a_node_firmware_tried_first_waits_a_day_for_the_others);
  RUN_TEST(a_nodes_hardware_travels_and_is_checked);
  RUN_TEST(parsers_survive_any_input);
  RUN_TEST(confirmation_is_signed_for_that_sensor);
  RUN_TEST(sealed_messages_hide_what_they_say);
  RUN_TEST(firmware_goes_in_signed_parts);
  RUN_TEST(parses_report_with_no_targets);
  RUN_TEST(parses_three_targets_and_picks_nearest);
  RUN_TEST(rejects_malformed_reports);
  RUN_TEST(battery_outside_0_to_100_is_unknown);
  RUN_TEST(signed_messages_are_checked);
  RUN_TEST(node_info_is_read_signed_and_kept);
}
