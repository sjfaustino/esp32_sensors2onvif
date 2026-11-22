// Every parser of what reaches the hub from outside, under libFuzzer with AddressSanitizer and UndefinedBehavior
// Sanitizer: the radio's messages, the files kept on storage, HTTP's headers and logins, ONVIF's XML, MQTT, and what a
// backup restores. The first byte picks the parser and the rest is its input. A parser may refuse anything; it must
// never read out of bounds, overflow, or crash. tools/fuzz.sh builds and runs it, on Linux with clang, as CI does;
// tools/fuzz.ps1 does the same on Windows.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "cam_auth.h"
#include "cam_board_rules.h"
#include "cam_events.h"
#include "s2o_fw_sig.h"
#include "cam_frames.h"
#include "cam_mqtt.h"
#include "cam_node_files.h"
#include "cam_pairing.h"
#include "cam_sensors.h"
#include "cam_soap.h"
#include "cam_storage.h"
#include "s2o_messages.h"

static const uint8_t MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t KEY[] = "0123456789abcdef0123456789abcdef";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return 0;
  const uint8_t pick = data[0];
  // The input as its own block, so reading one byte past it is caught; and as text.
  std::vector<uint8_t> in(data + 1, data + size);
  const uint8_t* d = in.data();
  int len = (int)in.size();
  std::string text(in.begin(), in.end());
  S2oKey key{KEY, 32};

  switch (pick % 31) {
    // ---- the radio: messages from nodes, and on the node, from the hub ----
    case 0: {
      S2oReport r;
      s2oReadReport(d, len, r);
      break;
    }
    case 1: {
      S2oInfo i;
      s2oReadInfo(d, len, i);
      break;
    }
    case 2: {
      uint8_t inputs, pub[S2O_X25519_LEN];
      s2oReadHello(d, len, inputs, pub);
      break;
    }
    case 3: {
      uint8_t state, channel, pub[S2O_X25519_LEN];
      s2oReadAnswer(d, len, state, channel, pub);
      break;
    }
    case 4: {
      uint32_t image, offset;
      s2oReadAsk(d, len, image, offset);
      S2oPart part;
      if (s2oReadPart(d, len, part)) {
        volatile uint8_t sum = 0;
        for (int i = 0; i < part.len; i++) sum += part.data[i];  // the part's bytes must be inside the message
      }
      break;
    }
    case 5: {
      uint8_t out[S2O_SEALED_MAX_INNER];
      s2oOpen(key, MAC, d, len, pick & 0x80, out, sizeof(out));
      break;
    }
    case 6: {
      S2oReport r;
      r.input = 1;
      S2oForNode e;
      s2oReadConfirmation(d, len, r, e);
      break;
    }
    case 7: {  // the hub's whole checks of a node's message, signature and all
      SensorReport r;
      camCheckPacket(MAC, d, len, std::string((const char*)KEY, 32), r);
      CamFirmwareAsk ask;
      camCheckFirmwareAsk(MAC, d, len, std::string((const char*)KEY, 32), ask);
      CamNodeInfo info;
      camCheckNodeInfo(MAC, d, len, std::string((const char*)KEY, 32), info);
      break;
    }
    case 8: {
      uint8_t inputs, pub[CAM_X25519_LEN];
      camParseHello(d, len, inputs, pub);
      break;
    }
    // ---- files the hub keeps, which a restore or a damaged card can bring back changed ----
    case 9: {
      CamSensorSettings s;
      if (camParseSensorSettings(text, s)) camSensorSettingsText(s);
      break;
    }
    case 10: {
      CamNodeSettings s;
      if (camParseNodeSettings(text, s)) camNodeSettingsText(s);
      break;
    }
    case 11: {
      CamNodeInfo i;
      camParseNodeInfo(text, i);
      break;
    }
    case 12: {
      std::vector<CamBatteryPoint> points;
      if (camParseBatteryLog(text, points)) camBatteryDaysLeft(points);
      break;
    }
    case 13: {
      uint8_t pub[CAM_X25519_LEN], k[CAM_NODE_KEY_LEN];
      camParsePairing(text, pub, k);
      break;
    }
    case 14: {
      if (in.size() >= CAM_EVENT_RECORD) {
        CamEvent e;
        if (camEventDecode(d, e)) camEventCsvLine(e);
      }
      break;
    }
    case 15: {
      CamNal nals[3];
      camParseFrame(d, in.size(), nals);
      break;
    }
    case 16: camClassifyPath(text); break;
    // ---- HTTP: headers, logins, addresses ----
    case 17:
      headerValue(text, "content-length");
      headerValue(text, "authorization");
      queryParam(text, "path");
      queryParam(text, "token");
      break;
    case 18:
      camBasicValid(text, "admin", "password1");
      camAuthParam(text, "nonce");
      camDigestValid(text, "DESCRIBE", "admin", "password1", "SensorCam", "abcdef");
      break;
    case 19: {
      HttpUrl url;
      parseHttpUrl(text, url);
      break;
    }
    case 20: camBase64Decode(text); break;
    case 21:
      camHostListValid(text);
      camKnownHost(text, "192.168.1.20", "esp-alarmhub.local", "192.168.4.1", "hub.lan,alarm.lan");
      break;
    // ---- ONVIF's XML ----
    case 22:
      xmlFirstText(text, "Password");
      xmlFirstText(text, "Nonce");
      soapConsumerAddress(text);
      break;
    case 23: camWsseValid(text, "admin", "password1"); break;
    // ---- MQTT, from the broker ----
    case 24: {
      long n = camMqttPacketSize(d, in.size());
      CamMqttMessage m;
      if (n > 0 && (size_t)n <= in.size()) camMqttReadPublish(d, (size_t)n, m);
      break;
    }
    // ---- a restore: each line's key and value as the backup holds them ----
    case 25: {
      size_t eq = text.find('=');
      camSettingValid(text.substr(0, eq), eq == std::string::npos ? std::string() : text.substr(eq + 1));
      break;
    }
    case 26: {
      uint8_t mac[6];
      camParseMac(text, mac);
      uint8_t hex[32];
      camParseHex(text, hex, sizeof(hex));
      break;
    }
    case 27: {  // what is kept through a restart, as a power cut can leave it
      if (in.size() >= sizeof(CamKept)) {
        CamKept k;
        memcpy(&k, d, sizeof(k));
        camReadKept(k, pick & 0x80);
        camRestartText(k.cause);
      }
      break;
    }
    case 28: {  // a firmware update's signature, in pieces of the size the first byte gives, and a size said that may be wrong
      if (in.size() < 2) break;
      S2oFirmwareSig s;
      size_t said = in.size() - 2 + (in[1] & 3) - 1;  // one short, right, one over, or two over
      size_t piece = in[0] % 97 + 1;
      if (!s.begin(said)) break;
      for (size_t at = 2; at < in.size(); at += piece) s.take(d + at, in.size() - at < piece ? in.size() - at : piece);
      uint8_t publicKey[32];
      s2oFirmwareKey(text.substr(2, 64), publicKey) || (memset(publicKey, 0, 32), true);
      s.good(publicKey);
      break;
    }
    case 29: {  // a node's one file, its settings and what it said
      CamNodeFile f;
      if (camParseNodeFile(text, f)) camNodeFileText(f);
      break;
    }
    case 30: {  // a sensor's one file, its name and settings
      CamSensorFile f;
      if (camParseSensorFile(text, f)) camSensorFileText(f);
      break;
    }
  }
  return 0;
}
