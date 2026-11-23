#include "cam_text.h"

#include <string.h>

static const char* textCode = "en";                    // the code of the language chosen, from its table
static const CamLanguageText* textLanguage = nullptr;  // null for English

static const CamLanguageText* findLanguage(const std::string& code) {
  for (size_t i = 0; i < CAM_LANGUAGE_COUNT; i++)
    if (code == CAM_LANGUAGES[i].code) return &CAM_LANGUAGES[i];
  return nullptr;
}

bool camTextLanguageKnown(const std::string& code) { return code == "en" || findLanguage(code) != nullptr; }

void camSetTextLanguage(const std::string& code) {
  textLanguage = findLanguage(code);
  textCode = textLanguage ? textLanguage->code : "en";
}

std::string camTextLanguage() { return textCode; }

static const char* lookUp(const char* en, uint8_t form) {
  if (!textLanguage) return nullptr;
  for (size_t i = 0; i < textLanguage->count; i++) {
    const CamPhrase& p = textLanguage->phrases[i];
    if (p.form == form && strcmp(p.en, en) == 0) return p.text;
  }
  return nullptr;
}

const char* camText(const char* en) {
  const char* found = lookUp(en, 0);
  return found ? found : en;
}

static std::string filled(const char* s, std::initializer_list<std::string> values) {
  std::string out;
  for (; *s; s++) {
    if (*s == '{' && s[1] >= '0' && s[1] <= '9' && s[2] == '}') {
      size_t i = (size_t)(s[1] - '0');
      if (i < values.size()) out += *(values.begin() + i);
      s += 2;
    } else {
      out += *s;
    }
  }
  return out;
}

std::string camText(const char* en, std::initializer_list<std::string> values) { return filled(camText(en), values); }

std::string camCount(long long n, const char* one, const char* many, std::initializer_list<std::string> values) {
  if (textLanguage) {
    const char* form = lookUp(one, (uint8_t)textLanguage->plural(n));
    if (form) return filled(form, values);
  }
  return filled(n == 1 ? one : many, values);
}

std::string camCount(long long n, const char* one, const char* many) { return camCount(n, one, many, {std::to_string(n)}); }
