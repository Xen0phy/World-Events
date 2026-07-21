#pragma once
#include <string>

// ---------------------------------------------------------------------------
// settings.h
// ---------------------------------------------------------------------------
// Declares one extern global per entry in settings_table.h, plus the
// load/save functions that persist them to settings.ini in the addon
// directory.
//
// Definitions (actual storage) live in settings.cpp — exactly one
// translation unit defines these globals.
// ---------------------------------------------------------------------------

// SETTING_SECRET is deliberately NOT defined here: settings_table.h has its
// own fallback (`#ifndef SETTING_SECRET #define SETTING_SECRET(S,Key,Default)
// SETTING(S,Key,std::string,Default) #endif`) that delegates to whatever
// SETTING currently expands to. An extern declaration for a secret is
// identical to an extern declaration for any other std::string setting, so
// there's nothing secret-specific to write here — the fallback already
// produces exactly `extern std::string Gw2ApiKey;` via SETTING. The two
// places that DO need `#define SETTING_SECRET` explicitly (SaveSettings/
// LoadSettings, further down in settings.cpp) are exactly the two places
// a secret's behavior actually differs from a plain SETTING — writing it
// encrypted, and decrypting/falling back to plaintext on read.
#define SETTING(S, Key, Type, Default) extern Type Key;
// float[N] rather than std::array<float,N>: needs to decay to a plain
// float* for ImGui::ColorEdit3/4 (which take `float col[3|4]`) without an
// extra `.data()` at every call site — see addon_options.cpp, which passes
// these arrays straight in.
#define SETTING_ARRAY(S, Key, N, Default) extern float Key[N];
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_SECRET

// ---------------------------------------------------------------------------
// LoadSettings / SaveSettings
// ---------------------------------------------------------------------------
// addonDir is the directory returned by APIDefs->Paths_GetAddonDirectory(...),
// e.g. "<GW2>/addons/WorldEvents". The settings file itself is
// "<addonDir>/settings.ini".
//
// Both swallow exceptions and return false on failure so callers don't need
// try/catch blocks. LoadSettings silently ignores unknown keys/sections, so
// a settings.ini written by an older build loads cleanly even if new keys
// were added since — and a missing file simply means every global keeps the
// compiled-in default from settings_table.h.
// ---------------------------------------------------------------------------
bool LoadSettings(const std::string& addonDir);
bool SaveSettings(const std::string& addonDir);
