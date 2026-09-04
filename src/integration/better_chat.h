//################################################################################
// better_chat.h
//--------------------------------------------------------------------------------
// BetterChatSettings              DataLink contract published by Better Chat
// IsBetterChatSelfCommandEnabled  true if Better Chat's /self is usable now
//--------------------------------------------------------------------------------
// Reads Better Chat's shared settings via Nexus DataLink (BetterChat.Settings) to
// determine whether its /self command is available right now, so the "Better Chat
// (/self)" paste-to-chat option (addon_options_helpers.cpp) only appears when it
// will actually work. This addon never talks to Better Chat beyond that one read-
// only settings blob.
//
// DataLink alone can't tell us Better Chat has unloaded: per Nexus's own docs, an
// addon's shared "static data" is left untouched on unload instead of cleared, so
// a stale SelfMessageCommand = 1 would otherwise outlive Better Chat's actual
// session. IsBetterChatSelfCommandEnabled cross-checks that Better Chat's DLL is
// still loaded in the process (see better_chat.cpp) before trusting the DataLink
// contents.
//--------------------------------------------------------------------------------

#pragma once

#include <cstdint>

//_ Identity/version pair for Better Chat's DataLink block, see BetterChatSettings below.
inline constexpr char     BETTERCHAT_DATALINK_IDENTIFIER[] = "BetterChat.Settings";
inline constexpr uint32_t BETTERCHAT_DATALINK_VERSION      = 1;

//********************************************************************************
// BetterChatSettings
//--------------------------------------------------------------------------------
// Size                sizeof(BetterChatSettings) as published by Better Chat
// Version             layout version this block was published with
// SelfMessageCommand  0 = /self disabled, non-zero = /self enabled
//--------------------------------------------------------------------------------
// Mirrors Better Chat's own published contract exactly - layout and field order
// are not this addon's to change. New settings are only ever appended, so
// existing offsets stay compatible; Version only changes if that ever stops being
// true. Size/Version must both check out before SelfMessageCommand is read, since
// DataLink_Get returns whatever's currently published, not necessarily this exact
// shape (see IsBetterChatSelfCommandEnabled).
//--------------------------------------------------------------------------------
struct BetterChatSettings
{
    uint32_t Size;
    uint32_t Version;

    uint32_t SelfMessageCommand;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBetterChatLoaded   (see: better_chat.cpp)
//--------------------------------------------------------------------------------
bool IsBetterChatLoaded();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBetterChatSelfCommandEnabled
//--------------------------------------------------------------------------------
// True only if Better Chat's DLL is currently loaded in-process, its DataLink
// block is published, its Size and Version both check out, and SelfMessageCommand
// is non-zero. False - never a crash or a garbage read - whenever Better Chat
// isn't loaded, publishes an incompatible block, or simply has /self turned off.
//--------------------------------------------------------------------------------
bool IsBetterChatSelfCommandEnabled();