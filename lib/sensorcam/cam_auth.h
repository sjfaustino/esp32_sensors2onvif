// Password checks for ONVIF (WS-Security) and RTSP (Digest and Basic).
#pragma once
#include <string>

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

// A value derived from the password, for a snapshot address that a client can fetch without logging in again.
std::string camSnapshotToken(const std::string& password);
