// settings.cpp
// Defines (allocates storage for) every global declared in settings.h, and
// implements LoadSettings/SaveSettings — the INI read/write for all of them.
//
// There is exactly one translation unit that defines these globals — this
// one. All other .cpp files access them via the extern declarations in
// settings.h.
//
// This file is data-driven from settings_table.h via X-macros: adding a new
// setting means adding one SETTING(...) line there, and it automatically
// gets storage, INI saving, and INI loading — with no risk of those three
// pieces drifting out of sync with each other.

#include "settings.h"
#include "apikey_crypto.h"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Global storage
// ---------------------------------------------------------------------------
#define SETTING(S, Key, Type, Default) Type Key = Default;
#define SETTING_ARRAY(S, Key, N, Default) float Key[N] = Default;
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_SECRET

// ---------------------------------------------------------------------------
// SaveSettings
// ---------------------------------------------------------------------------
// Writes all settings to "<addonDir>/settings.ini". Section headers are
// written for human readability only, not load-bearing.
// ---------------------------------------------------------------------------
bool SaveSettings(const std::string& addonDir)
{
    try
    {
        fs::create_directories(addonDir);
        std::string filepath = addonDir + "\\settings.ini";
        std::ofstream f(filepath);
        if (!f.is_open()) return false;

        auto write = [&](const char* k, auto v) { f << k << "=" << v << "\n"; };

        // Gw2ApiKey (SETTING_SECRET in settings_table.h) is written
        // encrypted (see apikey_crypto.h) instead of as its raw value like
        // every other setting here — same ini key name and section, so the
        // file layout looks unchanged to a human; only the VALUE is now a
        // base64 AES-GCM blob rather than the plaintext GW2 API key.
        const char* lastSection = nullptr;
        #define SETTING(S, Key, Type, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            write(#Key, Key);
        #define SETTING_SECRET(S, Key, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            write(#Key, ApiKeyCrypto::Encrypt(addonDir, Key));
        // Comma-joined ("r,g,b,a"), same section-header logic as SETTING
        // above. This is the ONLY place a SETTING_ARRAY value's on-disk
        // shape is written — see ParseColorArray below for the matching
        // read side, including its one-time legacy-format fallback.
        #define SETTING_ARRAY(S, Key, N, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            { \
                f << #Key << "="; \
                for (int _i = 0; _i < N; _i++) f << (_i ? "," : "") << Key[_i]; \
                f << "\n"; \
            }
        #include "settings_table.h"
        #undef SETTING
        #undef SETTING_SECRET
        #undef SETTING_ARRAY

        return true;
    }
    catch (...) { return false; }
}

// ---------------------------------------------------------------------------
// LoadSettings
// ---------------------------------------------------------------------------
// Reads "<addonDir>/settings.ini" and writes directly into the global
// variables. Unknown keys and section header lines (anything starting with
// '[') are silently skipped, so a settings.ini written by an older build
// loads cleanly even if new keys were added since.
//
// Matching is purely by key name across the whole file — section headers
// are not load-bearing, only there for human readability when hand-editing
// or diffing the file. This mirrors how the file is written: keys are
// assumed unique across the whole table, so no real section-scoped parser
// is needed.
// ---------------------------------------------------------------------------
template<typename T> T parse(const std::string& v);
template<> bool         parse<bool>        (const std::string& v) { return v == "1" || v == "true"; }
template<> int          parse<int>         (const std::string& v) { return std::stoi(v); }
template<> float        parse<float>       (const std::string& v) { return std::stof(v); }
template<> unsigned int parse<unsigned int>(const std::string& v) { return (unsigned int)std::stoul(v); }
template<> std::string  parse<std::string> (const std::string& v) { return v; }

