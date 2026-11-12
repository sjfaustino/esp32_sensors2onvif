// Password checks for ONVIF (WS-Security) and RTSP (Digest and Basic).
#pragma once
#include <stdint.h>
#include <string>
#include "cam_timing.h"

std::string camSha1(const std::string& data);    // the raw 20 bytes
std::string camHmacSha1(const std::string& key, const std::string& data);  // the raw 20 bytes
std::string camMd5Hex(const std::string& data);  // lowercase hex
std::string camBase64Decode(const std::string& text);

// True when the SOAP request carries a UsernameToken for this user and password, as a digest or as plain text.
bool camWsseValid(const std::string& soapBody, const std::string& user, const std::string& password);

// One value from an Authorization header, such as response in: Digest username="a", response="b"
std::string camAuthParam(const std::string& header, const char* name);
bool camDigestValid(const std::string& authorization, const std::string& method, const std::string& user, const std::string& password, const std::string& realm, const std::string& nonce);
bool camBasicValid(const std::string& authorization, const std::string& user, const std::string& password);

// A value derived from the password and a secret kept on the hub, for a snapshot address that a client can fetch
// without logging in again.
std::string camSnapshotToken(const std::string& secret, const std::string& password);

// Whether two texts are equal, in a time that does not depend on where they differ.
bool camSameText(const std::string& a, const std::string& b);

// Whether a Host header names this hub: its IP address, its name alone or with .local or a home router's usual ending,
// the setup address, or one of the extra names set on the HTTP page (comma-separated). An empty header passes, for simple
// HTTP sensors; a browser always sends one. Anything else is a site that pointed its own name at the hub.
bool camKnownHost(const std::string& hostHeader, const std::string& ip, const std::string& hubName, const std::string& setupHost,
                  const std::string& extraNames);
// Up to 4 names, comma-separated, each of letters, digits, dots and hyphens, not starting or ending with a dot or hyphen.
bool camHostListValid(const std::string& names);

// Wrong passwords, counted per address. Ten in a row shut the address out for a minute, even with the right password.
// Each wrong one after that, possible only once a lock has run out, doubles the lock, up to 15 minutes. The count is
// kept for a day after the last wrong password, and cleared by a right one. So guessing in quick bursts and guessing
// slowly both get at most about 100 tries a day.
class CamLoginGuard {
 public:
  static const int FREE_TRIES = CAM_LOGIN_FREE_TRIES;
  static const uint32_t FIRST_LOCK_MS = CAM_LOGIN_FIRST_LOCK_MS, LONGEST_LOCK_MS = CAM_LOGIN_LONGEST_LOCK_MS, FORGET_MS = CAM_LOGIN_FORGET_MS;
  bool allowed(uint32_t peer, uint32_t now) const;
  void note(uint32_t peer, bool ok, uint32_t now);  // only for logins that offered a password

 private:
  struct Entry {
    uint32_t peer = 0;
    uint16_t failures = 0;
    uint32_t lastAt = 0;
  };
  static uint32_t lockMs(uint16_t failures);
  Entry entries_[8];
};
