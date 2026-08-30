//################################################################################
// version_history.h
//--------------------------------------------------------------------------------
// kVersionHistory   manually-curated per-version release notes, newest first
//--------------------------------------------------------------------------------
// Backs the "What's New" dropdown (changelog_window.h/.cpp) - deliberately NOT
// auto-generated from every release, since most (internal refactors, hotfixes)
// have nothing worth surfacing in-game. Add an entry by hand at the TOP of the
// array on any release worth mentioning; skip the rest.
//
// Notes uses the same "\n"-separated format Split Wars' VersionNotice used: 2
// leading spaces = one indent level, "* " marks a bullet, an unindented non-
// bullet line is a section header ("New Features", "Improvements"). See
// DrawIndentedNotice in changelog_window.cpp for how it's interpreted.
//
// Keep Version to the version number plus five words or less - it's the whole
// label shown in the closed dropdown.
//--------------------------------------------------------------------------------

#pragma once

//********************************************************************************
// VersionHistoryEntry
//--------------------------------------------------------------------------------
// Version   version + up to five words, shown as-is in the closed dropdown
// Notes     header/bullet-formatted text - see file header for the convention
//--------------------------------------------------------------------------------
struct VersionHistoryEntry
{
    const char* Version;
    const char* Notes;
};

//_ Newest first - RenderVersionHistoryWindow's dropdown defaults to index 0.
static constexpr VersionHistoryEntry kVersionHistory[] = {
    {
        "1.6.0.0 - Live Events",
        "New Features\n"
        "  * Added live event reporting. Opt-in, only Treasure Mushrooms for now\n"
        "    * Draggable report button, recent-reports window, optional map markers\n"
        "  * Added a \"What's New\" popup on update, the one you're reading right now\n"
        "    * Full changelog on GitHub\n\n"
        "Improvements\n"
        "  * Better Chat integration now detects /self automatically"
    },
    {
        "1.5.0.0 - Qucik access",
        "New Features\n"
        "  * Added new quick access subscription window\n"
        "    * Accesible via right-clicking the subscription window, bar or toast"
    },
    {
        "1.4.1.1 - Paste to self",
        "New Features\n"
        "  * Added texture customization for cyclic event groups\n"
        "  * Added whisper and self \"Paste to\" options"
    },
    {
        "1.3.2.1 - Competitive disable",
        "New Features\n"
        "  * Added options to hide UI elements in WvW and PvP"
    },
    {
        "1.3.0.0 - Event defaults",
        "New Features\n"
        "  * Reset events to defaults\n"
        "  * Varying times for slots in cyclic groups\n\n"
        "Improvements\n"
        "  * Added festival events with available timers"
    },
    {
        "1.0.0.0 - Initial release",
        "Features\n"
        "  * Map markers for basic events and cyclic groups\n"
        "  * Subscriptions bar, window and notification toasts\n"
        "  * API-tracking for available events\n"
        "  * Lots of customisation"
    },
};

static constexpr int kVersionHistoryCount = sizeof(kVersionHistory) / sizeof(kVersionHistory[0]);