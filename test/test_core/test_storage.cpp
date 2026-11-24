// Storage paths, pictures, node firmware files, backups, and the event history kept on storage.
#include <algorithm>
#include "tests.h"
#include "cam_node_files.h"
#include "cam_health_history.h"

static int at_most_hour(const std::string& text, long long hour) {  // the lines, after checking none is later than hour
  int lines = 0;
  for (size_t at = 0; at < text.size(); at = text.find('\n', at) + 1) {
    if (atoll(text.c_str() + at) > hour) return -1;
    lines++;
  }
  return lines;
}

static void classifies_storage_paths() {
  const char* mac = "AA-BB-CC-DD-EE-01";
  TEST_ASSERT_EQUAL(CAM_PATH_CLEAR, camClassifyPath("clear.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_NODE, camClassifyPath(mac));
  TEST_ASSERT_EQUAL(CAM_PATH_SENSOR, camClassifyPath(std::string(mac) + "/1"));
  TEST_ASSERT_EQUAL(CAM_PATH_SENSOR, camClassifyPath(std::string(mac) + "/255"));
  TEST_ASSERT_EQUAL(CAM_PATH_FILE, camClassifyPath(std::string(mac) + "/1/active.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_FILE, camClassifyPath(std::string(mac) + "/1/rtl-delayed.h264"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath("text.json"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath(std::string(mac) + "/1/text.json"));
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath(std::string(mac) + "/settings.txt"));  // the node's own settings
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath(std::string(mac) + "/checked.txt"));   // when its first hour was checked
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath(std::string(mac) + "/1/checked.txt"));
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath(std::string(mac) + "/name.txt"));
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath(std::string(mac) + "/text.json"));
  const char* bad[] = {"",
                       "/",
                       "..",
                       "../x",
                       "clear.h264/x",
                       "aa-bb-cc-dd-ee-01",
                       "AA:BB:CC:DD:EE:01",
                       "AA-BB-CC-DD-EE-01/",
                       "AA-BB-CC-DD-EE-01/0",
                       "AA-BB-CC-DD-EE-01/256",
                       "AA-BB-CC-DD-EE-01/01",
                       "AA-BB-CC-DD-EE-01/..",
                       "AA-BB-CC-DD-EE-01/1/..",
                       "AA-BB-CC-DD-EE-01/1/evil.h264",
                       "AA-BB-CC-DD-EE-01/1/active.h264.tmp",
                       "AA-BB-CC-DD-EE-01/1/active.h264/x",
                       "../AA-BB-CC-DD-EE-01/1/active.h264"};
  for (const char* p : bad) TEST_ASSERT_EQUAL_MESSAGE(CAM_PATH_INVALID, camClassifyPath(p), p);
}

static void parses_and_prints_macs() {
  uint8_t mac[6];
  TEST_ASSERT_TRUE(camParseMac("84-F7-03-DF-03-62", mac));
  TEST_ASSERT_EQUAL_HEX8(0x84, mac[0]);
  TEST_ASSERT_EQUAL_HEX8(0x62, mac[5]);
  TEST_ASSERT_EQUAL_STRING("84-F7-03-DF-03-62", camMacText(mac).c_str());
  TEST_ASSERT_FALSE(camParseMac("84-F7-03-DF-03", mac));
  TEST_ASSERT_FALSE(camParseMac("84:F7:03:DF:03:62", mac));
}

static void names_state_files() {
  TEST_ASSERT_EQUAL_STRING("active.h264", camStateFile(0, false).c_str());
  TEST_ASSERT_EQUAL_STRING("approaching-delayed.h264", camStateFile(1, true).c_str());
  TEST_ASSERT_EQUAL_STRING("rtl.h264", camStateFile(4, false).c_str());
}

