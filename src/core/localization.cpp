//################################################################################
// localization.cpp   (see: localization.h)
//--------------------------------------------------------------------------------
// kLanguageProbeIdentifier   internal-only identifier GetActiveLanguage reads
//                            back to detect language
//--------------------------------------------------------------------------------
// Registering "de" for just this one identifier lets GetActiveLanguage answer
// with a single string-compare instead of enumerating every language Nexus itself
// supports.
//--------------------------------------------------------------------------------

#include "localization.h"

#include "addon.h"
#include "localization_table.h"

#include <cstring>

//_ Not shown to the user, never listed in kLocalizationTable.
static constexpr const char* kLanguageProbeIdentifier = "WE_LANGUAGE_PROBE";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_Load   (see: localization.h)
//--------------------------------------------------------------------------------
void Localization_Load()
{
    if (!APIDefs) return;

    APIDefs->Localization_Set(kLanguageProbeIdentifier, "de", "de");

    for (int i = 0; i < kLocalizationCount; i++)
    {
        const LocalizationEntry& entry = kLocalizationTable[i];
        APIDefs->Localization_Set(entry.Identifier, "en", entry.English);
        APIDefs->Localization_Set(entry.Identifier, "de", entry.German);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetActiveLanguage   (see: localization.h)
//--------------------------------------------------------------------------------
ELanguage GetActiveLanguage()
{
    if (!APIDefs) return ELanguage::English;

    const char* probe = APIDefs->Localization_Translate(kLanguageProbeIdentifier);
    if (probe && std::strcmp(probe, "de") == 0) return ELanguage::German;
    return ELanguage::English;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tr   (see: localization.h)
//--------------------------------------------------------------------------------
const char* Tr(const char* aIdentifier)
{
    if (!APIDefs) return aIdentifier;

    const char* lang = (GetActiveLanguage() == ELanguage::German) ? "de" : "en";
    return APIDefs->Localization_TranslateTo(aIdentifier, lang);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrId   (see: localization.h)
//--------------------------------------------------------------------------------
std::string TrId(const char* aIdentifier, const char* aIdSuffix)
{
    return std::string(Tr(aIdentifier)) + aIdSuffix;
}