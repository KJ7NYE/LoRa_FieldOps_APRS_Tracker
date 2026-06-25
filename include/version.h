#pragma once
// Version string is generated at build time by tools/gen_version.py from `git describe`.
// include/generated/firmware_version.h is written pre-build and gitignored.
#if __has_include("generated/firmware_version.h")
#  include "generated/firmware_version.h"
#endif
#ifndef FIRMWARE_VERSION_DATE
#  define FIRMWARE_VERSION_DATE "unknown"
#endif
