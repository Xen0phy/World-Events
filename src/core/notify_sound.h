#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// notify_sound.h
// ---------------------------------------------------------------------------
// User-supplied notification sound: a single filename (Settings::
// NotificationSoundFile, settings_table.h) picked from "<addon dir>/sounds"
// via a Combo in the options panel (addon_options.cpp), plus a "Test"
// button next to it. Deliberately just ONE global sound file, not a
// per-event sound library — what varies per event/slot is only whether
// this one configured sound plays for it at all, gated by that event's
// notify level (level 3 — see subscriptions.h's IsBasicEventSoundEnabled/
// IsCyclicSlotSoundEnabled). PlayNotificationSound is called from two
// places: the options panel's manual "Test" button (unconditional), and
// subscriptions_notification.cpp's UpdateNotifyStates, alongside each
// SpawnPopup call where the firing candidate has sound enabled.
//
// Same disk-scan shape as GetEventIconFilenames/ScanEventIconFiles in
// maprender.h/.cpp (own subfolder under g_AddonDir, own cached filename
// list, explicit rescan rather than filesystem-watching), just for .wav
// files under "sounds" instead of images under "textures".
// ---------------------------------------------------------------------------

// Scan (or re-scan) "<addon dir>/sounds" and rebuild the cached filename
// list returned by GetNotificationSoundFilenames() below. Creates the
// directory if it doesn't exist yet (same as ScanEventIconFiles), so
// there's always somewhere for the user to drop files even on a first
// run. Call this to pick up newly-added files — no automatic
// filesystem-watching, matching the textures folder's existing pattern.
void ScanNotificationSoundFiles();

// Filenames only (no path), sorted, restricted to ".wav" (the only format
// PlaySound's SND_FILENAME path decodes without extra codec support — see
// PlayNotificationSound below). Lazily scans on first call if
// ScanNotificationSoundFiles() hasn't been called yet this session.
const std::vector<std::string>& GetNotificationSoundFilenames();

// Plays "<addon dir>/sounds/<filename>" via winmm's PlaySound
// (SND_FILENAME | SND_ASYNC), i.e. non-blocking — safe to call from the
// render thread, which is where both current call sites live (the
// options panel's "Test" button, and subscriptions_notification.cpp's
// per-candidate sound opt-in). A no-op if filename is empty or the file
// no longer exists on disk (e.g. deleted since the dropdown was last
// scanned) rather than falling back to any default system sound.
void PlayNotificationSound(const std::string& filename);
