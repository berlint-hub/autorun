/* Included first by check_fex_cpu_features.py. FEX-2611's CPUFeatures.cpp
 * includes FEXUnixLib.h, written for the PE side of Wine's unix calls
 * (__wine_unix_call_dispatcher); the check builds it on a Linux host with the
 * unix side's headers (WINE_UNIX_LIB). The PE declarations are taken here,
 * once, before the unix side's. */
#pragma once
#ifdef WINE_UNIX_LIB
#undef WINE_UNIX_LIB
#include <stdarg.h>
#include <windef.h>
#include <winternl.h>
#include "wine/unixlib.h"
#define WINE_UNIX_LIB
#endif
#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#endif
