//################################################################################
// options_events.cpp   (see: options_events.h)
//--------------------------------------------------------------------------------

#include "options_events.h"

#include "imgui.h"
#include "localization.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawOptionsEvents   (see: options_events.h)
//--------------------------------------------------------------------------------
void DrawOptionsEvents(const OptionsDeepLink* link)
{
    (void)link; //. contract only
    ImGui::TextDisabled("%s", Tr("WE_OPTWIN_SECTION_PENDING"));
}
