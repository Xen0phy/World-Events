//################################################################################
// notify_sound.cpp
//--------------------------------------------------------------------------------
// See notify_sound.h for scope notes.
//--------------------------------------------------------------------------------

#include "addon.h" //. g_AddonDir
#include "notify_sound.h"

#define WIN32_LEAN_AND_MEAN
#include <mmsystem.h> //. PlaySoundW/SND_*, linked via CMakeLists
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

static std::vector<std::string> s_soundFilenames;
static bool                     s_soundFilenamesScanned = false;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ToWide
//--------------------------------------------------------------------------------
// Same UTF-8 -> UTF-16 conversion as icon_whitener.cpp's ToWide - a plain byte-
// widen isn't safe here, since an addon/GW2 install path can contain non-ASCII
// characters and PlaySoundW needs real UTF-16.
//--------------------------------------------------------------------------------
static std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SoundsDir
//--------------------------------------------------------------------------------
// "<addon dir>\sounds"
//--------------------------------------------------------------------------------
static std::string SoundsDir()
{
    return g_AddonDir + "\\sounds";
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScanNotificationSoundFiles / GetNotificationSoundFilenames
//--------------------------------------------------------------------------------
// See notify_sound.h for the full contract.
//--------------------------------------------------------------------------------
void ScanNotificationSoundFiles()
{
    s_soundFilenames.clear();
    s_soundFilenamesScanned = true;

    std::string soundsDir = SoundsDir();

    std::error_code ec;
    std::filesystem::create_directories(soundsDir, ec);

    for (auto& entry : std::filesystem::directory_iterator(soundsDir, ec))
    {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        //_ .wav only - PlaySoundW's SND_FILENAME path decodes uncompressed
        // WAV with no extra codec; other formats are left out entirely.
        if (ext == ".wav")
            s_soundFilenames.push_back(entry.path().filename().string());
    }

    std::sort(s_soundFilenames.begin(), s_soundFilenames.end());
}

const std::vector<std::string>& GetNotificationSoundFilenames()
{
    if (!s_soundFilenamesScanned)
        ScanNotificationSoundFiles();
    return s_soundFilenames;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PlayNotificationSound
//--------------------------------------------------------------------------------
// See notify_sound.h for the full contract. SND_ASYNC keeps this non-blocking on
// the render thread; SND_NODEFAULT stays silent on failure instead of falling
// back to the generic Windows system sound.
//--------------------------------------------------------------------------------
void PlayNotificationSound(const std::string& filename)
{
    if (filename.empty()) return;

    std::string fullPath = SoundsDir() + "\\" + filename;

    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) return;

    PlaySoundW(
        ToWide(fullPath).c_str(),
        nullptr,
        SND_FILENAME | SND_ASYNC | SND_NODEFAULT
    );
}