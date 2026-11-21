// For tools/fuzz.ps1 only: the few calls of POSIX's dirent.h that cam_storage.cpp makes, over the Windows file API,
// since clang building for Windows uses Microsoft's C library, which has no dirent.h. Nothing else includes this.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <string.h>
#include <windows.h>

struct dirent {
  char d_name[MAX_PATH];
};

struct DIR {
  HANDLE find;
  WIN32_FIND_DATAA data;
  bool first;
  struct dirent entry;
};

static inline DIR* opendir(const char* path) {
  char pattern[MAX_PATH];
  if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern)) return nullptr;
  DIR* d = (DIR*)calloc(1, sizeof(DIR));
  if (!d) return nullptr;
  d->find = FindFirstFileA(pattern, &d->data);
  if (d->find == INVALID_HANDLE_VALUE) {
    free(d);
    return nullptr;
  }
  d->first = true;
  return d;
}

static inline struct dirent* readdir(DIR* d) {
  if (!d->first && !FindNextFileA(d->find, &d->data)) return nullptr;
  d->first = false;
  strncpy(d->entry.d_name, d->data.cFileName, sizeof(d->entry.d_name) - 1);
  return &d->entry;
}

static inline int closedir(DIR* d) {
  FindClose(d->find);
  free(d);
  return 0;
}