// ---------------------------------------------------------------------------
// ParseColorArray
// ---------------------------------------------------------------------------
// Reads a SETTING_ARRAY(..., 4, ...) color value out of its on-disk form.
// Two shapes are accepted:
//
//   - CURRENT: "r,g,b,a" — comma-joined floats in [0,1], written by
//     SaveSettings above. The normal case for every load once a user has
//     saved at least once since this migration.
//
//   - LEGACY: a single decimal integer — the packed RRGGBBAA value these
//     same keys held back when they were `SETTING(..., unsigned int, ...)`.
//     Only ever seen on the FIRST load of a settings.ini written by an
//     older build; unpacked into the same [0,1] float layout here so the
//     rest of the addon never needs to know the packed format existed.
//
// Sets *wasLegacy = true only for the second case, so LoadSettings can
// trigger exactly one SaveSettings() rewrite after a load that actually
// touched the old format — settings.ini otherwise is NOT rewritten on
// every load (unlike events.json — see events_storage.cpp), so without
// this a legacy file would silently keep re-migrating in memory every
// single run without ever actually updating on disk.
// ---------------------------------------------------------------------------
static void ParseColorArray(const std::string& val, float* out, int n, bool* wasLegacy)
{
    if (val.find(',') != std::string::npos)
    {
        std::stringstream ss(val);
        std::string tok;
        int i = 0;
        while (i < n && std::getline(ss, tok, ','))
            out[i++] = std::stof(tok);
        if (i != n) throw std::runtime_error("wrong component count for color array");
        *wasLegacy = false;
        return;
    }

    // Legacy path assumes the RRGGBBAA packing every color setting used
    // before this migration, so it only makes sense for n==4 — every
    // current SETTING_ARRAY color is exactly that, so this isn't a real
    // restriction in practice, just documenting the assumption.
    if (n != 4) throw std::runtime_error("legacy color format requires n==4");
    unsigned int rgba = (unsigned int)std::stoul(val);
    out[0] = ((rgba >> 24) & 0xFF) / 255.0f;
    out[1] = ((rgba >> 16) & 0xFF) / 255.0f;
    out[2] = ((rgba >>  8) & 0xFF) / 255.0f;
    out[3] = ( rgba        & 0xFF) / 255.0f;
    *wasLegacy = true;
}

bool LoadSettings(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\settings.ini";
        std::ifstream f(filepath);
        if (!f.is_open()) return false; // no file yet — keep compiled-in defaults

        // Set true by ParseColorArray whenever a SETTING_ARRAY color is
        // read in its pre-migration packed-RRGGBBAA form — see
        // ParseColorArray's comment above for why this then triggers one
        // SaveSettings() rewrite below, rather than leaving the file
        // holding a mix of old- and new-format keys indefinitely.
        bool migratedLegacyColor = false;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '[') continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            // Each field's parse is individually try/catch'd — std::stoi/
            // stof/stoul (via parse<T>) throw on a malformed value.
            // Catching per-field means one corrupted/hand-edited line only
            // leaves that one setting at whatever it already was
            // (compiled-in default, or whatever a still-valid earlier line
            // already set it to); the rest of the file keeps loading
            // normally instead of the whole load aborting at that line.
            if (false) {}
            #define SETTING(S, Key, Type, Default) \
                else if (key == #Key) \
                { \
                    try { Key = parse<Type>(val); } \
                    catch (...) { /* malformed value for this one key — leave it as-is, keep loading the rest of the file */ } \
                }
            // Gw2ApiKey (SETTING_SECRET in settings_table.h): try decrypting
            // as our own AES-GCM blob first; if that fails (empty, or a
            // plaintext key left over from a pre-encryption settings.ini),
            // fall back to using val as-is. SaveSettings will write it back
            // out encrypted next time, so this is a one-time, transparent
            // migration for anyone upgrading from an older build.
            #define SETTING_SECRET(S, Key, Default) \
                else if (key == #Key) \
                { \
                    try { \
                        std::string dec = ApiKeyCrypto::Decrypt(addonDir, val); \
                        Key = !dec.empty() ? dec : val; \
                    } \
                    catch (...) { } \
                }
            #define SETTING_ARRAY(S, Key, N, Default) \
                else if (key == #Key) \
                { \
                    try { \
                        bool wasLegacy = false; \
                        ParseColorArray(val, Key, N, &wasLegacy); \
                        if (wasLegacy) migratedLegacyColor = true; \
                    } \
                    catch (...) { /* malformed value for this one key — leave it as-is, keep loading the rest of the file */ } \
                }
            #include "settings_table.h"
            #undef SETTING
            #undef SETTING_SECRET
            #undef SETTING_ARRAY
        }

        // One immediate rewrite if this load touched any pre-migration
        // packed-color value, so settings.ini is on the new format from
        // here on — see ParseColorArray's comment for why this can't just
        // rely on the normal AddonUnload-time save instead (a crash or
        // force-kill before then would otherwise leave the legacy value
        // on disk forever, silently re-migrating in memory every load).
        if (migratedLegacyColor)
            SaveSettings(addonDir);

        return true;
    }
    catch (...) { return false; }
}
