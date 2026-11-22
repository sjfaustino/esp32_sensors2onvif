// The words of the hub's own messages, its notifications, the daily check and the weekly summary, in the language chosen
// under Notifications: English, or Portuguese (European). Each message is a whole sentence with {0}, {1} and so on for
// what changes, so each language keeps its own word order. The English is the key; cam_text.cpp has the Portuguese.
// What goes to Home Assistant and the event history as names, the event types, stays English, as automations match it.
#pragma once
#include <initializer_list>
#include <string>

enum CamLanguage { CAM_LANG_EN = 0, CAM_LANG_PT = 1 };

void camSetTextLanguage(int lang);  // a CamLanguage; anything else is English
int camTextLanguage();

// The phrase in the language chosen: its Portuguese, or the English itself when there is none.
const char* camText(const char* en);
// The same, with {0}, {1} and so on filled in order.
std::string camText(const char* en, std::initializer_list<std::string> values);
// One of two phrases by a count, each with {0} for it: the singular for 1 alone, in both languages.
std::string camCount(long long n, const char* one, const char* many);

// Every phrase of the table, for the test that checks each has the same {n} in both languages.
size_t camPhraseCount();
const char* camPhraseEnglish(size_t i);
const char* camPhrasePortuguese(size_t i);
