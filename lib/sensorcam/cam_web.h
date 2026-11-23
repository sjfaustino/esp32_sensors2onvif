// The pages served on the web port: Wi-Fi setup at /setup and the file manager at /.
// Edit them in web/. tools/embed_web.py builds them into cam_web_pages.cpp, gzipped, on every build.
#pragma once
#include <stddef.h>
#include <stdint.h>

extern const uint8_t CAM_WEB_PAGE[];
extern const size_t CAM_WEB_PAGE_SIZE;
extern const uint8_t CAM_SETUP_PAGE[];
extern const size_t CAM_SETUP_PAGE_SIZE;
extern const char CAM_WEB_PAGE_TAG[];  // each page's ETag, from its bytes, quoted
extern const char CAM_SETUP_PAGE_TAG[];
// Each language's words for the pages, served as /lang?file=pt and /lang?file=pt-setup (tools/embed_web.py).
struct CamLangFile {
  const char* name;
  const uint8_t* data;
  size_t size;
  const char* tag;
};
extern const CamLangFile CAM_LANG_FILES[];
extern const size_t CAM_LANG_FILE_COUNT;
