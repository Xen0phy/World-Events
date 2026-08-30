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
// leading spaces = one indent level, "* " marks a bullet, an unindented
// non-bullet line is a section header ("New Features", "Improvements"). See
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
        "0.0.0.0 - Text",
        "New Features\n"
        "  * Text"
    },
};

static constexpr int kVersionHistoryCount = sizeof(kVersionHistory) / sizeof(kVersionHistory[0]);