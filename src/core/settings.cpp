//################################################################################
// settings.cpp
//--------------------------------------------------------------------------------
// Defines (allocates storage for) every global declared in settings.h, and
// implements LoadSettings/SaveSettings - the INI read/write for all of them.
//
// There is exactly one translation unit that defines these globals - this one.
// All other .cpp files access them via the extern declarations in settings.h.
//
// This file is data-driven from settings_table.h via X-macros: adding a new
// setting means adding one SETTING(...) line there, and it automatically gets
// storage, INI saving, and INI loading - with no risk of those three pieces
// drifting out of sync with each other.
//--------------------------------------------------------------------------------

#include "apikey_crypto.h"
#include "settings.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

//_ Storage for every settings.h extern - one instance of each,
// initialized to its settings_table.h compiled-in default.
#define SETTING(S, Key, Type, Default) Type Key = Default;
#define SETTING_ARRAY(S, Key, N, Default) float Key[N] = Default;
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_SECRET

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveSettings
//--------------------------------------------------------------------------------
// Writes all settings to "<addonDir>/settings.ini". Section headers are written
// for human readability only, not load-bearing.
//--------------------------------------------------------------------------------
bool SaveSettings(const std::string& addonDir)
{
    try
    {
        fs::create_directories(addonDir);
        std::string filepath = addonDir + "\\settings.ini";
        std::ofstream f(filepath);
        if (!f.is_open()) return false;

        auto write = [&](const char* k, auto v) { f << k << "=" << v << "\n"; };

        const char* lastSection = nullptr;
        #define SETTING(S, Key, Type, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            write(#Key, Key);
        //_ Gw2ApiKey (SETTING_SECRET) writes encrypted (see
        // apikey_crypto.h) - same key/section as a plain SETTING, only
        // the VALUE differs.
        #define SETTING_SECRET(S, Key, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            write(#Key, ApiKeyCrypto::Encrypt(addonDir, Key));
        //_ Comma-joined "r,g,b,a" - the only place a SETTING_ARRAY's
        // on-disk shape is written; see ParseColorArray below for the
        // read side.
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// parse<T>
//--------------------------------------------------------------------------------
// One specialization per settings_table.h Type. Throws on a malformed value (see
// LoadSettings below, which catches per-field).
//--------------------------------------------------------------------------------
template<typename T> T parse(const std::string& v);
template<> bool         parse<bool>        (const std::string& v) { return v == "1" || v == "true"; }
template<> int          parse<int>         (const std::string& v) { return std::stoi(v); }
template<> float        parse<float>       (const std::string& v) { return std::stof(v); }
template<> unsigned int parse<unsigned int>(const std::string& v) { return (unsigned int)std::stoul(v); }
template<> std::string  parse<std::string> (const std::string& v) { return v; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ParseColorArray
//--------------------------------------------------------------------------------
// Reads a SETTING_ARRAY(..., 4, ...) color value out of its on-disk form. Two
// shapes accepted: CURRENT "r,g,b,a" (comma-joined floats in [0,1], written by
// SaveSettings above - the normal case), or LEGACY a single packed-RRGGBBAA
// integer, only ever seen on the first load of a settings.ini written by an older
// build.
//
// Sets *wasLegacy = true only for the legacy case, so LoadSettings can trigger
// exactly one SaveSettings() rewrite after a load that actually touched the old
// format - without this, a legacy file would silently keep re-migrating in memory
// every run without ever updating on disk.
//--------------------------------------------------------------------------------
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

    //_ Assumes the RRGGBBAA packing every color setting used
    // pre-migration; only makes sense for n==4, which every
    // current color setting is.
    if (n != 4) throw std::runtime_error("legacy color format requires n==4");
    unsigned int rgba = (unsigned int)std::stoul(val);
    out[0] = ((rgba >> 24) & 0xFF) / 255.0f;
    out[1] = ((rgba >> 16) & 0xFF) / 255.0f;
    out[2] = ((rgba >>  8) & 0xFF) / 255.0f;
    out[3] = ( rgba        & 0xFF) / 255.0f;
    *wasLegacy = true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LoadSettings
//--------------------------------------------------------------------------------
// Reads "<addonDir>/settings.ini" and writes directly into the global variables.
// Unknown keys and section header lines (anything starting with '[') are silently
// skipped, so a settings.ini written by an older build loads cleanly even if new
// keys were added since.
//
// Matching is purely by key name across the whole file - section headers are not
// load-bearing, only there for human readability when hand-editing or diffing the
// file. This mirrors how the file is written: keys are assumed unique across the
// whole table, so no real section-scoped parser is needed.
//--------------------------------------------------------------------------------
bool LoadSettings(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\settings.ini";
        std::ifstream f(filepath);
        if (!f.is_open()) return false; //. no file yet, keep defaults

        //_ Set true by ParseColorArray on a pre-migration packed-color
        // read - triggers one SaveSettings() rewrite below (see above).
        bool migratedLegacyColor = false;

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '[') continue;

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            //_ Each field's parse is individually try/catch'd - a
            // corrupted/hand-edited line only leaves that one setting
            // unchanged; the rest of the file keeps loading normally.
            if (false) {}
            #define SETTING(S, Key, Type, Default) \
                else if (key == #Key) \
                { \
                    try { Key = parse<Type>(val); } \
                    catch (...) { } \
                }
            //_ Try decrypting as our own AES-GCM blob first; on failure
            // (empty, or a pre-encryption plaintext key) fall back to
            // val as-is - SaveSettings re-encrypts it next time.
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
                    catch (...) { } \
                }
            #include "settings_table.h"
            #undef SETTING
            #undef SETTING_SECRET
            #undef SETTING_ARRAY
        }

        //_ One rewrite if this load touched a pre-migration packed-color
        // value, so settings.ini is on the new format from here on -
        // can't rely on the normal AddonUnload save for this.
        if (migratedLegacyColor)
            SaveSettings(addonDir);

        return true;
    }
    catch (...) { return false; }
}