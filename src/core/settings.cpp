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
#include <fstream>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Global storage
// ---------------------------------------------------------------------------
#define SETTING(S, Key, Type, Default) Type Key = Default;
#include "settings_table.h"
#undef SETTING

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

        const char* lastSection = nullptr;
        #define SETTING(S, Key, Type, Default) \
            if (!lastSection || strcmp(lastSection, #S) != 0) { f << "\n[" #S "]\n"; lastSection = #S; } \
            write(#Key, Key);
        #include "settings_table.h"
        #undef SETTING

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

bool LoadSettings(const std::string& addonDir)
{
    try
    {
        std::string filepath = addonDir + "\\settings.ini";
        std::ifstream f(filepath);
        if (!f.is_open()) return false; // no file yet — keep compiled-in defaults

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
            #include "settings_table.h"
            #undef SETTING
        }
        return true;
    }
    catch (...) { return false; }
}
