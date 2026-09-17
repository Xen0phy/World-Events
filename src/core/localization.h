//################################################################################
// localization.h
//--------------------------------------------------------------------------------
// Localization_Load   call once from AddonLoad, after APIDefs is set
// GetActiveLanguage   index into kLanguageSlots for Nexus's active language
// Tr                  translate aIdentifier into the addon's active language
// TrEnglish           like Tr, but always English regardless of active language
// TrId                Tr(aIdentifier) plus a stable, untranslated ID suffix
//--------------------------------------------------------------------------------
// Nexus's own Localization_Translate() follows whatever language a player picked
// in Nexus's own Options (see localization.cpp), and for any of those it returns
// the bare identifier if nothing is registered for it. World Events only ever
// authors text for the languages in WE_LANGUAGE_LIST, so any other Nexus install
// must still get readable output - Tr() resolves that itself with
// Localization_TranslateTo instead of leaning on Translate's own current-language
// fallback.
//
// Supported languages are declared in localization_table.h. The strings
// themselves live in resources/localization/ui_strings.csv, one identifier+
// languages row per user-facing piece of text - start there to add or change a
// string.
//--------------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_Load
//--------------------------------------------------------------------------------
// Registers every row of kLocalizationTable and kEventNameLocalizationTable
// (localization_table.h) with Nexus via Localization_Set, plus the internal
// language-probe identifier GetActiveLanguage reads back. Call once from
// AddonLoad, after APIDefs is assigned - a no-op before that.
//--------------------------------------------------------------------------------
void Localization_Load();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetActiveLanguage
//--------------------------------------------------------------------------------
// Reads Nexus's current active language back out via a probe identifier that's
// only ever registered for kLanguageSlots[1..] (see localization.cpp) - returns
// the matching index into kLanguageSlots, or 0 (the default/fallback language) if
// Nexus's active language isn't one World Events has text for. No caching: this
// is a cheap lookup and Nexus has no "language changed" event to invalidate a
// cache on, so checking fresh every call is simpler than tracking staleness.
//--------------------------------------------------------------------------------
size_t GetActiveLanguage();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tr
//--------------------------------------------------------------------------------
// Translates aIdentifier (see localization_table.h) into languages per
// GetActiveLanguage, via Localization_TranslateTo - explicit about which language
// it wants instead of trusting Nexus's own active-language fallback, so a Nexus
// install on a language World Events has no text for still reads English, not a
// raw "((identifier))"-style placeholder. Returns aIdentifier itself if APIDefs
// isn't set yet (shouldn't happen post-AddonLoad) or the identifier has no row in
// kLocalizationTable/kEventNameLocalizationTable.
//--------------------------------------------------------------------------------
const char* Tr(const char* aIdentifier);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrEnglish
//--------------------------------------------------------------------------------
// Like Tr, but always resolves against kLanguageSlots[0] (English) regardless of
// GetActiveLanguage - for anywhere the code must match ArenaNet's own English API
// text or an old English-only save file, never the player's current language
// (weekly_vault.cpp's title matching, events_storage.cpp's customName migration,
// the eventNameToId migrations in subscriptions.cpp/events_categories.cpp/
// events_tracking.cpp). Same aIdentifier/no-APIDefs fallback as Tr.
//--------------------------------------------------------------------------------
const char* TrEnglish(const char* aIdentifier);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrId
//--------------------------------------------------------------------------------
// Tr(aIdentifier) + a literal, never-translated aIdSuffix (e.g. "###some_widget")
// - for ImGui widgets that need a translated visible label but a stable ID. Dear
// ImGui only drops preceding text from the ID hash at a triple "###"; a plain
// "##" still folds the translated label into the hash, so aIdSuffix must start
// with "###", not "##" - see kEditSubscriptionsWindowId (subscriptions_edit_
// window.h) for the pattern. Returns a std::string since the result is built at
// call time, not a static string - pass .c_str() to ImGui.
//--------------------------------------------------------------------------------
std::string TrId(const char* aIdentifier, const char* aIdSuffix);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_SyncCloseOnEscape
//--------------------------------------------------------------------------------
// Nexus's GUI_RegisterCloseOnEscape does a raw strcmp of the whole window Name
// string against what was registered, not an ID-hash match - so a window with a
// TrId()/Tr()-translated title needs both ImGuiWindow::Name and Nexus's
// registration forced into agreement every frame. Call immediately after
// ImGui::Begin() (regardless of its return value - the window still exists when
// collapsed), passing the exact string just passed to Begin(). Only touches
// ImGui/Nexus state when aFullLabel differs from what's already in sync (first
// render, or after a runtime language switch), so it's cheap to call
// unconditionally every frame.
//--------------------------------------------------------------------------------
void Localization_SyncCloseOnEscape(bool* aIsVisible, const std::string& aFullLabel);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_DeregisterCloseOnEscape
//--------------------------------------------------------------------------------
// Deregisters whatever string is currently registered (via
// Localization_SyncCloseOnEscape) for aIsVisible, if any. Use this for a window
// that needs escape-to-close turned off conditionally at runtime (e.g. while
// pinned/locked) - the next Localization_SyncCloseOnEscape call re-registers it.
//--------------------------------------------------------------------------------
void Localization_DeregisterCloseOnEscape(bool* aIsVisible);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_DeregisterAllCloseOnEscape
//--------------------------------------------------------------------------------
// Deregisters every window last registered via Localization_SyncCloseOnEscape.
// Call from AddonUnload so no stale registration survives a hot-reload.
//--------------------------------------------------------------------------------
void Localization_DeregisterAllCloseOnEscape();