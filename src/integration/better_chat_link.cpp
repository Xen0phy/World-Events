//################################################################################
// better_chat_link.cpp   (see: better_chat_link.h)
//--------------------------------------------------------------------------------

#include "addon.h"
#include "better_chat_link.h"
#include "settings.h" //. BetterChatManualOverride

void* BetterChatLink = nullptr;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// InitBetterChatLink
//--------------------------------------------------------------------------------
// Resolves to nullptr until Better Chat registers DL_BETTER_CHAT via
// DataLink_Share.
//--------------------------------------------------------------------------------
void InitBetterChatLink()
{
    BetterChatLink = APIDefs->DataLink_Get(DL_BETTER_CHAT);
}

bool IsBetterChatAvailable()
{
    return BetterChatLink != nullptr || BetterChatManualOverride;
}