//################################################################################
// reset_defaults.h
//--------------------------------------------------------------------------------
// "Default" button for the Events tab of the options panel: deletes
// events.json and rebuilds every list it holds - Basic Events, Cyclic
// Groups, categories, subscriptions/watchlist, and done-today markers -
// back to the compiled-in roster, wiping any user edits/customization
// along with it. Destructive and irreversible in-session, so a confirm
// popup sits between the button and ResetAllDataToDefaults() (addon.h)
// actually running.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawResetToDefaultsButton / DrawResetToDefaultsPopup
//--------------------------------------------------------------------------------
// Call DrawResetToDefaultsButton() somewhere in AddonOptions() to add the
// "Default" button; it opens the confirm popup managed here. Call
// DrawResetToDefaultsPopup() every frame from AddonOptions(), regardless
// of whether the popup is currently open, so it renders while open.
//--------------------------------------------------------------------------------
void DrawResetToDefaultsButton();
void DrawResetToDefaultsPopup();
