// For tools/fuzz.ps1 only: what cam_storage.cpp takes from POSIX's unistd.h and sys/stat.h that Microsoft's C library,
// which clang building for Windows uses, lacks. Nothing else includes this.
#pragma once
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
