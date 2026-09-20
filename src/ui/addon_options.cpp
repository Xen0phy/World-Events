//################################################################################
// addon_options.cpp
//--------------------------------------------------------------------------------
// AddonOptions()   draws the World Events section of the Nexus options panel
//--------------------------------------------------------------------------------
// Nexus UI callback - draws into a panel Nexus owns, not a standalone window. It
// holds one button that opens the settings window (options_window.h), where every
// setting, event, group and category is edited. There is no explicit "Save"
// button anywhere: edits live in memory and are written to disk on AddonUnload
// (see addon.cpp).
//--------------------------------------------------------------------------------

#include "addon.h"
#include "imgui.h"
#include "localization.h"
#include "options_window.h"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// AddonOptions   (see: addon.h)
//--------------------------------------------------------------------------------
void AddonOptions()
{
    if (ImGui::Button(Tr("WE_OPTWIN_OPEN_BUTTON")))
        OpenOptionsWindow();
}
