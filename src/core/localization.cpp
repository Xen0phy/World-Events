//################################################################################
// localization.cpp   (see: localization.h)
//--------------------------------------------------------------------------------
// kLanguageProbeIdentifier   internal-only identifier GetActiveLanguage reads
//                            back to detect language
// RegisterTable              registers one LocalizationEntry table with Nexus;
//                            shared by Localization_Load's two table passes
//--------------------------------------------------------------------------------
// Registering each non-default kLanguageSlots entry for this one identifier lets
// GetActiveLanguage answer by comparing Translate()'s result against
// kLanguageSlots, instead of enumerating every language Nexus itself supports.
//--------------------------------------------------------------------------------

#include "localization.h"

#include "addon.h"
#include "localization_table.h"

#include <cstring>

//_ Not shown to the user, never listed in kLocalizationTable.
static constexpr const char* kLanguageProbeIdentifier = "WE_LANGUAGE_PROBE";

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RegisterTable
//--------------------------------------------------------------------------------
// Registers every row of one LocalizationEntry table with Nexus. Shared by
// Localization_Load's kLocalizationTable/kEventNameLocalizationTable passes
// below, since both register the same way and differ only in which array/count
// symbols they read.
//--------------------------------------------------------------------------------
static void RegisterTable(const LocalizationEntry* table, int count)
{
    for (int i = 0; i < count; i++)
    {
        const LocalizationEntry& entry = table[i];
        for (size_t li = 0; li < kLanguageCount; li++)
            APIDefs->Localization_Set(entry.Identifier, kLanguageSlots[li].Code, entry.*kLanguageSlots[li].Field);
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_Load   (see: localization.h)
//--------------------------------------------------------------------------------
void Localization_Load()
{
    if (!APIDefs) return;

    for (size_t li = 1; li < kLanguageCount; li++)
        APIDefs->Localization_Set(kLanguageProbeIdentifier, kLanguageSlots[li].Code, kLanguageSlots[li].Code);

    RegisterTable(kLocalizationTable, kLocalizationCount);
    RegisterTable(kEventNameLocalizationTable, kEventNameLocalizationCount);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetActiveLanguage   (see: localization.h)
//--------------------------------------------------------------------------------
size_t GetActiveLanguage()
{
    if (!APIDefs) return 0;

    const char* probe = APIDefs->Localization_Translate(kLanguageProbeIdentifier);
    if (probe)
    {
        for (size_t li = 1; li < kLanguageCount; li++)
            if (std::strcmp(probe, kLanguageSlots[li].Code) == 0) return li;
    }
    return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Tr   (see: localization.h)
//--------------------------------------------------------------------------------
const char* Tr(const char* aIdentifier)
{
    if (!APIDefs) return aIdentifier;

    return APIDefs->Localization_TranslateTo(aIdentifier, kLanguageSlots[GetActiveLanguage()].Code);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrEnglish   (see: localization.h)
//--------------------------------------------------------------------------------
const char* TrEnglish(const char* aIdentifier)
{
    if (!APIDefs) return aIdentifier;

    return APIDefs->Localization_TranslateTo(aIdentifier, kLanguageSlots[0].Code);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// TrId   (see: localization.h)
//--------------------------------------------------------------------------------
std::string TrId(const char* aIdentifier, const char* aIdSuffix)
{
    return std::string(Tr(aIdentifier)) + aIdSuffix;
}