static void splits_picture_files() {
  const uint8_t ok[] = {0, 0, 0, 1, 0x67, 1, 2, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x06, 9, 0, 0, 1, 0x65, 4, 5, 6};
  CamNal n[3];
  TEST_ASSERT_TRUE(camParseFrame(ok, sizeof(ok), n));
  TEST_ASSERT_EQUAL(3, n[0].len);
  TEST_ASSERT_EQUAL_HEX8(0x67, n[0].data[0]);
  TEST_ASSERT_EQUAL(2, n[1].len);  // the zero of the following 4-byte start code is not part of it
  TEST_ASSERT_EQUAL_HEX8(0x68, n[1].data[0]);
  TEST_ASSERT_EQUAL(4, n[2].len);
  TEST_ASSERT_EQUAL_HEX8(0x65, n[2].data[0]);

  const uint8_t noIdr[] = {0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2};
  const uint8_t twoIdr[] = {0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2, 0, 0, 1, 0x65, 3, 0, 0, 1, 0x65, 4};
  const uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8};
  TEST_ASSERT_FALSE(camParseFrame(noIdr, sizeof(noIdr), n));
  TEST_ASSERT_FALSE(camParseFrame(twoIdr, sizeof(twoIdr), n));
  TEST_ASSERT_FALSE(camParseFrame(garbage, sizeof(garbage), n));
}

static void accepts_sps_differing_only_in_vui() {
  uint8_t hub[] = {0x67, 0x42, 0xc0, 0x1e, 0xd9, 0x00, 0xa0, 0x2f, 0xf9, 0x70, 0x11, 0x00, 0x00,
                   0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x02, 0x0f, 0x16, 0x2e, 0x48};
  // The header ffmpeg.wasm produced in a real browser.
  uint8_t browser[] = {0x67, 0x42, 0xc0, 0x1e, 0xd9, 0x00, 0xa0, 0x2f, 0xf9, 0x61, 0x00, 0x00,
                       0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x02, 0x0f, 0x16, 0x2e, 0x48};
  CamNal a{hub, sizeof(hub)}, b{browser, sizeof(browser)};
  TEST_ASSERT_FALSE(camSameNal(a, b));
  TEST_ASSERT_TRUE(camCompatibleSps(a, b));
  TEST_ASSERT_TRUE(camCompatibleSps(a, CAM_FRAMES[0].nals[0]));

  uint8_t changed[sizeof(browser)];
  CamNal c{changed, sizeof(changed)};
  for (int index : {1, 2, 3, 4, 5, 6, 7}) {  // profile, constraints, level, and the fields up to the picture size
    memcpy(changed, browser, sizeof(browser));
    changed[index] ^= 0x10;
    TEST_ASSERT_FALSE_MESSAGE(camCompatibleSps(a, c), std::to_string(index).c_str());
  }
  CamNal empty{browser, 0};
  TEST_ASSERT_FALSE(camCompatibleSps(a, empty));
}

static void builtin_pictures_are_valid() {
  for (int f = 0; f < 2; f++) {
    TEST_ASSERT_EQUAL(3, CAM_FRAMES[f].count);
    TEST_ASSERT_EQUAL(7, CAM_FRAMES[f].nals[0].data[0] & 31);
    TEST_ASSERT_EQUAL(8, CAM_FRAMES[f].nals[1].data[0] & 31);
    TEST_ASSERT_EQUAL(5, CAM_FRAMES[f].nals[2].data[0] & 31);
  }
  TEST_ASSERT_TRUE(camSameNal(CAM_FRAMES[0].nals[0], CAM_FRAMES[1].nals[0]));
}

