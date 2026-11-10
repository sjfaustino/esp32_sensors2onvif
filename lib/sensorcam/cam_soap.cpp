#include "cam_soap.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cam_frames.h"

// ---- request parsing ----

namespace {
struct Tag {
  std::string local;
  bool closing = false;
  bool selfClosed = false;
  size_t end = 0;  // index of '>'
};

bool nextTag(const std::string& s, size_t& pos, Tag& tag) {
  while ((pos = s.find('<', pos)) != std::string::npos) {
    if (s.compare(pos, 4, "<!--") == 0) {
      pos = s.find("-->", pos);
      if (pos == std::string::npos) return false;
      continue;
    }
    if (s.compare(pos, 2, "<?") == 0 || s.compare(pos, 2, "<!") == 0) {
      pos++;
      continue;
    }
    size_t end = s.find('>', pos);
    if (end == std::string::npos) return false;
    size_t i = pos + 1;
    tag.closing = i < end && s[i] == '/';
    if (tag.closing) i++;
    size_t stop = i;
    while (stop < end && !isspace((unsigned char)s[stop]) && s[stop] != '/') stop++;
    std::string name = s.substr(i, stop - i);
    size_t colon = name.find(':');
    tag.local = colon == std::string::npos ? name : name.substr(colon + 1);
    tag.selfClosed = s[end - 1] == '/';
    tag.end = end;
    pos = end + 1;
    return true;
  }
  return false;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

std::string textAfter(const std::string& s, const Tag& tag) {
  size_t stop = s.find('<', tag.end + 1);
  return stop == std::string::npos ? "" : trim(s.substr(tag.end + 1, stop - tag.end - 1));
}
}  // namespace

std::string soapAction(const std::string& body) {
  size_t pos = 0;
  Tag t;
  while (nextTag(body, pos, t)) {
    if (t.closing || t.local != "Body") continue;
    if (t.selfClosed) return "";
    return nextTag(body, pos, t) && !t.closing ? t.local : "";
  }
  return "";
}

std::string soapConsumerAddress(const std::string& body) {
  size_t pos = 0;
  Tag t;
  bool inside = false;
  while (nextTag(body, pos, t)) {
    if (t.closing) {
      if (t.local == "ConsumerReference") inside = false;
      continue;
    }
    if (t.local == "ConsumerReference") inside = true;
    else if (inside && t.local == "Address") return textAfter(body, t);
  }
  return "";
}

std::string xmlFirstText(const std::string& xml, const char* localName) {
  size_t pos = 0;
  Tag t;
  while (nextTag(xml, pos, t))
    if (!t.closing && t.local == localName) return t.selfClosed ? "" : textAfter(xml, t);
  return "";
}

bool parseHttpUrl(const std::string& url, HttpUrl& out) {
  const char prefix[] = "http://";
  if (url.compare(0, sizeof(prefix) - 1, prefix) != 0) return false;
  size_t start = sizeof(prefix) - 1;
  size_t slash = url.find('/', start);
  std::string authority = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  size_t colon = authority.find(':');
  out.host = authority.substr(0, colon);
  out.port = 80;
  if (colon != std::string::npos) {
    int port = atoi(authority.c_str() + colon + 1);
    if (port <= 0 || port > 65535) return false;
    out.port = (uint16_t)port;
  }
  return !out.host.empty();
}

std::string headerValue(const std::string& headers, const char* lowerName) {
  size_t nameLen = strlen(lowerName);
  size_t pos = 0;
  while (pos < headers.size()) {
    size_t eol = headers.find("\r\n", pos);
    if (eol == std::string::npos) eol = headers.size();
    if (eol - pos > nameLen && headers[pos + nameLen] == ':') {
      bool same = true;
      for (size_t i = 0; i < nameLen && same; i++) same = tolower((unsigned char)headers[pos + i]) == lowerName[i];
      if (same) return trim(headers.substr(pos + nameLen + 1, eol - pos - nameLen - 1));
    }
    pos = eol + 2;
  }
  return "";
}

std::string queryParam(const std::string& target, const char* name) {
  size_t q = target.find('?');
  if (q == std::string::npos) return "";
  std::string key = std::string(name) + "=";
  size_t pos = q + 1;
  while (pos < target.size()) {
    size_t amp = target.find('&', pos);
    if (amp == std::string::npos) amp = target.size();
    if (target.compare(pos, key.size(), key) == 0) return target.substr(pos + key.size(), amp - pos - key.size());
    pos = amp + 1;
  }
  return "";
}

// ---- replies ----

std::string iso(time_t t) {
  struct tm* u = gmtime(&t);
  char b[32];
  strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", u);
  return b;
}

static std::string num(int v) { return std::to_string(v); }

std::string soapEnvelope(const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
         "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
         "xmlns:tt=\"http://www.onvif.org/ver10/schema\" xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
         "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" "
         "xmlns:tns1=\"http://www.onvif.org/ver10/topics\" xmlns:wstop=\"http://docs.oasis-open.org/wsn/t-1\"><s:Body>" +
         body + "</s:Body></s:Envelope>";
}

std::string soapFault(const char* reason) {
  return soapEnvelope(std::string("<s:Fault><s:Code><s:Value>s:Receiver</s:Value></s:Code><s:Reason><s:Text xml:lang=\"en\">") +
                      reason + "</s:Text></s:Reason></s:Fault>");
}

std::string soapNotAuthorized() {
  return soapEnvelope("<s:Fault><s:Code><s:Value>s:Sender</s:Value><s:Subcode><s:Value xmlns:ter=\"http://www.onvif.org/ver10/error\">ter:NotAuthorized</s:Value></s:Subcode></s:Code>"
                      "<s:Reason><s:Text xml:lang=\"en\">Sender not authorized</s:Text></s:Reason></s:Fault>");
}

static std::string encoderBody() {
  std::string w = num(CAM_WIDTH), h = num(CAM_HEIGHT), f = num(CAM_FPS);
  return "<tt:Name>Enc</tt:Name><tt:UseCount>1</tt:UseCount><tt:Encoding>H264</tt:Encoding><tt:Resolution><tt:Width>" + w +
         "</tt:Width><tt:Height>" + h + "</tt:Height></tt:Resolution><tt:Quality>4</tt:Quality><tt:RateControl><tt:FrameRateLimit>" + f +
         "</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>256</tt:BitrateLimit></tt:RateControl>"
         "<tt:H264><tt:GovLength>1</tt:GovLength><tt:H264Profile>Baseline</tt:H264Profile></tt:H264>"
         "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port>"
         "<tt:TTL>0</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast><tt:SessionTimeout>PT60S</tt:SessionTimeout>";
}

static std::string sourceConfig() {
  return "<tt:Name>Src</tt:Name><tt:UseCount>1</tt:UseCount><tt:SourceToken>src</tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"" +
         num(CAM_WIDTH) + "\" height=\"" + num(CAM_HEIGHT) + "\"/>";
}

static std::string profile(const char* tag) {
  std::string t(tag);
  return "<" + t + " token=\"main\" fixed=\"true\"><tt:Name>main</tt:Name><tt:VideoSourceConfiguration token=\"srccfg\">" + sourceConfig() +
         "</tt:VideoSourceConfiguration><tt:VideoEncoderConfiguration token=\"enc\">" + encoderBody() +
         "</tt:VideoEncoderConfiguration></" + t + ">";
}

static std::string serviceEntry(const char* ns, const std::string& addr) {
  return std::string("<tds:Service><tds:Namespace>") + ns + "</tds:Namespace><tds:XAddr>" + addr +
         "</tds:XAddr><tds:Version><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tds:Version></tds:Service>";
}

std::string soapDeviceReply(const std::string& a, const SoapContext& c) {
  std::string base = "http://" + c.host + "/onvif/";
  std::string device = base + "device_service", media = base + "media", events = base + "events";
  std::string w = num(CAM_WIDTH), h = num(CAM_HEIGHT), f = num(CAM_FPS);

  if (a == "GetSystemDateAndTime") {
    time_t now = c.now;
    struct tm* u = gmtime(&now);
    return std::string("<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime><tt:DateTimeType>") + (c.timeFromNtp ? "NTP" : "Manual") + "</tt:DateTimeType><tt:DaylightSavings>false"
           "</tt:DaylightSavings><tt:TimeZone><tt:TZ>UTC0</tt:TZ></tt:TimeZone><tt:UTCDateTime><tt:Time><tt:Hour>" + num(u->tm_hour) +
           "</tt:Hour><tt:Minute>" + num(u->tm_min) + "</tt:Minute><tt:Second>" + num(u->tm_sec) + "</tt:Second></tt:Time><tt:Date><tt:Year>" +
           num(u->tm_year + 1900) + "</tt:Year><tt:Month>" + num(u->tm_mon + 1) + "</tt:Month><tt:Day>" + num(u->tm_mday) +
           "</tt:Day></tt:Date></tt:UTCDateTime></tds:SystemDateAndTime></tds:GetSystemDateAndTimeResponse>";
  }
  if (a == "GetDeviceInformation")
    return "<tds:GetDeviceInformationResponse><tds:Manufacturer>SensorCam</tds:Manufacturer><tds:Model>ESP32</tds:Model>"
           "<tds:FirmwareVersion>" + c.firmwareVersion + "</tds:FirmwareVersion><tds:SerialNumber>" + c.serial +
           "</tds:SerialNumber><tds:HardwareId>1.0</tds:HardwareId></tds:GetDeviceInformationResponse>";
  if (a == "GetCapabilities")
    return "<tds:GetCapabilitiesResponse><tds:Capabilities><tt:Device><tt:XAddr>" + device + "</tt:XAddr></tt:Device><tt:Media><tt:XAddr>" + media +
           "</tt:XAddr><tt:StreamingCapabilities><tt:RTPMulticast>false</tt:RTPMulticast><tt:RTP_TCP>true</tt:RTP_TCP><tt:RTP_RTSP_TCP>true"
           "</tt:RTP_RTSP_TCP></tt:StreamingCapabilities></tt:Media><tt:Events><tt:XAddr>" + events +
           "</tt:XAddr><tt:WSSubscriptionPolicySupport>false</tt:WSSubscriptionPolicySupport><tt:WSPullPointSupport>true</tt:WSPullPointSupport>"
           "<tt:WSPausableSubscriptionManagerInterfaceSupport>false</tt:WSPausableSubscriptionManagerInterfaceSupport></tt:Events>"
           "</tds:Capabilities></tds:GetCapabilitiesResponse>";
  if (a == "GetServices")
    return "<tds:GetServicesResponse>" + serviceEntry("http://www.onvif.org/ver10/device/wsdl", device) +
           serviceEntry("http://www.onvif.org/ver10/media/wsdl", media) + serviceEntry("http://www.onvif.org/ver10/events/wsdl", events) +
           "</tds:GetServicesResponse>";
  if (a == "GetScopes")
    return "<tds:GetScopesResponse><tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/type/video_encoder</tt:ScopeItem></tds:Scopes>"
           "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/name/SensorCam</tt:ScopeItem></tds:Scopes></tds:GetScopesResponse>";
  if (a == "GetProfiles") return "<trt:GetProfilesResponse>" + profile("trt:Profiles") + "</trt:GetProfilesResponse>";
  if (a == "GetProfile") return "<trt:GetProfileResponse>" + profile("trt:Profile") + "</trt:GetProfileResponse>";
  if (a == "GetStreamUri") {
    std::string ip = c.host.substr(0, c.host.find(':'));
    return "<trt:GetStreamUriResponse><trt:MediaUri><tt:Uri>rtsp://" + ip + ":" + num(c.rtspPort) +
           "/stream</tt:Uri><tt:InvalidAfterConnect>false</tt:InvalidAfterConnect><tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
           "<tt:Timeout>PT60S</tt:Timeout></trt:MediaUri></trt:GetStreamUriResponse>";
  }
  if (a == "GetSnapshotUri")
    return "<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>http://" + c.host + "/onvif/snapshot" + (c.snapshotToken.empty() ? std::string() : "?token=" + c.snapshotToken) + "</tt:Uri><tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
           "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot><tt:Timeout>PT60S</tt:Timeout></trt:MediaUri></trt:GetSnapshotUriResponse>";
  if (a == "GetVideoSources")
    return "<trt:GetVideoSourcesResponse><trt:VideoSources token=\"src\"><tt:Framerate>" + f + "</tt:Framerate><tt:Resolution><tt:Width>" + w +
           "</tt:Width><tt:Height>" + h + "</tt:Height></tt:Resolution></trt:VideoSources></trt:GetVideoSourcesResponse>";
  if (a == "GetVideoSourceConfigurations")
    return "<trt:GetVideoSourceConfigurationsResponse><trt:Configurations token=\"srccfg\">" + sourceConfig() + "</trt:Configurations></trt:GetVideoSourceConfigurationsResponse>";
  if (a == "GetVideoSourceConfiguration")
    return "<trt:GetVideoSourceConfigurationResponse><trt:Configuration token=\"srccfg\">" + sourceConfig() + "</trt:Configuration></trt:GetVideoSourceConfigurationResponse>";
  if (a == "GetVideoEncoderConfigurations")
    return "<trt:GetVideoEncoderConfigurationsResponse><trt:Configurations token=\"enc\">" + encoderBody() + "</trt:Configurations></trt:GetVideoEncoderConfigurationsResponse>";
  if (a == "GetVideoEncoderConfiguration")
    return "<trt:GetVideoEncoderConfigurationResponse><trt:Configuration token=\"enc\">" + encoderBody() + "</trt:Configuration></trt:GetVideoEncoderConfigurationResponse>";
  if (a == "GetVideoEncoderConfigurationOptions")
    return "<trt:GetVideoEncoderConfigurationOptionsResponse><trt:Options><tt:QualityRange><tt:Min>1</tt:Min><tt:Max>5</tt:Max></tt:QualityRange><tt:H264><tt:ResolutionsAvailable><tt:Width>" + w +
           "</tt:Width><tt:Height>" + h + "</tt:Height></tt:ResolutionsAvailable><tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:GovLengthRange><tt:FrameRateRange><tt:Min>" + f +
           "</tt:Min><tt:Max>" + f + "</tt:Max></tt:FrameRateRange><tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange><tt:H264ProfilesSupported>Baseline</tt:H264ProfilesSupported></tt:H264></trt:Options></trt:GetVideoEncoderConfigurationOptionsResponse>";
  if (a == "SetVideoEncoderConfiguration") return "<trt:SetVideoEncoderConfigurationResponse/>";
  if (a == "SetSynchronizationPoint") return "<trt:SetSynchronizationPointResponse/>";
  if (a == "SetSystemDateAndTime") return "<tds:SetSystemDateAndTimeResponse/>";
  if (a == "GetNetworkInterfaces") {
    std::string address = "<tt:Address>" + c.ip + "</tt:Address><tt:PrefixLength>" + num(c.prefixLength) + "</tt:PrefixLength>";
    return "<tds:GetNetworkInterfacesResponse><tds:NetworkInterfaces token=\"wlan0\"><tt:Enabled>true</tt:Enabled><tt:Info><tt:Name>wlan0</tt:Name><tt:HwAddress>" + c.mac +
           "</tt:HwAddress><tt:MTU>1500</tt:MTU></tt:Info><tt:IPv4><tt:Enabled>true</tt:Enabled><tt:Config>" +
           (c.dhcp ? "<tt:FromDHCP>" + address + "</tt:FromDHCP><tt:DHCP>true</tt:DHCP>" : "<tt:Manual>" + address + "</tt:Manual><tt:DHCP>false</tt:DHCP>") +
           "</tt:Config></tt:IPv4></tds:NetworkInterfaces></tds:GetNetworkInterfacesResponse>";
  }
  if (a == "GetHostname")
    return "<tds:GetHostnameResponse><tds:HostnameInformation><tt:FromDHCP>false</tt:FromDHCP><tt:Name>" + c.hostName + "</tt:Name></tds:HostnameInformation></tds:GetHostnameResponse>";
  if (a == "GetDNS") {
    std::string entry = "<tt:Type>IPv4</tt:Type><tt:IPv4Address>" + c.dns + "</tt:IPv4Address>";
    return std::string("<tds:GetDNSResponse><tds:DNSInformation><tt:FromDHCP>") + (c.dhcp ? "true" : "false") + "</tt:FromDHCP>" +
           (c.dhcp ? "<tt:DNSFromDHCP>" + entry + "</tt:DNSFromDHCP>" : "<tt:DNSManual>" + entry + "</tt:DNSManual>") + "</tds:DNSInformation></tds:GetDNSResponse>";
  }
  if (a == "GetNTP")
    return "<tds:GetNTPResponse><tds:NTPInformation><tt:FromDHCP>false</tt:FromDHCP><tt:NTPManual><tt:Type>DNS</tt:Type><tt:DNSname>" + c.ntpServer +
           "</tt:DNSname></tt:NTPManual></tds:NTPInformation></tds:GetNTPResponse>";
  if (a == "GetNetworkDefaultGateway")
    return "<tds:GetNetworkDefaultGatewayResponse><tds:NetworkGateway><tt:IPv4Address>" + c.gateway + "</tt:IPv4Address></tds:NetworkGateway></tds:GetNetworkDefaultGatewayResponse>";
  if (a == "GetNetworkProtocols")
    return "<tds:GetNetworkProtocolsResponse><tds:NetworkProtocols><tt:Name>HTTP</tt:Name><tt:Enabled>true</tt:Enabled><tt:Port>" + num(c.onvifPort) +
           "</tt:Port></tds:NetworkProtocols><tds:NetworkProtocols><tt:Name>RTSP</tt:Name><tt:Enabled>true</tt:Enabled><tt:Port>" + num(c.rtspPort) +
           "</tt:Port></tds:NetworkProtocols></tds:GetNetworkProtocolsResponse>";
  if (a == "GetUsers") return "<tds:GetUsersResponse><tds:User><tt:Username>admin</tt:Username><tt:UserLevel>Administrator</tt:UserLevel></tds:User></tds:GetUsersResponse>";
  if (a == "GetDiscoveryMode") return "<tds:GetDiscoveryModeResponse><tds:DiscoveryMode>Discoverable</tds:DiscoveryMode></tds:GetDiscoveryModeResponse>";
  if (a == "GetAudioSources") return "<trt:GetAudioSourcesResponse/>";
  if (a == "GetAudioEncoderConfigurations") return "<trt:GetAudioEncoderConfigurationsResponse/>";
  if (a == "GetAudioSourceConfigurations") return "<trt:GetAudioSourceConfigurationsResponse/>";
  if (a == "GetEventProperties")
    return "<tev:GetEventPropertiesResponse><tev:TopicNamespaceLocation>http://www.onvif.org/onvif/ver10/topics/topicns.xml</tev:TopicNamespaceLocation><wsnt:FixedTopicSet>true</wsnt:FixedTopicSet>"
           "<wstop:TopicSet><tns1:VideoSource><MotionAlarm wstop:topic=\"true\"/></tns1:VideoSource><tns1:RuleEngine><CellMotionDetector><Motion wstop:topic=\"true\"/></CellMotionDetector></tns1:RuleEngine></wstop:TopicSet>"
           "<wsnt:TopicExpressionDialect>http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet</wsnt:TopicExpressionDialect>"
           "<tev:MessageContentFilterDialect>http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter</tev:MessageContentFilterDialect></tev:GetEventPropertiesResponse>";
  if (a == "GetServiceCapabilities")
    return "<tev:GetServiceCapabilitiesResponse><tev:Capabilities WSSubscriptionPolicySupport=\"false\" WSPullPointSupport=\"true\" WSPausableSubscriptionManagerInterfaceSupport=\"false\" MaxNotificationProducers=\"4\" MaxPullPoints=\"4\"/></tev:GetServiceCapabilitiesResponse>";
  return "";
}

static std::string subscriptionTimes(const char* prefix, time_t now) {
  std::string p(prefix);
  return "<" + p + ":CurrentTime>" + iso(now) + "</" + p + ":CurrentTime><" + p + ":TerminationTime>" + iso(now + 300) + "</" + p + ":TerminationTime>";
}

static std::string subscriptionAddress(const SoapContext& c, uint32_t id) {
  return "<wsa:Address>http://" + c.host + "/onvif/sub/" + std::to_string(id) + "</wsa:Address>";
}

static std::string notificationMessages(bool active, bool initial, time_t now) {
  static const char* const topics[2][2] = {{"tns1:VideoSource/MotionAlarm", "State"}, {"tns1:RuleEngine/CellMotionDetector/Motion", "IsMotion"}};
  std::string out;
  for (auto& t : topics) {
    out += std::string("<wsnt:NotificationMessage><wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">") + t[0] +
           "</wsnt:Topic><wsnt:Message><tt:Message UtcTime=\"" + iso(now) + "\" PropertyOperation=\"" + (initial ? "Initialized" : "Changed") +
           "\"><tt:Source><tt:SimpleItem Name=\"Source\" Value=\"src\"/></tt:Source><tt:Data><tt:SimpleItem Name=\"" + t[1] + "\" Value=\"" +
           (active ? "true" : "false") + "\"/></tt:Data></tt:Message></wsnt:Message></wsnt:NotificationMessage>";
  }
  return out;
}

std::string soapSubscribeReply(const SoapContext& c, uint32_t id) {
  return "<wsnt:SubscribeResponse><wsnt:SubscriptionReference>" + subscriptionAddress(c, id) + "</wsnt:SubscriptionReference>" +
         subscriptionTimes("wsnt", c.now) + "</wsnt:SubscribeResponse>";
}

std::string soapPullPointReply(const SoapContext& c, uint32_t id) {
  return "<tev:CreatePullPointSubscriptionResponse><tev:SubscriptionReference>" + subscriptionAddress(c, id) + "</tev:SubscriptionReference>" +
         subscriptionTimes("wsnt", c.now) + "</tev:CreatePullPointSubscriptionResponse>";
}

std::string soapPullMessagesReply(const SoapContext& c, bool includeState, bool active, bool initial) {
  return "<tev:PullMessagesResponse>" + subscriptionTimes("tev", c.now) + (includeState ? notificationMessages(active, initial, c.now) : std::string()) +
         "</tev:PullMessagesResponse>";
}

std::string soapRenewReply(const SoapContext& c) {
  return "<wsnt:RenewResponse><wsnt:TerminationTime>" + iso(c.now + 300) + "</wsnt:TerminationTime><wsnt:CurrentTime>" + iso(c.now) +
         "</wsnt:CurrentTime></wsnt:RenewResponse>";
}

std::string soapUnsubscribeReply() { return "<wsnt:UnsubscribeResponse/>"; }

std::string soapNotify(bool active, bool initial, time_t now) {
  std::string xml = soapEnvelope("<wsnt:Notify>" + notificationMessages(active, initial, now) + "</wsnt:Notify>");
  size_t body = xml.find("<s:Body>");
  return xml.insert(body, "<s:Header><wsa:Action>http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify</wsa:Action></s:Header>");
}

// ---- WS-Discovery ----

static std::string deviceUuid(const std::string& serial) {
  std::string tail = serial;
  while (tail.size() < 12) tail = "0" + tail;
  return "urn:uuid:53656e73-6f72-4361-8d00-" + tail.substr(tail.size() - 12);
}

static std::string messageUuid(uint32_t no) {
  char b[64];
  snprintf(b, sizeof(b), "urn:uuid:53656e73-0000-4000-8000-%012lx", (unsigned long)no);
  return b;
}

static std::string discoveryEnvelope(const std::string& header, const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
         "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
         "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\"><s:Header>" + header + "</s:Header><s:Body>" + body + "</s:Body></s:Envelope>";
}

static std::string endpointFields(const std::string& ip, uint16_t port, const std::string& serial) {
  return "<a:EndpointReference><a:Address>" + deviceUuid(serial) + "</a:Address></a:EndpointReference><d:Types>dn:NetworkVideoTransmitter</d:Types>"
         "<d:Scopes>onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/name/SensorCam onvif://www.onvif.org/hardware/ESP32</d:Scopes>"
         "<d:XAddrs>http://" + ip + ":" + std::to_string(port) + "/onvif/device_service</d:XAddrs><d:MetadataVersion>1</d:MetadataVersion>";
}

std::string discoveryProbeMatch(const std::string& relatesTo, const std::string& ip, uint16_t port, const std::string& serial, uint32_t no) {
  return discoveryEnvelope("<a:MessageID>" + messageUuid(no) + "</a:MessageID><a:RelatesTo>" + relatesTo + "</a:RelatesTo><a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>"
                           "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</a:Action>",
                           "<d:ProbeMatches><d:ProbeMatch>" + endpointFields(ip, port, serial) + "</d:ProbeMatch></d:ProbeMatches>");
}

std::string discoveryHello(const std::string& ip, uint16_t port, const std::string& serial, uint32_t no) {
  return discoveryEnvelope("<a:MessageID>" + messageUuid(no) + "</a:MessageID><a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To><a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Hello</a:Action>",
                           "<d:Hello>" + endpointFields(ip, port, serial) + "</d:Hello>");
}
