#pragma once
#include <string>

// ---------------------------------------------------------------------------
// settings.h
// ---------------------------------------------------------------------------
// Declares one extern global per entry in settings_table.h, plus the
// load/save functions that persist them to settings.ini in the addon
// directory.
//
// Definitions (actual storage) live in settings.cpp — see that file's
// header comment for why there's exactly one translation unit that does this.
// ---------------------------------------------------------------------------

#define SETTING(S, Key, Type, Default) extern Type Key;
#include "settings_table.h"
#undef SETTING

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
