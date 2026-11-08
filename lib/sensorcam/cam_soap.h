#pragma once
#include <stdint.h>
#include <time.h>
#include <string>

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
  std::string serial;
  time_t now = 0;
};

// Device and media calls, independent of event state. Empty when the action is unknown.
std::string soapDeviceReply(const std::string& action, const SoapContext& ctx);
std::string soapEnvelope(const std::string& body);
std::string soapFault(const char* reason);

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
