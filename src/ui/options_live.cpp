//################################################################################
// options_live.cpp   (see: options_live.h)
//--------------------------------------------------------------------------------

#include "options_live.h"

#include "imgui.h"
#include "localization.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsLive   (see: options_live.h)
//--------------------------------------------------------------------------------
void DrawOptionsLive(const OptionsDeepLink* link)
{
    (void)link; //. contract only
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_SECTION_PENDING"));
}
