//################################################################################
// better_chat_link.h
//--------------------------------------------------------------------------------
// InitBetterChatLink()     call once from AddonLoad, after APIDefs is set
// IsBetterChatAvailable()  true once Better Chat's DataLink resolves
//--------------------------------------------------------------------------------
// Presence detection for the Better Chat addon, using the same
// DataLink_Get mechanism AddonLoad already uses for MumbleLink/NexusLink:
// it returns a pointer to memory another addon shared via DataLink_Share,
// or nullptr if nothing has registered under that identifier yet.
// IsBetterChatAvailable() reduces to that pointer being non-null, letting
// the options panel hide the "Better Chat (/self)" paste target until
// Better Chat actually shares data.
//
// DL_BETTER_CHAT's identifier string and BetterChatLink's void* type are
// placeholders confined to this file; Better Chat has not published
// either one yet. IsBetterChatAvailable() and its callers only ever check
// for a non-null pointer, so updating these two values to the real ones
// needs no change anywhere else.
//--------------------------------------------------------------------------------

#pragma once

//_ Nexus DataLink identifier Better Chat is expected to register.
inline constexpr const char* DL_BETTER_CHAT = "DL_BETTER_CHAT";

//_ Points at Better Chat's shared struct once resolved, else nullptr.
extern void* BetterChatLink;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitBetterChatLink
//--------------------------------------------------------------------------------
// Resolves BetterChatLink via APIDefs->DataLink_Get, the same timing as
// the MumbleLink/NexusLink calls already in AddonLoad.
//--------------------------------------------------------------------------------
void InitBetterChatLink();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBetterChatAvailable
//--------------------------------------------------------------------------------
// True once Better Chat's DataLink has been resolved to a non-null
// pointer - i.e. Better Chat is installed and loaded.
//--------------------------------------------------------------------------------
bool IsBetterChatAvailable();
