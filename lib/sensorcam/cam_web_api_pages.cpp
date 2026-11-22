// The API's pages and files: the hub's page and the setup page, the Wi-Fi networks, the Sensors page's tree, the live
// picture, and the files on storage.
#include "cam_web_server.h"
#include "cam_node_files.h"
#include "cam_number.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cam_auth.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_soap.h"
#include "cam_web.h"
#include "cam_server_internal.h"
#include "cam_web_checks.h"

// Pictures may use the storage only up to what the event history still needs: its full size, both CSV files, and room
// to rewrite the saved files. Without that room no report could be saved, and so none could be confirmed.
size_t CamWeb::historyRoom() const {
  size_t logs = alarm_.log().bytesOnStorage();
  return HISTORY_ROOM + (logs < 2 * alarm_.log().fileMax() ? 2 * alarm_.log().fileMax() - logs : 0);
}

bool CamWeb::roomBesideHistory(size_t bytes) const {
  uint32_t used = 0, total = 0;
  if (!chip_->storageUsage(used, total)) return true;
  size_t needed = (bytes + 4095) / 4096 * 4096 + 2 * 4096 + historyRoom();  // whole blocks, the new copy beside the old, and its folder
  return total > used && total - used >= needed;
}

static bool hasFile(const std::vector<CamFsEntry>& items, const std::string& name) {
  for (const CamFsEntry& e : items)
    if (!e.dir && e.name == name) return true;
  return false;
}

// Built as one string that grows only when the memory for it is there, and is never copied.
// Empty when the hub is too short of memory to build it.
std::string CamWeb::treeJson() {
  uint32_t used = 0, total = 0;
  chip_->storageUsage(used, total);
  std::vector<CamFsEntry> root;
  storage_.list("", root);
  std::string out;
  auto makeRoom = [&](size_t bytes) {  // reserved in advance, so a failed allocation is a refusal and not a crash
    if (out.capacity() - out.size() >= bytes) return true;
    size_t wanted = out.capacity() * 2 + bytes;
    if (!roomFor(wanted)) return false;
    out.reserve(wanted);
    return true;
  };
  if (!makeRoom(128)) return std::string();
  bool clear = false;
  uint32_t clearSize = 0;
  for (const CamFsEntry& s : root) {
    if (s.dir || s.name != "clear.h264") continue;
    clear = true;
    clearSize = s.size;
  }
  CamJson j(out);
  j.object()
      .num("used", used)
      .num("total", total)
      .num("kept", historyRoom())
      .num("clear", clear, clearSize)
      .flag("clearPreview", hasFile(root, "clear.jpg"))
      .array("nodes");
  for (const CamFsEntry& s : root) {
    if (!s.dir || camClassifyPath(s.name) != CAM_PATH_NODE) continue;
    if (!makeRoom(320)) return std::string();  // the longest node entry before its sensors
    CamNodeSettings node = camReadNodeFile(storage_, s.name).settings;
    uint8_t mac[6];
    camParseMac(s.name, mac);
    int any = table_.findNode(mac);
    uint32_t every = any ? table_.nodeOf(any).info.everyS : 0;
    j.object()
        .str("mac", s.name)
        .flag("paired", storage_.exists(s.name + "/pairing.txt"))
        .num("late", node.lateS != 0, node.lateS)
        .num("silent", node.silentS != 0, node.silentS);
    j.num("every", every != 0, every).num("autoSilent", every != 0, camAutoSilentS(every));
    j.num("reportEvery", node.everyS != 0, node.everyS).num("repeat", node.repeatS != 0, node.repeatS);
    time_t t = chip_->wallTime();
    uint32_t walk = any && t >= CAM_CLOCK_VALID_AFTER && table_.nodeOf(any).walkUntil > (uint32_t)t ? table_.nodeOf(any).walkUntil - (uint32_t)t : 0;
    j.num("walkLeft", walk);
    // Its hardware: as set here, and as the node says it runs it, with its chip, which decides the pins it can use.
    const CamNodeInfo* info = any ? &table_.nodeOf(any).info : nullptr;
    j.num("chip", info && info->chip != CAM_CHIP_UNKNOWN, info ? info->chip : 0);
    j.str("hwSet", node.hwSet ? camHardwareText(node.hw) : std::string());
    j.str("hwRuns", info && info->hasHardware ? camHardwareText(info->hardware) : std::string());
    j.array("sensors");
    std::vector<CamFsEntry> inputs;
    storage_.list(s.name, inputs);
    for (const CamFsEntry& n : inputs) {
      std::string base = s.name + "/" + n.name;
      if (!n.dir || camClassifyPath(base) != CAM_PATH_SENSOR) continue;
      uint8_t input = 0;
      int slot = camNumber(n.name, input) ? table_.findSlot(mac, input) : 0;
      if (!slot) continue;                        // beyond the sensor limit, so it does nothing
      if (!makeRoom(1500)) return std::string();  // the longest possible entry: all ten pictures, and a name and a camera all escapes
      const CamSensorSettings& set = table_.slotOf(slot).settings;
      j.object().num("input", input).str("name", table_.slotOf(slot).name).num("hold", set.holdS != 0, set.holdS);
      j.flag("interior", set.interior).str("camera", set.camera);
      if (set.quietFrom >= 0)
        j.array("quiet").num(set.quietFrom).num(set.quietTo).end();
      else
        j.null("quiet");
      j.array("files");
      std::vector<CamFsEntry> items;
      storage_.list(base, items);
      for (const CamFsEntry& f : items) {
        if (f.dir || camClassifyPath(base + "/" + f.name) != CAM_PATH_FILE) continue;
        j.object().str("name", f.name).num("size", f.size).flag("preview", hasFile(items, camPreviewPath(f.name))).end();
      }
      j.end().end();
    }
    j.end().end();
  }
  j.finish();
  return out;
}

