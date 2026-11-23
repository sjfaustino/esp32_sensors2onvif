// The words of the hub's own messages, its notifications, the daily check and the weekly summary, in the language chosen
// under Notifications: English, or one of the languages in i18n/, whose phrases with the context "message" the build
// puts in cam_text_data.cpp (tools/i18n.py). Each message is a whole sentence with {0}, {1} and so on for what changes, so
// each language keeps its own word order, and a count takes the form the language's own plural rule gives. The English
// is the key. What goes to Home Assistant and the event history as names, the event types, stays English.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <string>

// One phrase of one language: its English, its translation, and which plural form it is, 0 for a phrase without.
struct CamPhrase {
  const char* en;
  const char* text;
  uint8_t form;
};
struct CamLanguageText {
  const char* code;  // such as "pt"
  const char* name;  // in itself, such as "Português"
  const CamPhrase* phrases;
  size_t count;
  int (*plural)(long long n);  // which form a count takes
};
extern const CamLanguageText CAM_LANGUAGES[];  // in cam_text_data.cpp, made by the build from i18n/*.po
extern const size_t CAM_LANGUAGE_COUNT;

bool camTextLanguageKnown(const std::string& code);  // "en", or a language the build put in
void camSetTextLanguage(const std::string& code);    // one not known is English
std::string camTextLanguage();

// The phrase in the language chosen, or the English itself when there is none.
const char* camText(const char* en);
// The same, with {0}, {1} and so on filled in order.
std::string camText(const char* en, std::initializer_list<std::string> values);
// A phrase by a count, with {0} for it: one and many are the English; the language has as many forms as it needs.
std::string camCount(long long n, const char* one, const char* many);
// The same, with the values given, the count among them where the phrase says, such as "{0}: about {1} days".
std::string camCount(long long n, const char* one, const char* many, std::initializer_list<std::string> values);
