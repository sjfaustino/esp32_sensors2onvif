#pragma once
#include <stdint.h>
#include <time.h>
#include <string>

std::string iso(time_t t);  // UTC, e.g. 2026-11-08T17:00:00Z

// ---- request parsing ----
std::string soapAction(const std::string& body);       // local name of the first element in Body
std::string soapConsumerAddress(const std::string& body);  // Subscribe's ConsumerReference address
std::string xmlFirstText(const std::string& xml, const char* localName);

struct HttpUrl {
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
};
bool parseHttpUrl(const std::string& url, HttpUrl& out);  // http:// only

std::string headerValue(const std::string& headers, const char* lowerName);
std::string queryParam(const std::string& target, const char* name);

// ---- replies ----
struct SoapContext {
  std::string host;  // host[:port] the client used to reach us
  uint16_t rtspPort = 554;
  uint16_t onvifPort = 80;
  std::string serial;
  time_t now = 0;
  // The hub's own network details, for clients that display them.
  std::string ip = "0.0.0.0", mac = "00:00:00:00:00:00", gateway = "0.0.0.0", dns = "0.0.0.0", hostName = "hub", ntpServer;
  int prefixLength = 24;
  bool dhcp = true;
  std::string snapshotToken;  // added to the snapshot address when ONVIF has a password
  std::string firmwareVersion = "0";
  bool timeFromNtp = true;
};

// Device and media calls, independent of event state. Empty when the action is unknown.
std::string soapDeviceReply(const std::string& action, const SoapContext& ctx);
std::string soapEnvelope(const std::string& body);
std::string soapFault(const char* reason);
std::string soapNotAuthorized();

std::string soapSubscribeReply(const SoapContext& ctx, uint32_t subId);
std::string soapPullPointReply(const SoapContext& ctx, uint32_t subId);
std::string soapPullMessagesReply(const SoapContext& ctx, bool includeState, bool active, bool initial);
std::string soapRenewReply(const SoapContext& ctx);
std::string soapUnsubscribeReply();
std::string soapNotify(bool active, bool initial, time_t now);

// ---- WS-Discovery ----
std::string discoveryProbeMatch(const std::string& relatesTo, const std::string& ip, uint16_t onvifPort,
                                const std::string& serial, uint32_t messageNo);
std::string discoveryHello(const std::string& ip, uint16_t onvifPort, const std::string& serial, uint32_t messageNo);
