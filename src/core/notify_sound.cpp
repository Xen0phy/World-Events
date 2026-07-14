// notify_sound.cpp
// See notify_sound.h for scope notes.

#include "notify_sound.h"
#include "addon.h" // g_AddonDir

#include <windows.h>
#include <mmsystem.h> // PlaySoundW / SND_* — winmm, linked via CMakeLists (target_link_libraries ... winmm)

#include <filesystem>
#include <algorithm>
#include <cctype>

static std::vector<std::string> s_soundFilenames;
static bool                     s_soundFilenamesScanned = false;

// Same UTF-8 -> UTF-16 conversion as icon_whitener.cpp's ToWide — a plain
// std::wstring(s.begin(), s.end()) byte-widen (fine for gw2_api.cpp's ASCII
// bearer token) is NOT safe here, since an addon/GW2 install path can
// contain non-ASCII characters (accented usernames, etc.), and PlaySoundW
// needs a real UTF-16 string.
static std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

// "<addon dir>\sounds" — same backslash convention as maprender.cpp's
// "<addon dir>\textures", since g_AddonDir itself comes back from Nexus
// without a trailing separator either way.
static std::string SoundsDir()
{
    return g_AddonDir + "\\sounds";
}

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
        // .wav only — PlaySoundW's SND_FILENAME path plays uncompressed
        // WAV directly with no extra codec; anything else (mp3, ogg...)
        // would need a decoder this addon doesn't carry, so those are
        // left out of the list entirely rather than shown and failing
        // silently when picked.
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

void PlayNotificationSound(const std::string& filename)
{
    if (filename.empty()) return;

    std::string fullPath = SoundsDir() + "\\" + filename;

    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) return;

    // SND_ASYNC: mandatory — this is called from the render thread (the
    // options panel's "Test" button, and now the per-candidate sound
    // opt-in in subscriptions_notification.cpp), never block a frame on
    // playback.
    // SND_FILENAME: fullPath names a file on disk, not a resource/memory
    // block.
    // SND_NODEFAULT: if playback fails for any reason (bad/corrupt WAV,
    // file removed between the exists() check above and this call,
    // etc.), stay silent instead of falling back to the generic Windows
    // system sound, which would be a confusing thing to hear come out of
    // a game overlay.
    PlaySoundW(
        ToWide(fullPath).c_str(),
        nullptr,
        SND_FILENAME | SND_ASYNC | SND_NODEFAULT
    );
}
