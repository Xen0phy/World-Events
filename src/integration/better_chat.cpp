//################################################################################
// better_chat.cpp   (see: better_chat.h)
//--------------------------------------------------------------------------------

#include "better_chat.h"

#include "addon.h" //. APIDefs, for DataLink_Get

#define WIN32_LEAN_AND_MEAN
#include <windows.h> //. GetModuleHandleA - see IsBetterChatLoaded

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBetterChatLoaded
//--------------------------------------------------------------------------------
// GetModuleHandleA matches case-insensitively on just the module's own file
// name, regardless of the full path it was loaded from, so this needs no
// enumeration - one call per candidate name is enough. A renamed .dll this list
// doesn't happen to cover would still read as "not loaded"; there is no Nexus
// API to ask "is addon X loaded" directly (see better_chat.h).
//--------------------------------------------------------------------------------
bool IsBetterChatLoaded()
{
    return GetModuleHandleA("better_chat.dll") || GetModuleHandleA("betterchat.dll");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBetterChatSelfCommandEnabled
//--------------------------------------------------------------------------------
// See better_chat.h for the full contract. Checks IsBetterChatLoaded before the
// DataLink contents: Nexus's "hot unload" actually FreeLibrary()s an addon's DLL
// on unload, so this reflects Better Chat's real session state, unlike the
// DataLink block itself, which Nexus intentionally leaves as stale "static data"
// after the owning addon is gone.
//--------------------------------------------------------------------------------
bool IsBetterChatSelfCommandEnabled()
{
    if (!IsBetterChatLoaded())
        return false;

    const auto* settings = static_cast<const BetterChatSettings*>(
        APIDefs->DataLink_Get(BETTERCHAT_DATALINK_IDENTIFIER));

    if (!settings ||
        settings->Size < sizeof(BetterChatSettings) ||
        settings->Version != BETTERCHAT_DATALINK_VERSION)
        return false;

    return settings->SelfMessageCommand != 0;
}