void CamWeb::getSetup(const ApiRequest& r) { sendBody(r.c, HTML_TYPE, (const char*)CAM_SETUP_PAGE, CAM_SETUP_PAGE_SIZE, PAGE_HEADERS); }

void CamWeb::getWifiScan(const ApiRequest& r) {
  r.c.out = jsonOk(jsonObject([&](CamJson& j) {
    radio_->wifiNetworks(j);
    j.flag("needPassword", webPassword_.empty());
  }));
}

void CamWeb::postWifi(const ApiRequest& r) {
  HttpConn& c = r.c;
  {
    // A blank hub's setup network is open, so whoever sets it up chooses the hub password in the same step. It then
    // guards the pages, ONVIF, the video and the setup network itself.
    std::string ssid = r.field("ssid"), password = r.field("password"), hubPassword = r.field("hubpw");
    bool needPassword = webPassword_.empty();
    if (ssid.empty() || ssid.size() > 32 || (!password.empty() && (password.size() < 8 || password.size() > 63)))
      c.out = jsonFail(400, "the name must be 1 to 32 characters and the password empty or 8 to 63");
    else if (needPassword && (hubPassword.size() < 8 || hubPassword.size() > 63))
      c.out = jsonFail(400, "choose a hub password of 8 to 63 characters");
    else if (needPassword && !store_->passwordSave(hubPassword))
      c.out = jsonFail(507, "could not save the hub password");
    else {
      if (needPassword) setPassword(hubPassword);  // guards the hub from now on, even if the network cannot be saved
      c.out = saved(radio_->wifiSave(ssid, password, r.field("role") == "backup"), "could not save the Wi-Fi settings");
    }
  }
}

void CamWeb::getRoot(const ApiRequest& r) {
  if (radio_->setupActive())
    getSetup(r);
  else
    getFiles(r);
}

void CamWeb::getFiles(const ApiRequest& r) { sendBody(r.c, HTML_TYPE, (const char*)CAM_WEB_PAGE, CAM_WEB_PAGE_SIZE, PAGE_HEADERS); }

void CamWeb::getTree(const ApiRequest& r) {
  HttpConn& c = r.c;
  c.body = treeJson();
  if (c.body.empty())
    c.out = jsonFail(507, "the hub is short of memory; try again");
  else
    sendBody(c, JSON_TYPE, c.body.data(), c.body.size());
}

void CamWeb::getLive(const ApiRequest& r) {
  HttpConn& c = r.c;
  video_.current(millis_());
  c.out = jsonOk(
      jsonObject([&](CamJson& j) { j.num("seq", video_.seq()).str("file", video_.file().empty() ? std::string("built-in") : video_.file()); }));
}

void CamWeb::getLiveFrame(const ApiRequest& r) {
  HttpConn& c = r.c;
  // Read from the picture as it is sent, each NAL after a start code. If the picture is replaced meanwhile, the reply
  // stops short and the page asks again.
  const CamFrame* frame = &video_.current(millis_());
  size_t size = 0;
  for (int i = 0; i < frame->count; i++) size += 4 + frame->nals[i].len;
  uint32_t gen = video_.gen();
  c.reader = [this, frame, gen](size_t offset, char* out, size_t len) {
    if (video_.isLoaded(frame) && video_.gen() != gen) return false;  // the built-in pictures never change
    for (int i = 0; i < frame->count && len; i++) {
      static const char START[4] = {0, 0, 0, 1};
      const CamNal& n = frame->nals[i];
      for (int part = 0; part < 2 && len; part++) {
        const char* data = part ? (const char*)n.data : START;
        size_t partLen = part ? n.len : 4;
        if (offset >= partLen) {
          offset -= partLen;
          continue;
        }
        size_t take = partLen - offset < len ? partLen - offset : len;
        memcpy(out, data + offset, take);
        out += take;
        len -= take;
        offset = 0;
      }
    }
    return len == 0;
  };
  c.pull = &c.reader;
  c.staticLen = size;
  c.out = httpHead(200, "video/h264", size, "X-Frame-Seq: " + std::to_string(video_.seq()) + "\r\n");
}