static void backup_values_follow_the_page_rules() {
  // The hub password as the page requires it; an empty one, from a backup from before, is read but not restored.
  TEST_ASSERT_FALSE(camBackupValueValid("net.webpw", "seven77"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.webpw", "eight888"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.webpw", std::string(64, 'x')));
  TEST_ASSERT_TRUE(camBackupValueValid("net.webpw", ""));
  TEST_ASSERT_FALSE(camBackupValueKept("net.webpw", ""));
  TEST_ASSERT_TRUE(camBackupValueKept("net.webpw", "eight888"));
  TEST_ASSERT_TRUE(camBackupValueKept("net.name", "hub"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.web", "80"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.web", "0"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.tzoff", "-180"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzoff", "99999"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzoff", "10"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.tzdst", "7"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.rbday", "-1"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.rbmin", "1440"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.name", "esp-alarmhub"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.name", "bad name"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.name", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.enkey", "0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.enkey", "short"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hook", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hook", "http://192.168.1.10:8123/api/webhook/hub"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "https://192.168.1.10/x"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "http://homeassistant.local/x"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hook", "http://192.168.1.10/a b"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hookal", "1"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.token", "0123abcd"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.token", "a\"b"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.ip", "192.168.8.192"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.ip", "192.168.8"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.nvrip", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("net.webpw", ""));
  TEST_ASSERT_TRUE(camBackupValueValid("wifi.password2", ""));
  TEST_ASSERT_FALSE(camBackupValueValid("wifi.password", "short"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.ntp", "2"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.unknown", "1"));
  TEST_ASSERT_EQUAL(CAM_PATH_PREVIEW, camClassifyPath("plan.jpg"));  // the floor plan
  TEST_ASSERT_EQUAL(CAM_PATH_TEXT, camClassifyPath("plan.json"));
  TEST_ASSERT_EQUAL(CAM_PATH_INVALID, camClassifyPath("plan.png"));
  // Every setting a backup holds is checked, or no backup holding it could be restored.
  for (const CamSetting& k : camSettings()) TEST_ASSERT_TRUE_MESSAGE(camBackupKeyKnown(std::string(k.space) + "." + k.key), k.key);
  TEST_ASSERT_FALSE(camBackupKeyKnown("net.unknown"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hookdy", "-1"));
  TEST_ASSERT_TRUE(camBackupValueValid("net.hookdy", "450"));
  TEST_ASSERT_FALSE(camBackupValueValid("net.hookdy", "1440"));
}

static void a_node_image_is_told_from_other_files() {
  std::string file(5000, '\0');
  file[0] = (char)0xE9;
  file[12] = 13;  // an ESP32-C6
  std::string mark = std::string(CAM_NODE_IMAGE_MARK) + "3f2a1b0";
  file.replace(1020, mark.size(), mark);  // across the first two blocks read
  auto reader = [&](const std::string& f) {
    return [&f](size_t offset, char* out, size_t len) {
      if (offset + len > f.size()) return false;
      memcpy(out, f.data() + offset, len);
      return true;
    };
  };
  CamNodeImage image;
  TEST_ASSERT_TRUE(camCheckNodeImage(reader(file), file.size(), image));
  TEST_ASSERT_EQUAL(13, image.chip);
  TEST_ASSERT_EQUAL_STRING("3f2a1b0", image.version.c_str());
  TEST_ASSERT_EQUAL(5000, image.size);
  CamNodeImage again;
  std::string changed = file;
  changed[4000] = 1;
  TEST_ASSERT_TRUE(camCheckNodeImage(reader(changed), changed.size(), again));
  TEST_ASSERT_NOT_EQUAL(image.number, again.number);  // another file, another number
  std::string hub = file;
  hub.replace(1020, mark.size(), std::string(mark.size(), 'x'));  // the hub's own firmware has no such mark
  TEST_ASSERT_FALSE(camCheckNodeImage(reader(hub), hub.size(), image));
  std::string notApp = file;
  notApp[0] = 'P';
  TEST_ASSERT_FALSE(camCheckNodeImage(reader(notApp), notApp.size(), image));
}

static std::string tempFolder(const char* name) {
  std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : "/tmp") + "/" + name;
  CamStorage(path).removeTree("");
  CamStorage(path).makeDirs("");
  return path;
}

static void a_node_and_a_sensor_keep_one_file_each() {
  CamNodeFile node;
  TEST_ASSERT_TRUE(camParseNodeFile("late=300\nsaid.every=7200\nsaid.sensor=RCWL-0516\nsaid.version=3f2a1b0\n", node));
  TEST_ASSERT_EQUAL(300, node.settings.lateS);
  TEST_ASSERT_EQUAL(7200, node.info.everyS);
  TEST_ASSERT_EQUAL_STRING("RCWL-0516", node.info.sensor.c_str());
  CamNodeFile again;
  TEST_ASSERT_TRUE(camParseNodeFile(camNodeFileText(node), again));
  TEST_ASSERT_TRUE(again.info == node.info);
  TEST_ASSERT_EQUAL(300, again.settings.lateS);
  TEST_ASSERT_EQUAL_STRING("late=300\n", camNodeFileText(CamNodeFile{node.settings, CamNodeInfo()}).c_str());  // said nothing yet
  TEST_ASSERT_EQUAL_STRING("", camNodeFileText(CamNodeFile()).c_str());
  TEST_ASSERT_FALSE(camParseNodeFile("late=0\n", node));        // a page setting out of range
  TEST_ASSERT_FALSE(camParseNodeFile("said.every=x\n", node));  // what the node said, damaged
  TEST_ASSERT_FALSE(camParseNodeFile("name=Gate\n", node));     // a sensor's line
  TEST_ASSERT_EQUAL(300, node.settings.lateS);                  // left as it was

  CamSensorFile sensor;
  TEST_ASSERT_TRUE(camParseSensorFile("name=Front \"gate\"\r\nhold=3\n", sensor));
  TEST_ASSERT_EQUAL_STRING("Front \"gate\"", sensor.name.c_str());
  TEST_ASSERT_EQUAL(3, sensor.settings.holdS);
  TEST_ASSERT_EQUAL_STRING("name=Front \"gate\"\nhold=3\n", camSensorFileText(sensor).c_str());
  TEST_ASSERT_EQUAL_STRING("", camSensorFileText(CamSensorFile()).c_str());
  TEST_ASSERT_FALSE(camParseSensorFile(("name=" + std::string(65, 'x') + "\n").c_str(), sensor));
  TEST_ASSERT_FALSE(camParseSensorFile("name=a\tb\n", sensor));  // a control character
  TEST_ASSERT_FALSE(camParseSensorFile("late=300\n", sensor));   // a node's line
  TEST_ASSERT_TRUE(camValidName(std::string(64, 'x')));
  TEST_ASSERT_FALSE(camValidName("two\nlines"));
}

static void the_files_from_before_are_taken_in_once() {
  std::string folder = tempFolder("cam_node_files_test");
  CamStorage storage(folder);
  storage.makeDirs("node/1");
  storage.writeFile("node/settings.txt", "late=120\n", 9);
  storage.writeFile("node/about.txt", "every=600\n", 10);
  CamNodeFile node = camReadNodeFile(storage, "node");
  TEST_ASSERT_EQUAL(120, node.settings.lateS);
  TEST_ASSERT_EQUAL(600, node.info.everyS);
  TEST_ASSERT_FALSE(storage.exists("node/settings.txt"));
  TEST_ASSERT_FALSE(storage.exists("node/about.txt"));
  std::string text;
  TEST_ASSERT_TRUE(storage.readFile("node/node.txt", text, 1024));
  TEST_ASSERT_TRUE(text.compare(0, 23, "late=120\nsaid.every=600") == 0);
  TEST_ASSERT_EQUAL(120, camReadNodeFile(storage, "node").settings.lateS);  // read again, from node.txt alone

  storage.writeFile("node/1/name.txt", "Gate\r\n", 6);
  storage.writeFile("node/1/settings.txt", "hold=3\n", 7);
  CamSensorFile sensor = camReadSensorFile(storage, "node/1");
  TEST_ASSERT_EQUAL_STRING("Gate", sensor.name.c_str());
  TEST_ASSERT_EQUAL(3, sensor.settings.holdS);
  TEST_ASSERT_FALSE(storage.exists("node/1/name.txt"));
  TEST_ASSERT_TRUE(storage.readFile("node/1/sensor.txt", text, 512));
  TEST_ASSERT_EQUAL_STRING("name=Gate\nhold=3\n", text.c_str());

  sensor = CamSensorFile();
  TEST_ASSERT_TRUE(camWriteSensorFile(storage, "node/1", sensor));  // nothing set: no file
  TEST_ASSERT_FALSE(storage.exists("node/1/sensor.txt"));
  storage.writeFile("node/node.txt", "late=0\n", 7);  // damaged: the defaults
  TEST_ASSERT_EQUAL(0, camReadNodeFile(storage, "node").settings.lateS);
  storage.removeTree("");
}

static CamEvent sampleEvent(int n) {
  CamEvent e;
  e.received = 1800000000 + n;
  e.age = (uint32_t)n;
  snprintf(e.sensor, sizeof(e.sensor), "AA-BB-CC-DD-EE-%02d/1", n % 100);
  snprintf(e.text, sizeof(e.text), "Gate, \"%d\"", n);
  e.kind = CAM_EVENT_REPORT;
  e.active = n % 2;
  e.delayed = n % 3 == 0;
  e.battery = (uint8_t)(n % 101);
  e.targetCount = 2;
  e.targets[0].distanceCm = 250;
  e.targets[0].direction = 1;
  e.targets[1].distanceCm = 120;
  e.targets[1].direction = 3;
  return e;
}

static void an_event_record_reads_back_and_damage_is_found() {
  CamEvent e = sampleEvent(42), back;
  e.offS = 3600;
  uint8_t r[CAM_EVENT_RECORD];
  camEventEncode(e, r);
  TEST_ASSERT_TRUE(camEventDecode(r, back));
  TEST_ASSERT_TRUE(camEventCsvLine(back) == camEventCsvLine(e));
  TEST_ASSERT_EQUAL_UINT32(3600, back.offS);
  TEST_ASSERT_EQUAL_STRING(e.text, back.text);
  r[40] ^= 1;
  TEST_ASSERT_FALSE(camEventDecode(r, back));
  TEST_ASSERT_EQUAL_STRING("2027-01-15T08:00:42Z,2027-01-15T08:00:00Z,AA-BB-CC-DD-EE-42/1,\"Gate, \"\"42\"\"\",clear,42,1.20,left to right\n",
                           camEventCsvLine(e).c_str());
}

// Reads the download the way the web server does: in pieces, some sent only in part and asked for again.
static std::string download(CamEventLog& log, const std::string& sensor = std::string(), time_t since = 0) {
  log.csvBegin(sensor, since);
  size_t at = 0;
  std::string out;
  char piece[700];
  for (;;) {
    size_t n = log.csvRead(at, piece, sizeof(piece));
    if (!n) break;
    size_t sent = n > 100 && (at / 700) % 2 ? n / 3 : n;  // every other piece only partly sent
    out.append(piece, sent);
    at += sent;
  }
  char extra;
  return log.csvRead(at, &extra, 1) ? "read past the end" : out;
}

static void the_event_log_survives_restarts_and_downloads_whole() {
  std::string folder = tempFolder("cam_event_log_test");
  CamStorage storage(folder);
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
  {
    CamEventLog log(storage);
    log.load();
    for (int i = 0; i < 100; i++) {
      log.add(sampleEvent(i));
      expect += camEventCsvLine(sampleEvent(i));
      if (i % 7 == 0) TEST_ASSERT_TRUE(log.save(false));
    }
    TEST_ASSERT_TRUE(log.save(false));
    TEST_ASSERT_TRUE(download(log) == expect);
  }
  FILE* f = fopen((folder + "/events.log").c_str(), "ab");  // a power cut part way through a record
  fwrite("\x01\x02\x03", 1, 3, f);
  fclose(f);
  CamEventLog again(storage);
  again.load();
  TEST_ASSERT_EQUAL(CamEventLog::RECENT, again.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(99).text, again.recent(0).text);
  TEST_ASSERT_EQUAL_STRING(sampleEvent(36).text, again.recent(63).text);
  again.add(sampleEvent(100));  // written over the cut-off record
  TEST_ASSERT_TRUE(again.save(false));
  TEST_ASSERT_TRUE(download(again) == expect + camEventCsvLine(sampleEvent(100)));
  TEST_ASSERT_EQUAL(101 * CAM_EVENT_RECORD, (int)again.bytesOnStorage());
}

static void the_event_log_starts_a_new_file_but_not_during_a_download() {
  std::string folder = tempFolder("cam_event_log_rotate");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.load();
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n";
  size_t perFile = CamEventLog::FILE_MAX / CAM_EVENT_RECORD;
  for (size_t i = 0; i < perFile + 10; i++) {
    log.add(sampleEvent((int)i));
    expect += camEventCsvLine(sampleEvent((int)i));
    TEST_ASSERT_TRUE(log.save(i == perFile + 5));  // one save during a download keeps adding to the same file
  }
  TEST_ASSERT_TRUE(storage.exists("events-old.log"));
  TEST_ASSERT_TRUE(download(log) == expect);
  CamEventLog again(storage);
  again.load();
  TEST_ASSERT_TRUE(download(again) == expect);
}

// One sensor's events from a time on, as each sensor's page asks for, across both log files, with an event saved before
// the clock was set among them, which must not make the search skip later ones.
static void the_history_can_be_one_sensors_last_days() {
  std::string folder = tempFolder("cam_event_log_chosen");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.load();
  std::string header = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\n", expect = header;
  size_t perFile = CamEventLog::FILE_MAX / CAM_EVENT_RECORD, total = perFile + 300;
  time_t since = 1800000000 + (time_t)perFile - 200;
  for (size_t i = 0; i < total; i++) {
    CamEvent e = sampleEvent((int)i);
    if (i == perFile + 100) e.received = 1000;  // after a restart without the time
    log.add(e);
    if (e.received >= since && i % 100 == 7) expect += camEventCsvLine(e);
    TEST_ASSERT_TRUE(log.save(false));
  }
  TEST_ASSERT_TRUE(storage.exists("events-old.log"));
  TEST_ASSERT_TRUE(download(log, "AA-BB-CC-DD-EE-07/1", since) == expect);
  TEST_ASSERT_TRUE(download(log, "AA-BB-CC-DD-EE-07/1", 1800000000 + (time_t)total + 5) == header);  // none yet
  std::string whole = download(log);                                                                 // and the whole history again after it
  TEST_ASSERT_EQUAL((int)(total + 1), (int)std::count(whole.begin(), whole.end(), '\n'));
}

// The weekly summary reads the history back from a time on, newest first: the events waiting to be saved, then both
// logs, skipping one from before the clock was set, and says whether it reached that time.
static void the_history_is_read_back_newest_first_to_a_time() {
  std::string folder = tempFolder("cam_event_log_since");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.load();
  size_t perFile = CamEventLog::FILE_MAX / CAM_EVENT_RECORD, total = perFile + 300;
  for (size_t i = 0; i < total; i++) {
    CamEvent e = sampleEvent((int)i);
    if (i == total - 50) e.received = 1000;  // after a restart without the time
    log.add(e);
    if (i < total - 3) TEST_ASSERT_TRUE(log.save(false));  // the newest three still waiting
  }
  std::vector<time_t> seen;
  time_t since = 1800000000 + (time_t)perFile - 100;  // reaches into the older log
  TEST_ASSERT_TRUE(log.eachSince(since, 100000, [&](const CamEvent& e) { seen.push_back(e.received); }));
  TEST_ASSERT_EQUAL((int)(total - (perFile - 100) - 1), (int)seen.size());  // all since then, but the one without a time
  TEST_ASSERT_EQUAL((int)(1800000000 + total - 1), (int)seen.front());      // the newest, still waiting, first
  for (size_t i = 1; i < seen.size(); i++) TEST_ASSERT_TRUE(seen[i] < seen[i - 1]);
  TEST_ASSERT_EQUAL((int)since, (int)seen.back());
  seen.clear();
  TEST_ASSERT_FALSE(log.eachSince(since, 10, [&](const CamEvent& e) { seen.push_back(e.received); }));  // stopped at most
  TEST_ASSERT_EQUAL(10, (int)seen.size());
  TEST_ASSERT_FALSE(log.eachSince(0, 100000, [](const CamEvent&) {}));  // the history ends before then
}

// Read in steps, a new log begun in the middle moves the log being read to the older one: the scan follows it, hands
// out each event once, newest first, and ends there, not complete, as the older log it would have read next is gone.
static void a_history_read_in_steps_follows_a_new_log() {
  std::string folder = tempFolder("cam_event_log_steps");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.load();
  size_t perFile = CamEventLog::FILE_MAX / CAM_EVENT_RECORD;
  for (size_t i = 0; i < perFile + 50; i++) {
    log.add(sampleEvent((int)i));
    TEST_ASSERT_TRUE(log.save(false));
  }
  CamHistoryScan s;
  log.scanBegin(s, 1800000000, 100000);
  std::vector<time_t> seen;
  auto each = [&](const CamEvent& e) { seen.push_back(e.received); };
  TEST_ASSERT_FALSE(log.scanStep(s, 8, each));  // the waiting events, none, and the current log's size
  TEST_ASSERT_FALSE(log.scanStep(s, 8, each));
  TEST_ASSERT_EQUAL(8, (int)seen.size());
  for (size_t i = perFile + 50; i < 2 * perFile + 10; i++) {  // enough for a new log
    log.add(sampleEvent((int)i));
    TEST_ASSERT_TRUE(log.save(false));
  }
  while (!log.scanStep(s, 8, each)) {
  }
  TEST_ASSERT_FALSE(s.complete);
  TEST_ASSERT_EQUAL(50, (int)seen.size());  // the 50 that were the current log, each once
  for (size_t i = 1; i < seen.size(); i++) TEST_ASSERT_TRUE(seen[i] == seen[i - 1] - 1);
  TEST_ASSERT_EQUAL((int)(1800000000 + perFile + 49), (int)seen.front());
}

// A 4 MB board's storage holds some 190 KB: the history is kept small enough to fit, and storage that refuses a save
// gives up the older log for room, as the report waiting on that save cannot be confirmed without it.
static void a_small_storage_keeps_a_smaller_history_that_always_saves() {
  TEST_ASSERT_EQUAL((int)CamEventLog::FILE_MAX, (int)camLogFileMax(12 * 1024 * 1024, 64 * 1024));  // 16 MB board: unchanged
  size_t small = camLogFileMax(190 * 1024, 64 * 1024);
  TEST_ASSERT_TRUE(small < 40 * 1024 && small > 20 * 1024);
  TEST_ASSERT_EQUAL(0, (int)(small % CAM_EVENT_RECORD));
  TEST_ASSERT_TRUE(2 * small + 64 * 1024 < 190 * 1024);  // both logs and the room kept fit, with some left for pictures
  TEST_ASSERT_EQUAL(64 * (int)CAM_EVENT_RECORD, (int)camLogFileMax(10 * 1024, 64 * 1024));  // never below 64 records

  std::string folder = tempFolder("cam_event_log_small");
  CamStorage storage(folder);
  CamEventLog log(storage);
  log.setFileMax(10 * CAM_EVENT_RECORD);
  log.load();
  for (int i = 0; i < 35; i++) {
    log.add(sampleEvent(i));
    TEST_ASSERT_TRUE(log.save(false));
  }
  TEST_ASSERT_TRUE(log.bytesOnStorage() <= 2 * 10 * CAM_EVENT_RECORD);  // the older ones went, as on a full log

  // Storage refusing the log, as full storage would: here the log is a folder, which cannot be written to.
  storage.removeTree("events.log");
  storage.makeDirs("events.log");
  log.add(sampleEvent(99));
  TEST_ASSERT_TRUE(log.save(false));  // the older log gave way, and the save went through
  TEST_ASSERT_FALSE(log.saveFailed());
  TEST_ASSERT_EQUAL((int)CAM_EVENT_RECORD, (int)storage.fileSize("events.log"));
  TEST_ASSERT_EQUAL_STRING(sampleEvent(99).text, log.recent(0).text);
}

static void the_older_firmwares_history_is_kept() {
  std::string folder = tempFolder("cam_event_log_legacy");
  CamStorage storage(folder);
  // Its newest events as a copy of memory, with the count and next position: three events, the newest at index 2.
  std::string blob(16 + 64 * sizeof(CamEvent), '\0');
  uint32_t header[4] = {0x31564543, (uint32_t)sizeof(CamEvent), 3, 3};
  memcpy(&blob[0], header, sizeof(header));
  for (int i = 0; i < 3; i++) {
    CamEvent e = sampleEvent(i);
    memcpy(&blob[16 + i * sizeof(CamEvent)], &e, sizeof(e));
  }
  storage.writeFile("events.bin", blob.data(), blob.size());
  storage.writeFile("events.csv", "old line\n", 9);
  CamEventLog log(storage);
  log.load();
  TEST_ASSERT_EQUAL(3, log.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(2).text, log.recent(0).text);
  TEST_ASSERT_FALSE(storage.exists("events.bin"));
  std::string expect = "received_utc,happened_utc,sensor,name,event,battery_percent,distance_m,direction\nold line\n";
  for (int i = 0; i < 3; i++) expect += camEventCsvLine(sampleEvent(i));
  TEST_ASSERT_TRUE(download(log) == expect);  // the old CSV file first, unchanged

  // A failed update is undone, and the older firmware starts a new file: its events go after the log's.
  header[2] = header[3] = 2;
  for (int i = 0; i < 2; i++) {
    CamEvent e = sampleEvent(10 + i);
    memcpy(&blob[16 + i * sizeof(CamEvent)], &e, sizeof(e));
  }
  memcpy(&blob[0], header, sizeof(header));
  storage.writeFile("events.bin", blob.data(), blob.size());
  CamEventLog later(storage);
  later.load();
  TEST_ASSERT_EQUAL(5, later.count());
  TEST_ASSERT_EQUAL_STRING(sampleEvent(11).text, later.recent(0).text);
  TEST_ASSERT_FALSE(storage.exists("events.bin"));
  for (int i = 0; i < 2; i++) expect += camEventCsvLine(sampleEvent(10 + i));
  TEST_ASSERT_TRUE(download(later) == expect);
}

// A week of the hub's health, an hour at a time: each hour's least memory, weakest signal and NVR minutes, kept across a
// restart, a week long, and nothing before the clock is set.
static void the_health_history_keeps_a_week_by_the_hour() {
  CamHealthHistory h;
  const time_t T = 1790000000 - 1790000000 % 3600;             // an hour's start in 2026
  TEST_ASSERT_FALSE(h.sample(1000, 100000, -50, true, 5000));  // the clock not set: nothing kept
  TEST_ASSERT_FALSE(h.sample(T, 120 * 1024, -60, true, 0));
  for (int s = 5; s < 3600; s += 5) h.sample(T + s, (s == 1800 ? 80 : 120) * 1024, s == 900 ? -75 : -60, s < 1800, 5000);
  TEST_ASSERT_TRUE(h.sample(T + 3600, 120 * 1024, -60, true, 5000));  // the hour has ended
  TEST_ASSERT_EQUAL(1, h.ended());
  std::string out;
  {
    CamJson j(out);
    j.array();
    h.hours(j);
    j.finish();
  }
  char want[96];
  snprintf(want, sizeof(want), "[[%lld,80,-75,59,29],[%lld,120,-60,0,0]]\n", (long long)T, (long long)T + 3600);
  TEST_ASSERT_EQUAL_STRING(want, out.c_str());
  CamHealthHistory again;  // after a restart, from what was saved
  again.read(h.text() + "nonsense\n1 2 3\n");
  TEST_ASSERT_EQUAL(1, again.ended());
  TEST_ASSERT_EQUAL_STRING(h.text().c_str(), again.text().c_str());
  for (int i = 2; i <= 200; i++) h.sample(T + i * 3600, 100 * 1024, -60, true, 5000);  // more than a week
  TEST_ASSERT_EQUAL(CamHealthHistory::HOURS, h.ended());
  out.clear();
  {
    CamJson j(out);
    j.array();
    h.hours(j);
    j.finish();
  }
  int rows = 0;
  for (size_t i = 1; i < out.size(); i++) rows += out[i] == '[';
  TEST_ASSERT_EQUAL(CamHealthHistory::HOURS, rows);       // the week up to the hour under way, which is one of them
  h.sample(T + 100 * 3600, 100 * 1024, -60, true, 5000);  // the clock set back: the later hours go
  h.sample(T + 101 * 3600, 100 * 1024, -60, true, 5000);
  TEST_ASSERT_EQUAL(69, at_most_hour(h.text(), T / 3600 + 100));  // hours 32 to 100
}

void runStorageTests() {
  RUN_TEST(the_health_history_keeps_a_week_by_the_hour);
  RUN_TEST(an_event_record_reads_back_and_damage_is_found);
  RUN_TEST(the_event_log_survives_restarts_and_downloads_whole);
  RUN_TEST(the_event_log_starts_a_new_file_but_not_during_a_download);
  RUN_TEST(a_small_storage_keeps_a_smaller_history_that_always_saves);
  RUN_TEST(the_history_can_be_one_sensors_last_days);
  RUN_TEST(the_history_is_read_back_newest_first_to_a_time);
  RUN_TEST(a_history_read_in_steps_follows_a_new_log);
  RUN_TEST(the_older_firmwares_history_is_kept);
  RUN_TEST(a_node_image_is_told_from_other_files);
  RUN_TEST(classifies_storage_paths);
  RUN_TEST(parses_and_prints_macs);
  RUN_TEST(names_state_files);
  RUN_TEST(splits_picture_files);
  RUN_TEST(accepts_sps_differing_only_in_vui);
  RUN_TEST(builtin_pictures_are_valid);
  RUN_TEST(backup_values_follow_the_page_rules);
  RUN_TEST(a_node_and_a_sensor_keep_one_file_each);
  RUN_TEST(the_files_from_before_are_taken_in_once);
}
