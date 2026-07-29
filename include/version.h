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

// BOARD_ENV_ID mirrors the PlatformIO environment name for this build (see
// platformio.ini / variants/*/platformio.ini). Exposed over serial via the
// "version" CLI command so serial_config.html's firmware flasher can
// auto-select the matching board instead of requiring a manual pick.
#if defined(HELTEC_T114)
#  define BOARD_ENV_ID "heltec_t114"
#elif defined(HELTEC_V3_433_APRS)
#  define BOARD_ENV_ID "heltec_v3_433_aprs"
#elif defined(TTGO_T_Beam_V1_2_433_APRS)
#  define BOARD_ENV_ID "tbeam_433_aprs"
#elif defined(TTGO_T_BEAM_1W)
#  define BOARD_ENV_ID "tbeam_433_1w_aprs"
#elif defined(LILYGO_T3_433_APRS)
#  define BOARD_ENV_ID "lilygo_t3_433_aprs"
#elif defined(LORANGER_V1)
#  define BOARD_ENV_ID "LoRanger_V1"
#else
#  define BOARD_ENV_ID "unknown"
#endif
