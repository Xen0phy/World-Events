//################################################################################
// localization_table.h
//--------------------------------------------------------------------------------
// WE_LANGUAGE_LIST      every language World Events ships text for: field name +
//                       Nexus code, in the order each row's fields follow it
// LocalizationEntry     one row of the table: an identifier plus one named field
//                       per WE_LANGUAGE_LIST entry (see below)
// kLanguageSlots        WE_LANGUAGE_LIST as a runtime array of {code, field}
// kLanguageCount        number of entries in kLanguageSlots
// kLocalizationTable    every user-facing string, one row per identifier (see:
//                       localization_table_ui.generated.h)
// kLocalizationCount    number of rows in kLocalizationTable
//--------------------------------------------------------------------------------
// This file holds the language schema, not the strings. To add a language: add a
// line to WE_LANGUAGE_LIST below, then add the matching column to every row of
// resources/localization/ui_strings.csv. LocalizationEntry and kLanguageSlots are
// both generated from WE_LANGUAGE_LIST, so the field and its Nexus code can't
// drift out of sync with each other.
//
// The strings themselves live in resources/localization/ui_strings.csv, one row
// per identifier, grouped into "//_" banner rows by UI area.
// tools/generate_localization_table.py turns that CSV into
// src/generated/localization_table_ui.generated.h (included below) as part of the
// normal build, and fails the build on any row missing a language.
//
// Identifier is World Events' own key, not shown to the user - prefixed "WE_" so
// it can't collide with Nexus's own identifiers (short "KB_..." names or
// "((000123))"-style numeric placeholders).
//
// Add a row to the CSV, then call Tr("WE_...") at the call site - never format
// languages into the same literal; Tr() picks per Nexus's active language at
// render time, not at compile time.
//--------------------------------------------------------------------------------

#pragma once

#include <cstddef>

//_ Nexus language codes (see Nexus-Translations on GitHub). First entry is the default and fallback language.
#define WE_LANGUAGE_LIST \
    WE_LANG(English, "en") \
    WE_LANG(German,  "de") \
    WE_LANG(French,  "fr") \
    WE_LANG(Spanish, "es") \
    WE_LANG(Chinese, "cn")

//********************************************************************************
// LocalizationEntry
//--------------------------------------------------------------------------------
// Identifier     World Events' own key, passed to Tr()
// (per-language) one field per WE_LANGUAGE_LIST entry, same order; the first
//                (English) is also the fallback for any other Nexus language
//--------------------------------------------------------------------------------
struct LocalizationEntry
{
    const char* Identifier;
#define WE_LANG(aName, aCode) const char* aName;
    WE_LANGUAGE_LIST
#undef WE_LANG
};

//********************************************************************************
// LanguageSlot / kLanguageSlots
//--------------------------------------------------------------------------------
// WE_LANGUAGE_LIST as data: pairs each Nexus language code with the
// LocalizationEntry field that holds that language's text, so localization.cpp
// can loop over every language without knowing their field names.
//--------------------------------------------------------------------------------
struct LanguageSlot
{
    const char* Code;
    const char* LocalizationEntry::* Field;
};

static constexpr LanguageSlot kLanguageSlots[] = {
#define WE_LANG(aName, aCode) { aCode, &LocalizationEntry::aName },
    WE_LANGUAGE_LIST
#undef WE_LANG
};
static constexpr size_t kLanguageCount = sizeof(kLanguageSlots) / sizeof(kLanguageSlots[0]);

//_ Generated from resources/localization/ui_strings.csv - see the file header above.
#include "../generated/localization_table_ui.generated.h"