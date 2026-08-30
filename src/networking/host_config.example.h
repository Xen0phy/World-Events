//################################################################################
// host_config.example.h
//--------------------------------------------------------------------------------
// kHostXor      XOR-obfuscated relay host bytes (placeholder as committed)
// kHostXorLen   byte length of kHostXor
//--------------------------------------------------------------------------------
// Template for host_config.h, which ws_client.cpp actually includes.
// host_config.h itself is gitignored -- never commit it.
//
// Don't hand-edit the byte array below -- regenerate it instead, from the repo
// root:
//
//     python3 tools/generate_host_config.py "your-worker.your-subdomain.workers.dev" > src/networking/host_config.h
//
// XOR-obfuscated with a fixed key (see kHostXorKey in ws_client.cpp), NOT
// encrypted -- keeps the host out of a strings/hex-editor pass over the DLL only,
// not from a debugger, hook, or network proxy (DecodeHost() reconstructs it in
// memory before every connection regardless). Not a credential -- the relay is
// rate-limited, validate+forward only -- so this guards against casual scraping,
// not a secret.
//
// Decodes to the placeholder "gw2-world-events.example.workers.dev", harmless to
// leave committed as-is.
//--------------------------------------------------------------------------------

#pragma once

#include <cstddef>

inline constexpr unsigned char kHostXor[] = {
    0x4a, 0xb1, 0x0e, 0x55, 0x42, 0xf4, 0x49, 0x41, 0xa2, 0x11, 0x1d, 0x43,
    0xfe, 0x55, 0x59, 0xb5, 0x12, 0x1d, 0x4d, 0xfa, 0x56, 0x5d, 0xaa, 0x59,
    0x56, 0x42, 0xf4, 0x49, 0x46, 0xa3, 0x4e, 0x0b, 0x1b, 0xff, 0x5e, 0x5b,
};
inline constexpr size_t kHostXorLen = 36;
