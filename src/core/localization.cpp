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

#include "imgui.h"          // IWYU pragma: keep //.
#include "imgui_internal.h" //. ImGuiWindow::Name/NameBufLen, ImStrdupcpy - see Localization_SyncCloseOnEscape

#include <cstring>
#include <unordered_map>

//_ Not shown to the user, never listed in kLocalizationTable.
static constexpr const char* kLanguageProbeIdentifier = "WE_LANGUAGE_PROBE";

//_ Full window-Name string last registered with Nexus, keyed by aIsVisible - see Localization_SyncCloseOnEscape (localization.h)
static std::unordered_map<bool*, std::string> s_closeOnEscapeLabels;

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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_SyncCloseOnEscape
//--------------------------------------------------------------------------------
// ImGuiWindow::Name only refreshes on its own while the Ctrl+Tab switcher list is
// open (imgui.cpp Begin()); otherwise it stays frozen to whichever string the
// window's first-ever Begin() call used, even though the title bar always shows
// the fresh argument. Force-updates it the same way that Ctrl+Tab path does, then
// no-ops if aFullLabel already matches what's registered for aIsVisible -
// otherwise deregisters the stale string before registering the new one, so
// Nexus's registry never holds two entries for the same bool*.
//--------------------------------------------------------------------------------
void Localization_SyncCloseOnEscape(bool* aIsVisible, const std::string& aFullLabel)
{
    if (!APIDefs) return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window && strcmp(window->Name, aFullLabel.c_str()) != 0)
    {
        size_t bufLen = (size_t)window->NameBufLen;
        window->Name = ImStrdupcpy(window->Name, &bufLen, aFullLabel.c_str());
        window->NameBufLen = (int)bufLen;
    }

    auto it = s_closeOnEscapeLabels.find(aIsVisible);
    if (it != s_closeOnEscapeLabels.end())
    {
        if (it->second == aFullLabel) return; //. already in sync
        APIDefs->GUI_DeregisterCloseOnEscape(it->second.c_str());
    }

    APIDefs->GUI_RegisterCloseOnEscape(aFullLabel.c_str(), aIsVisible);
    s_closeOnEscapeLabels[aIsVisible] = aFullLabel;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_DeregisterCloseOnEscape   (see: localization.h)
//--------------------------------------------------------------------------------
void Localization_DeregisterCloseOnEscape(bool* aIsVisible)
{
    if (!APIDefs) return;

    auto it = s_closeOnEscapeLabels.find(aIsVisible);
    if (it == s_closeOnEscapeLabels.end()) return;

    APIDefs->GUI_DeregisterCloseOnEscape(it->second.c_str());
    s_closeOnEscapeLabels.erase(it);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Localization_DeregisterAllCloseOnEscape   (see: localization.h)
//--------------------------------------------------------------------------------
void Localization_DeregisterAllCloseOnEscape()
{
    if (!APIDefs) return;

    for (auto& [visPtr, label] : s_closeOnEscapeLabels)
        APIDefs->GUI_DeregisterCloseOnEscape(label.c_str());
    s_closeOnEscapeLabels.clear();
}