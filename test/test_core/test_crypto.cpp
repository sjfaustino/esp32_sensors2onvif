// Hashes, keys and passwords, against the standards' own test values.
#include "tests.h"

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

void runCryptoTests() {
  RUN_TEST(only_the_hubs_own_names_are_served);
  RUN_TEST(wrong_passwords_lock_out_quick_and_slow_guessing);
  RUN_TEST(xml_text_is_unescaped_and_secrets_compared_whole);
  RUN_TEST(base64_matches_known_values);
  RUN_TEST(hmac_sha1_matches_rfc_2202);
  RUN_TEST(sha1_in_pieces_matches);
  RUN_TEST(x25519_matches_rfc_7748);
}
