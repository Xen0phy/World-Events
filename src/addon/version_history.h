//################################################################################
// version_history.h
//--------------------------------------------------------------------------------
// VersionHistoryEntry        one release's Version label plus one Notes field
//                             per WE_LANGUAGE_LIST entry (see below)
// VersionHistoryLanguageSlot WE_LANGUAGE_LIST as a runtime array of
//                             {code, field}, same shape as LanguageSlot
// kVersionHistoryLanguageSlots  WE_LANGUAGE_LIST as data (see below)
// kVersionHistory             manually-curated per-version release notes,
//                              newest first (see: version_history.generated.h)
// kVersionHistoryCount        number of entries in kVersionHistory
//--------------------------------------------------------------------------------
// This file holds the entry schema, not the notes. The notes themselves live in
// resources/localization/version_history.csv, one row per release worth
// mentioning - not every release, most (internal refactors, hotfixes) have
// nothing worth surfacing in-game. tools/generate_version_history.py turns that
// CSV into src/generated/version_history.generated.h (included below) as part of
// the normal build, and fails the build on any row missing a language. Add an
// entry by hand at the TOP of the CSV on any release worth mentioning; skip the
// rest.
//
// Version is English-only, never translated - keep it to the version number plus
// five words or less, it's the whole label shown in the closed dropdown. Notes is
// per-language, one column per WE_LANGUAGE_LIST entry in the CSV; add a language
// the same way as localization_table.h: add a line to WE_LANGUAGE_LIST there,
// then add the matching column to every row of both CSVs.
//
// Notes uses the same "\n"-separated format Split Wars' VersionNotice used: 2
// leading spaces = one indent level, "* " marks a bullet, an unindented non-
// bullet line is a section header ("New Features", "Improvements"). See
// DrawIndentedNotice in changelog_window.cpp for how it's interpreted.
//--------------------------------------------------------------------------------

#pragma once

#include "../core/localization_table.h"   //. WE_LANGUAGE_LIST

//********************************************************************************
// VersionHistoryEntry
//--------------------------------------------------------------------------------
// Version        version + up to five words, shown as-is in the closed
//                dropdown - not translated (see file header)
// (per-language) one Notes field per WE_LANGUAGE_LIST entry, same order and
//                field names as LocalizationEntry
//--------------------------------------------------------------------------------
struct VersionHistoryEntry
{
    const char* Version;
#define WE_LANG(aName, aCode) const char* aName;
    WE_LANGUAGE_LIST
#undef WE_LANG
};

//********************************************************************************
// VersionHistoryLanguageSlot / kVersionHistoryLanguageSlots
//--------------------------------------------------------------------------------
// Same shape and purpose as LanguageSlot/kLanguageSlots (localization_table.h),
// retargeted at VersionHistoryEntry - GetActiveLanguage()'s index is valid into
// either array, since both are generated from the same WE_LANGUAGE_LIST.
//--------------------------------------------------------------------------------
struct VersionHistoryLanguageSlot
{
    const char* Code;
    const char* VersionHistoryEntry::* Field;
};

static constexpr VersionHistoryLanguageSlot kVersionHistoryLanguageSlots[] = {
#define WE_LANG(aName, aCode) { aCode, &VersionHistoryEntry::aName },
    WE_LANGUAGE_LIST
#undef WE_LANG
};

//_ Generated from resources/localization/version_history.csv - see the file header above.
#include "../generated/version_history.generated.h"