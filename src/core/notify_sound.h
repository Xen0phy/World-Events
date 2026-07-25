//################################################################################
// notify_sound.h
//--------------------------------------------------------------------------------
// ScanNotificationSoundFiles / GetNotificationSoundFilenames
//                              sound-picker dropdown contents
// PlayNotificationSound        plays the configured sound file
//--------------------------------------------------------------------------------
// User-supplied notification sound: a single filename (Settings::
// NotificationSoundFile, settings_table.h) picked from "<addon dir>/sounds"
// via a Combo in addon_options.cpp, plus a "Test" button. Just ONE global
// sound file, not a per-event library - what varies per event/slot is only
// whether it plays, gated by that event's notify level (level 3 - see
// IsBasicEventSoundEnabled/IsCyclicSlotSoundEnabled in subscriptions.h).
//--------------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ScanNotificationSoundFiles / GetNotificationSoundFilenames
//--------------------------------------------------------------------------------
// Sorted, ".wav"-only filenames (no path) from "<addon dir>/sounds" -
// see PlayNotificationSound for why only .wav. Scan() re-scans to pick
// up newly-added files (no filesystem-watching), creating the directory
// first if needed. Get() lazily scans on first call.
//--------------------------------------------------------------------------------
void ScanNotificationSoundFiles();
const std::vector<std::string>& GetNotificationSoundFilenames();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PlayNotificationSound
//--------------------------------------------------------------------------------
// Plays "<addon dir>/sounds/<filename>" via winmm's PlaySound
// (SND_FILENAME | SND_ASYNC) - non-blocking, safe from the render thread
// (both current call sites: the options panel's "Test" button, and
// subscriptions_notification.cpp's per-candidate sound opt-in). A no-op
// if filename is empty or the file no longer exists on disk.
//--------------------------------------------------------------------------------
void PlayNotificationSound(const std::string& filename);