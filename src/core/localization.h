//################################################################################
// localization.h
//--------------------------------------------------------------------------------
// ELanguage           the two languages World Events ships text for
// Localization_Load   call once from AddonLoad, after APIDefs is set
// GetActiveLanguage   Nexus's active language, collapsed to English/German
// Tr                  translate aIdentifier into the addon's active language
// TrId                Tr(aIdentifier) plus a stable, untranslated ID suffix
//--------------------------------------------------------------------------------
// Nexus's own Localization_Translate() follows whatever language a player picked
// in Nexus's own Options (see localization.cpp), and for any of those it returns
// the bare identifier if nothing is registered for it. World Events only ever
// authors some languages's text, so as fallback Nexus install must still get
// readable output - Tr() resolves that itself with Localization_TranslateTo
// instead of leaning on Translate's own current-language fallback.
//
// All strings live in localization_table.h, one identifier+languages row per
// user-facing piece of text - start there to add or change a string.
//--------------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ELanguage
//--------------------------------------------------------------------------------
// English is the default, and the fallback for any Nexus language World Events
// has no text for (French, Spanish, Chinese, ...).
//--------------------------------------------------------------------------------
enum class ELanguage : uint8_t
{
    English,
    German
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_Load
//--------------------------------------------------------------------------------
// Registers every row of kLocalizationTable (localization_table.h) with Nexus via
// Localization_Set, plus the internal language-probe identifier GetActiveLanguage
// reads back. Call once from AddonLoad, after APIDefs is assigned - a no-op
// before that.
//--------------------------------------------------------------------------------
void Localization_Load();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetActiveLanguage
//--------------------------------------------------------------------------------
// Reads Nexus's current active language back out via a probe identifier that's
// only ever registered for "de" (see localization.cpp) - if Nexus's active
// language is German, Translate() returns "de"; for every other Nexus language
// (English included) there's nothing registered right now, so Translate() returns
// the identifier itself, which this reports as English. No caching: this is a
// cheap lookup and Nexus has no "language changed" event to invalidate a cache
// on, so checking fresh every call is simpler than tracking staleness.
//--------------------------------------------------------------------------------
ELanguage GetActiveLanguage();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tr
//--------------------------------------------------------------------------------
// Translates aIdentifier (see localization_table.h) into languages per
// GetActiveLanguage, via Localization_TranslateTo - explicit about which of the
// two languages it wants instead of trusting Nexus's own active-language
// fallback, so a third-language Nexus install still reads English, not a raw
// "((identifier))"-style placeholder. Returns aIdentifier itself if APIDefs isn't
// set yet (shouldn't happen post-AddonLoad) or the identifier has no row in
// kLocalizationTable.
//--------------------------------------------------------------------------------
const char* Tr(const char* aIdentifier);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrId
//--------------------------------------------------------------------------------
// Tr(aIdentifier) + a literal, never-translated aIdSuffix (e.g. "##some_widget")
// - for ImGui widgets that need a translated visible label but a stable ID, the
// same concern as kVersionHistoryWindowId (changelog_window.h): ImGui hashes a
// widget/window's ID from only the text after "##", so appending the same
// aIdSuffix regardless of active language keeps that ID constant while the
// visible label translates. Returns a std::string since the result is built at
// call time, not a static string - pass .c_str() to ImGui.
//--------------------------------------------------------------------------------
std::string TrId(const char* aIdentifier, const char* aIdSuffix);