void CamWeb::apiFiles(const ApiRequest& r) {
  HttpConn& c = r.c;
  const std::string& method = r.method;
  bool fileRoute = r.route == "/api/file", mkdirRoute = r.route == "/api/mkdir";
  std::string path = queryParam(r.target, "path");
  CamPathKind kind = camClassifyPath(path);
  bool isFile = kind == CAM_PATH_FILE || kind == CAM_PATH_CLEAR;
  bool isText = kind == CAM_PATH_TEXT;
  bool isPreview = kind == CAM_PATH_PREVIEW;
  const char* body = r.body;
  size_t bodyLen = r.bodyLen;
  if (kind == CAM_PATH_INVALID) {
    c.out = jsonFail(400, "invalid path");
    return;
  }
  if (method != "GET" && method != "DELETE" && !table_.sensorFits(path)) {  // it would be stored and then never do anything
    c.out = jsonFail(409, "the hub already has its limit of " + std::to_string(CAM_MAX_SENSORS) + " sensors; delete one first");
    return;
  }
  if (fileRoute && method == "PUT" && (isFile || isPreview) && !roomBesideHistory(bodyLen)) {
    c.out = jsonFail(507, "storage is nearly full, and the rest is kept for the event history; delete some pictures first");
    return;
  }
  if (mkdirRoute && method == "POST" && !isFile && !isText && !isPreview) {
    bool ok = storage_.makeDirs(path);
    alarm_.rescan();
    c.out = saved(ok, "could not create the folder");
  } else if (fileRoute && method == "PUT" && isPreview) {
    if (bodyLen < 4 || memcmp(body, "\xFF\xD8\xFF", 3) != 0)
      c.out = jsonFail(422, "not a JPEG");
    else
      c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the preview; storage may be full");
  } else if (fileRoute && method == "GET" && (isFile || isText || isPreview)) {
    if (!sendFile(c, path, isText ? JSON_TYPE : isPreview ? "image/jpeg" : "video/h264")) c.out = jsonFail(404, "no such file");
  } else if (fileRoute && method == "PUT" && isText) {
    if (bodyLen > (path == "plan.json" ? 2048u : 1024u)) {  // the plan holds a place for each of up to 32 sensors
      c.out = jsonFail(413, "text is too long");
    } else {
      c.out = saved(storage_.writeFile(path, body, bodyLen), "could not save the text; storage may be full");
      if (path.find('/') != std::string::npos && path.compare(path.size() - 10, 10, "/text.json") != 0) alarm_.rescan();  // a name or settings
    }
  } else if (fileRoute && method == "PUT" && isFile) {
    CamNal nals[3];
    if (!camParseFrame((const uint8_t*)body, bodyLen, nals)) {
      c.out = jsonFail(422, "not a single H.264 picture with SPS, PPS and IDR");
    } else if (!camCompatibleSps(nals[0], CAM_FRAMES[0].nals[0])) {
      c.out = jsonFail(422, "wrong video format: make it with tools/make_frames.py");
    } else {
      // Stored with the hub's own SPS, so the NVR sees one unchanging format whichever encoder made the picture.
      const CamNal parts[3] = {CAM_FRAMES[0].nals[0], nals[1], nals[2]};
      if (parts[0].len + parts[1].len + parts[2].len + 12 > CAM_MAX_FRAME) {
        c.out = jsonFail(413, "file is larger than 32 KB");
        return;
      }
      bool ok = storage_.writeNals(path, parts, 3);
      if (ok) {
        if (storage_.exists(camPreviewPath(path))) storage_.removeTree(camPreviewPath(path));  // it shows the old picture
        alarm_.rescan();
      }
      c.out = saved(ok, "could not save the file; storage may be full");
    }
  } else if (fileRoute && method == "DELETE") {
    bool ok = storage_.exists(path) && storage_.removeTree(path);
    if (ok && isFile && storage_.exists(camPreviewPath(path))) storage_.removeTree(camPreviewPath(path));
    alarm_.rescan();
    c.out = ok ? jsonOk(JSON_OK) : jsonFail(404, "nothing to delete at that path");
  } else {
    c.out = jsonFail(400, "unsupported request");
  }
}
