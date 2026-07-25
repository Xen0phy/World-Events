//################################################################################
// settings.h
//--------------------------------------------------------------------------------
// Declares one extern global per entry in settings_table.h, plus the
// load/save functions that persist them to settings.ini in the addon
// directory.
//
// Definitions (actual storage) live in settings.cpp - exactly one
// translation unit defines these globals.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SETTING / SETTING_ARRAY
//--------------------------------------------------------------------------------
// X-macros expanding settings_table.h into one extern declaration per
// setting (see settings.cpp for the storage/load/save side). SETTING_ARRAY
// uses a plain float[N], not std::array, so it decays to float* for
// ImGui::ColorEdit3/4 without a .data() at every call site.
//
// SETTING_SECRET is deliberately not defined here: settings_table.h's own
// fallback delegates it to SETTING, so `extern std::string Gw2ApiKey;` is
// produced automatically. Only SaveSettings/LoadSettings (settings.cpp)
// need `#define SETTING_SECRET` explicitly - encrypting/decrypting is the
// only place a secret's behavior actually differs from a plain SETTING.
//--------------------------------------------------------------------------------
#define SETTING(S, Key, Type, Default) extern Type Key;
#define SETTING_ARRAY(S, Key, N, Default) extern float Key[N];
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_SECRET

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LoadSettings / SaveSettings
//--------------------------------------------------------------------------------
// addonDir is the directory returned by APIDefs->Paths_GetAddonDirectory(...),
// e.g. "<GW2>/addons/WorldEvents". The settings file itself is
// "<addonDir>/settings.ini".
//
// Both swallow exceptions and return false on failure so callers don't need
// try/catch blocks. LoadSettings silently ignores unknown keys/sections, so
// a settings.ini written by an older build loads cleanly even if new keys
// were added since - and a missing file simply means every global keeps
// the compiled-in default from settings_table.h.
//--------------------------------------------------------------------------------
bool LoadSettings(const std::string& addonDir);
bool SaveSettings(const std::string& addonDir);