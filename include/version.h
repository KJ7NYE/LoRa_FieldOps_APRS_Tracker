#pragma once
// FIRMWARE_VERSION_DATE is written by tools/gen_version.py two ways:
//   1. env.Append(CPPDEFINES=[...]) — works on espressif32 (ESP32 targets)
//   2. include/generated/firmware_version.h — fallback for nordicnrf52,
//      which does not propagate CPPDEFINES appended by pre-scripts to the
//      compiler command lines.
// The outer #ifndef keeps whichever source wins without a redefinition warning.
#ifndef FIRMWARE_VERSION_DATE
#  if __has_include("generated/firmware_version.h")
#    include "generated/firmware_version.h"
#  endif
#endif
#ifndef FIRMWARE_VERSION_DATE
#  define FIRMWARE_VERSION_DATE "unknown"
#endif
