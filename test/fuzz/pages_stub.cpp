// The fuzzer serves no pages, and cam_web_pages.cpp is made at build time from web/, so these stand in for it.
#include "cam_web.h"

const uint8_t CAM_WEB_PAGE[] = {0};
const size_t CAM_WEB_PAGE_SIZE = 0;
const uint8_t CAM_SETUP_PAGE[] = {0};
const size_t CAM_SETUP_PAGE_SIZE = 0;
const char CAM_WEB_PAGE_TAG[] = "\"fuzz\"";
const char CAM_SETUP_PAGE_TAG[] = "\"fuzz\"";
const CamLangFile CAM_LANG_FILES[] = {{"", nullptr, 0, ""}};
const size_t CAM_LANG_FILE_COUNT = 0;
// And the hub's messages in English alone, which cam_text_data.cpp would give in each language.
#include "cam_text.h"
const CamLanguageText CAM_LANGUAGES[] = {{"", "", nullptr, 0, nullptr}};
const size_t CAM_LANGUAGE_COUNT = 0;
