//################################################################################
// notify_host_config.example.h
//--------------------------------------------------------------------------------
// kNotifyHostXor      XOR-obfuscated notify-worker host bytes (placeholder as
//                     committed)
// kNotifyHostXorLen   byte length of kNotifyHostXor
//--------------------------------------------------------------------------------
// Template for notify_host_config.h, which notification_client.cpp actually
// includes. notify_host_config.h itself is gitignored -- never commit it. See
// host_config.example.h for the sibling per-shard relay's template - this is the
// same story, one level up, for the region-wide notify worker (live-toast-
// handoff.md section 5).
//
// Don't hand-edit the byte array below -- regenerate it instead, from the repo
// root:
//
//     python3 tools/generate_host_config.py "your-worker-notify.your-subdomain.workers.dev" Notify > src/networking/notify_host_config.h
//
// XOR-obfuscated with the same fixed key host_config.example.h uses (see
// kNotifyHostXorKey in notification_client.cpp), NOT encrypted -- same reasoning
// as host_config.example.h: keeps the host out of a strings/hex-editor pass over
// the DLL, not a secret.
//
// Decodes to the placeholder "gw2-world-events-notify.example.workers.dev",
// harmless to leave committed as-is.
//--------------------------------------------------------------------------------

#pragma once

#include <cstddef>

inline constexpr unsigned char kNotifyHostXor[] = {
    0x4a, 0xb1, 0x0e, 0x55, 0x42, 0xf4, 0x49, 0x41, 0xa2, 0x11, 0x1d, 0x43,
    0xfe, 0x55, 0x59, 0xb5, 0x11, 0x16, 0x5a, 0xef, 0x52, 0x4b, 0xbf, 0x12,
    0x1d, 0x4d, 0xfa, 0x56, 0x5d, 0xaa, 0x59, 0x56, 0x42, 0xf4, 0x49, 0x46,
    0xa3, 0x4e, 0x0b, 0x1b, 0xff, 0x5e, 0x5b,
};
inline constexpr size_t kNotifyHostXorLen = 43;
