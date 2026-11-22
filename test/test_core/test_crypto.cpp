// Hashes, keys and passwords, against the standards' own test values.
#include "tests.h"
#include "cam_backup.h"

static std::string hex(const std::string& raw) { return camHex((const uint8_t*)raw.data(), raw.size()); }

static void hmac_sha1_matches_rfc_2202() {
  TEST_ASSERT_EQUAL_STRING("b617318655057264e28bc0b6fb378c8ef146be00", hex(camHmacSha1(std::string(20, '\x0b'), "Hi There")).c_str());
  TEST_ASSERT_EQUAL_STRING("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", hex(camHmacSha1("Jefe", "what do ya want for nothing?")).c_str());
  TEST_ASSERT_EQUAL_STRING("aa4ae5e15272d00e95705637ce8a3b55ed402112",
                           hex(camHmacSha1(std::string(80, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")).c_str());
}

// FIPS 180 examples, and the same hash whatever pieces the message arrives in, across block edges.
static void sha1_in_pieces_matches() {
  TEST_ASSERT_EQUAL_STRING("a9993e364706816aba3e25717850c26c9cd0d89d", hex(camSha1("abc")).c_str());
  TEST_ASSERT_EQUAL_STRING("84983e441c3bd26ebaae4aa1f95129e5e54670f1",
                           hex(camSha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")).c_str());
  TEST_ASSERT_EQUAL_STRING("da39a3ee5e6b4b0d3255bfef95601890afd80709", hex(camSha1("")).c_str());
  std::string text;
  for (int i = 0; i < 300; i++) text += (char)(i * 7);
  for (size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 300u}) {
    CamSha1 s;
    for (size_t at = 0; at < text.size(); at += piece) s.add(text.data() + at, text.size() - at < piece ? text.size() - at : piece);
    uint8_t out[20];
    s.finish(out);
    TEST_ASSERT_EQUAL_STRING(hex(camSha1(text)).c_str(), camHex(out, 20).c_str());
  }
  // Lengths where the padding does and does not need a block of its own, checked against Python's hashlib.
  TEST_ASSERT_EQUAL_STRING("c1c8bbdc22796e28c0e15163d20899b65621d65a", hex(camSha1(std::string(55, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("c2db330f6083854c99d4b5bfb6e8f29f201be699", hex(camSha1(std::string(56, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("03f09f5b158a7a8cdad920bddc29b81c18a551f5", hex(camSha1(std::string(63, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("0098ba824b5c16427bd7a1122a5a442a25ec644d", hex(camSha1(std::string(64, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56", hex(camSha1(std::string(119, 'a'))).c_str());
  TEST_ASSERT_EQUAL_STRING("f34c1488385346a55709ba056ddd08280dd4c6d6", hex(camSha1(std::string(120, 'a'))).c_str());
}

// RFC 7748's test vectors: section 5.2, and the key agreement of section 6.1.
static void x25519_matches_rfc_7748() {
  auto bytes = [](const char* hexText, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) {
      unsigned v = 0;
      sscanf(hexText + 2 * i, "%2x", &v);
      out[i] = (uint8_t)v;
    }
  };
  uint8_t k[32], u[32], out[32];
  bytes("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4", k);
  bytes("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c", u);
  camX25519(out, k, u);
  TEST_ASSERT_EQUAL_STRING("c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", camHex(out, 32).c_str());
  uint8_t alice[32], bob[32], alicePub[32], bobPub[32], s1[32], s2[32];
  bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice);
  bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bob);
  camX25519Public(alicePub, alice);
  camX25519Public(bobPub, bob);
  TEST_ASSERT_EQUAL_STRING("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", camHex(alicePub, 32).c_str());
  TEST_ASSERT_EQUAL_STRING("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", camHex(bobPub, 32).c_str());
  camX25519(s1, alice, bobPub);
  camX25519(s2, bob, alicePub);
  TEST_ASSERT_EQUAL_STRING("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", camHex(s1, 32).c_str());
  TEST_ASSERT_EQUAL_STRING(camHex(s1, 32).c_str(), camHex(s2, 32).c_str());
}

static void base64_matches_known_values() {
  TEST_ASSERT_EQUAL_STRING("TWE=", base64((const uint8_t*)"Ma", 2).c_str());
  TEST_ASSERT_EQUAL_STRING("TWFu", base64((const uint8_t*)"Man", 3).c_str());
  TEST_ASSERT_EQUAL_STRING("TQ==", base64((const uint8_t*)"M", 1).c_str());
}

static void xml_text_is_unescaped_and_secrets_compared_whole() {
  TEST_ASSERT_EQUAL_STRING("p&ss<w>\"'A", xmlFirstText("<a:Password> p&amp;ss&lt;w&gt;&quot;&apos;&#65;</a:Password>", "Password").c_str());
  TEST_ASSERT_EQUAL_STRING("a&b;c&zz;", xmlFirstText("<x>a&b;c&zz;</x>", "x").c_str());  // not entities, so kept
  TEST_ASSERT_TRUE(camSameText("secret", "secret"));
  TEST_ASSERT_FALSE(camSameText("secret", "secreT"));
  TEST_ASSERT_FALSE(camSameText("secret", "secrets"));
  TEST_ASSERT_FALSE(camSameText("", "x"));
  TEST_ASSERT_FALSE(camSnapshotToken("one", "pw") == camSnapshotToken("two", "pw"));  // depends on the hub's secret
}

static void wrong_passwords_lock_out_quick_and_slow_guessing() {
  CamLoginGuard g;
  uint32_t t = 1000;
  for (int i = 0; i < 9; i++) g.note(7, false, t);
  TEST_ASSERT_TRUE(g.allowed(7, t));  // nine wrong: still allowed
  g.note(7, false, t);
  TEST_ASSERT_FALSE(g.allowed(7, t + 59000));  // the tenth locks it for a minute
  TEST_ASSERT_TRUE(g.allowed(8, t));           // only that address
  TEST_ASSERT_TRUE(g.allowed(7, t + 60000));
  g.note(7, false, t + 60000);  // waiting out the lock does not start the count again: the next lock is twice as long
  TEST_ASSERT_FALSE(g.allowed(7, t + 60000 + 119000));
  TEST_ASSERT_TRUE(g.allowed(7, t + 60000 + 120000));
  uint32_t at = t + 180000;
  for (int i = 0; i < 20; i++) {  // guessing slowly, once each lock runs out, reaches the longest lock
    g.note(7, false, at);
    at += CamLoginGuard::LONGEST_LOCK_MS;
  }
  TEST_ASSERT_FALSE(g.allowed(7, at - CamLoginGuard::LONGEST_LOCK_MS + CamLoginGuard::LONGEST_LOCK_MS - 1));
  g.note(7, true, at);  // a right password clears it
  TEST_ASSERT_TRUE(g.allowed(7, at));
  for (int i = 0; i < 9; i++) g.note(9, false, 0);
  g.note(9, false, CamLoginGuard::FORGET_MS);  // a day later, the old ones are forgotten
  TEST_ASSERT_TRUE(g.allowed(9, CamLoginGuard::FORGET_MS));
}

static void only_the_hubs_own_names_are_served() {
  const char* ip = "192.168.1.50";
  TEST_ASSERT_TRUE(camKnownHost("", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("192.168.1.50:8080", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("192.168.4.1", ip, "esp-alarmhub.local", "192.168.4.1:8080", ""));
  TEST_ASSERT_TRUE(camKnownHost("ESP-AlarmHub.Local.", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camKnownHost("esp-alarmhub.home.arpa", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_FALSE(camKnownHost("esp-alarmhub.fritz.box", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camKnownHost("esp-alarmhub.fritz.box:80", ip, "esp-alarmhub.local", "", " Esp-AlarmHub.Fritz.Box , other"));
  TEST_ASSERT_FALSE(camKnownHost("esp-alarmhub.attacker.example", ip, "esp-alarmhub.local", "", "other"));
  TEST_ASSERT_FALSE(camKnownHost("192.168.1.5", ip, "esp-alarmhub.local", "", ""));
  TEST_ASSERT_TRUE(camHostListValid(""));
  TEST_ASSERT_TRUE(camHostListValid("a.b, c-d,e,f"));
  TEST_ASSERT_FALSE(camHostListValid("a,b,c,d,e"));
  TEST_ASSERT_FALSE(camHostListValid("bad name"));
  TEST_ASSERT_FALSE(camHostListValid(".lead"));
  TEST_ASSERT_FALSE(camHostListValid("x/y"));
}

// The long fingerprint starts with the short one, so a node's older label still matches the hub's page.
static void the_long_fingerprint_starts_with_the_short_one() {
  uint8_t key[S2O_X25519_LEN];
  for (int i = 0; i < S2O_X25519_LEN; i++) key[i] = (uint8_t)(i * 7 + 3);
  char shortOne[15], longOne[20];
  s2oFingerprint(key, shortOne);
  s2oFingerprintLong(key, longOne);
  TEST_ASSERT_EQUAL(14, (int)strlen(shortOne));
  TEST_ASSERT_EQUAL(19, (int)strlen(longOne));
  TEST_ASSERT_EQUAL_STRING_LEN(shortOne, longOne, 14);
  TEST_ASSERT_EQUAL('-', longOne[14]);
}

static std::vector<uint8_t> unhex(const char* text) {
  std::vector<uint8_t> out;
  for (size_t i = 0; text[i] && text[i + 1]; i += 2) out.push_back((uint8_t)strtoul(std::string(text + i, 2).c_str(), nullptr, 16));
  return out;
}

// SHA-512 against FIPS 180, and Ed25519 against RFC 8032's tests 1 and 2: the parts a firmware signature is checked with.
static void ed25519_matches_rfc_8032() {
  uint8_t hash[64];
  crypto_sha512(hash, (const uint8_t*)"abc", 3);
  TEST_ASSERT_EQUAL_STRING(
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
      camHex(hash, 64).c_str());
  std::vector<uint8_t> key = unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  std::vector<uint8_t> sig =
      unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  TEST_ASSERT_EQUAL(0, crypto_ed25519_check(sig.data(), key.data(), nullptr, 0));
  key = unhex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
  sig = unhex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
  uint8_t message = 0x72;
  TEST_ASSERT_EQUAL(0, crypto_ed25519_check(sig.data(), key.data(), &message, 1));
  message = 0x73;
  TEST_ASSERT_NOT_EQUAL(0, crypto_ed25519_check(sig.data(), key.data(), &message, 1));
}

// A 300-byte image signed by tools/sign_firmware.py with RFC 8032's first key, taken in pieces of every size: the
// image's bytes go to flash and the trailer does not, and only the whole, unchanged file with the right key is good.
static void a_firmware_signature_is_checked_whole() {
  std::vector<uint8_t> file(1, 0xE9);
  for (int i = 1; i < 300; i++) file.push_back((uint8_t)(i * 7));
  std::vector<uint8_t> sig =
      unhex("697f69c94828ac0023794e149840c27c331cc20bbbc646e24938981b480da87e101cfe0767dcd710d1a2ca519539eb2c83266e88f9c24e72baeef0c6a550e003");
  file.insert(file.end(), sig.begin(), sig.end());
  file.insert(file.end(), S2O_FW_MAGIC, S2O_FW_MAGIC + 8);
  uint8_t key[32], other[32];
  TEST_ASSERT_TRUE(s2oFirmwareKey("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key));
  TEST_ASSERT_TRUE(s2oFirmwareKey("3D4017C3E843895A92B70AA74D1B7EBC9C982CCF2EC4968CC0CD55F12AF4660C", other));
  TEST_ASSERT_FALSE(s2oFirmwareKey("d75a98", other));
  TEST_ASSERT_FALSE(s2oFirmwareKey(std::string(63, '0') + "g", other));
  TEST_ASSERT_FALSE(s2oFirmwareKey("", other));
  TEST_ASSERT_TRUE(s2oFirmwareKey("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", other));

  auto run = [&](const std::vector<uint8_t>& f, size_t piece, const uint8_t* k, size_t total, size_t* image) {
    S2oFirmwareSig s;
    if (!s.begin(total)) return false;
    size_t got = 0;
    for (size_t at = 0; at < f.size(); at += piece) got += s.take(f.data() + at, f.size() - at < piece ? f.size() - at : piece);
    if (image) *image = got;
    return s.good(k);
  };
  for (size_t piece : {1u, 7u, 64u, 71u, 72u, 73u, 300u, 372u, 1000u}) {
    size_t image = 0;
    TEST_ASSERT_TRUE_MESSAGE(run(file, piece, key, file.size(), &image), std::to_string(piece).c_str());
    TEST_ASSERT_EQUAL(300, image);
    TEST_ASSERT_FALSE(run(file, piece, other, file.size(), nullptr));
  }
  {  // the hub's own firmware never passes for a node's, nor the other way
    S2oFirmwareSig s;
    s.begin(file.size());
    s.take(file.data(), file.size());
    TEST_ASSERT_FALSE(s.good(key, S2O_FW_NODE_CONTEXT));
  }
  for (size_t at : {0u, 150u, 299u, 300u, 363u, 364u, 371u}) {  // a byte changed in the image, signature or end mark
    std::vector<uint8_t> changed = file;
    changed[at] ^= 0x20;
    TEST_ASSERT_FALSE_MESSAGE(run(changed, 50, key, file.size(), nullptr), std::to_string(at).c_str());
  }
  std::vector<uint8_t> shorter(file.begin(), file.end() - 1);
  TEST_ASSERT_FALSE(run(shorter, 50, key, shorter.size(), nullptr));  // a byte missing
  TEST_ASSERT_FALSE(run(shorter, 50, key, file.size(), nullptr));     // fewer bytes than said
  TEST_ASSERT_FALSE(run(file, 50, key, file.size() - 1, nullptr));    // more bytes than said
  std::vector<uint8_t> unsigned_(file.begin(), file.begin() + 300);
  TEST_ASSERT_FALSE(run(unsigned_, 50, key, unsigned_.size(), nullptr));
  S2oFirmwareSig s;
  TEST_ASSERT_FALSE(s.begin(S2O_FW_TRAILER));  // no room for an image
  TEST_ASSERT_FALSE(s.good(key));
  TEST_ASSERT_TRUE(s.begin(file.size()));  // and checked once only
  s.take(file.data(), file.size());
  TEST_ASSERT_TRUE(s.good(key));
  TEST_ASSERT_FALSE(s.good(key));
}

// The same image with release number 4002 at the end of its signed part: read back whatever pieces it comes in, and
// none from a file without one.
static void a_firmware_release_number_is_read_from_the_signed_part() {
  std::vector<uint8_t> file(1, 0xE9);
  for (int i = 1; i < 300; i++) file.push_back((uint8_t)(i * 7));
  const uint8_t number[4] = {0xA2, 0x0F, 0, 0};  // 4002
  file.insert(file.end(), number, number + 4);
  file.insert(file.end(), S2O_FW_RELEASE_MAGIC, S2O_FW_RELEASE_MAGIC + 8);
  std::vector<uint8_t> sig =
      unhex("d9b6ab324e750c168b569c6c42631bc0fa9dd79fc36449ad17de893a93162337f031b6c0b4d0ab360e5ec76ad7fbbf553ce11f9f13ac4d1a57fa1e5f5e8d5204");
  file.insert(file.end(), sig.begin(), sig.end());
  file.insert(file.end(), S2O_FW_MAGIC, S2O_FW_MAGIC + 8);
  uint8_t key[32];
  TEST_ASSERT_TRUE(s2oFirmwareKey("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", key));
  for (size_t piece : {1u, 5u, 11u, 12u, 13u, 64u, 311u, 312u, 1000u}) {
    S2oFirmwareSig s;
    TEST_ASSERT_TRUE(s.begin(file.size()));
    for (size_t at = 0; at < file.size(); at += piece) s.take(file.data() + at, file.size() - at < piece ? file.size() - at : piece);
    TEST_ASSERT_TRUE_MESSAGE(s.good(key), std::to_string(piece).c_str());
    TEST_ASSERT_EQUAL_INT64_MESSAGE(4002, s.release(), std::to_string(piece).c_str());
  }
  S2oFirmwareSig half;  // not all of it yet
  half.begin(file.size());
  half.take(file.data(), 200);
  TEST_ASSERT_EQUAL_INT64(-1, half.release());
  std::vector<uint8_t> plain(1, 0xE9);  // the first signed format: no number
  for (int i = 1; i < 300; i++) plain.push_back((uint8_t)(i * 7));
  plain.resize(plain.size() + S2O_FW_TRAILER);
  S2oFirmwareSig none;
  none.begin(plain.size());
  none.take(plain.data(), plain.size());
  TEST_ASSERT_EQUAL_INT64(-1, none.release());
}

// A backup sealed with a passphrase: read back only with it, and refused whole when anything in it was changed.
static void a_backup_opens_only_with_its_passphrase() {
  std::vector<uint8_t> work(CAM_BACKUP_MAX_BLOCKS * 1024);
  uint8_t salt[16], nonce[24];
  for (int i = 0; i < 16; i++) salt[i] = (uint8_t)i;
  for (int i = 0; i < 24; i++) nonce[i] = (uint8_t)(100 + i);
  std::string plain = "sensorcam-backup 1\nnet.webpw=%73%65%63%72%65%74\npairing.AA-BB-CC-DD-EE-01=00,11\n";
  std::string sealed = camSealBackup(plain, "correct horse", salt, nonce, work.data());
  TEST_ASSERT_TRUE(camBackupIsSealed(sealed));
  TEST_ASSERT_TRUE(sealed.find("secret") == std::string::npos && sealed.find("%73") == std::string::npos);  // nothing readable
  uint32_t blocks = 0, passes = 0;
  TEST_ASSERT_TRUE(camBackupParams(sealed, blocks, passes));
  TEST_ASSERT_EQUAL_UINT32(CAM_BACKUP_BLOCKS, blocks);
  TEST_ASSERT_EQUAL_UINT32(CAM_BACKUP_PASSES, passes);
  std::string out;
  TEST_ASSERT_TRUE(camOpenBackup(sealed, "correct horse", out, work.data()));
  TEST_ASSERT_TRUE(out == plain);
  TEST_ASSERT_FALSE(camOpenBackup(sealed, "correct horsE", out, work.data()));  // one letter off
  std::string fewer = sealed;
  fewer.replace(fewer.find(" 48\n"), 4, " 47\n");  // fewer passes asked for, to make guessing cheaper: the head is sealed too
  TEST_ASSERT_FALSE(camOpenBackup(fewer, "correct horse", out, work.data()));
  std::string changed = sealed;
  changed[changed.size() - 5] = changed[changed.size() - 5] == 'A' ? 'B' : 'A';
  TEST_ASSERT_FALSE(camOpenBackup(changed, "correct horse", out, work.data()));
  TEST_ASSERT_FALSE(camOpenBackup(sealed.substr(0, sealed.size() / 2), "correct horse", out, work.data()));
  // A file may not ask for more memory or passes than the hub allows, nor be read as one when it is plain.
  TEST_ASSERT_FALSE(camBackupParams("sensorcam-backup sealed 1 4096 48\nAAAA\n", blocks, passes));
  TEST_ASSERT_FALSE(camBackupParams("sensorcam-backup sealed 1 32 100000\nAAAA\n", blocks, passes));
  TEST_ASSERT_FALSE(camBackupParams("sensorcam-backup sealed 1 32\nAAAA\n", blocks, passes));
  TEST_ASSERT_FALSE(camBackupIsSealed(plain));
  TEST_ASSERT_FALSE(camValidPassphrase("seven77"));
  TEST_ASSERT_TRUE(camValidPassphrase("eight888"));
  TEST_ASSERT_FALSE(camValidPassphrase(std::string(129, 'x')));
}

void runCryptoTests() {
  RUN_TEST(a_backup_opens_only_with_its_passphrase);
  RUN_TEST(a_firmware_release_number_is_read_from_the_signed_part);
  RUN_TEST(ed25519_matches_rfc_8032);
  RUN_TEST(a_firmware_signature_is_checked_whole);
  RUN_TEST(the_long_fingerprint_starts_with_the_short_one);
  RUN_TEST(only_the_hubs_own_names_are_served);
  RUN_TEST(wrong_passwords_lock_out_quick_and_slow_guessing);
  RUN_TEST(xml_text_is_unescaped_and_secrets_compared_whole);
  RUN_TEST(base64_matches_known_values);
  RUN_TEST(hmac_sha1_matches_rfc_2202);
  RUN_TEST(sha1_in_pieces_matches);
  RUN_TEST(x25519_matches_rfc_7748);
}
