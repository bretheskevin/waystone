/*
 * Minimal stub for <switch.h> (libnx).
 * Provides only the types referenced by switch/source/saves.h so that
 * the preview build can compile engine headers without the Switch SDK.
 */
#pragma once
#include <stdint.h>

/* Basic libnx integer aliases */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* libnx Result type — 0 = success */
typedef uint32_t Result;
#define R_SUCCEEDED(r) ((r) == 0)

/* Account UID (128-bit opaque identifier) */
typedef struct { u8 uid[16]; } AccountUid;
