// The pages served on the web port: Wi-Fi setup at /setup and the file manager at /.
// Edit them in web/. tools/embed_web.py builds them into cam_web_pages.cpp, gzipped, on every build.
#pragma once
#include <stddef.h>
#include <stdint.h>

extern const uint8_t CAM_WEB_PAGE[];
extern const size_t CAM_WEB_PAGE_SIZE;
extern const uint8_t CAM_SETUP_PAGE[];
extern const size_t CAM_SETUP_PAGE_SIZE;
