//################################################################################
// reset_defaults.h
//--------------------------------------------------------------------------------
// "Default" button for the Events tab of the options panel: deletes events.json
// and rebuilds every list it holds - Basic Events, Cyclic Groups, categories,
// subscriptions/watchlist, and done-today markers - back to the compiled-in
// roster, wiping any user edits/customization along with it. Destructive and
// irreversible in-session, so a confirm popup sits between the button and
// ResetAllDataToDefaults() (addon.h) actually running.
//--------------------------------------------------------------------------------

#pragma once

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawResetToDefaultsButton / DrawResetToDefaultsPopup
//--------------------------------------------------------------------------------
// Call DrawResetToDefaultsButton() somewhere in AddonOptions() to add the
// "Default" button; it opens the confirm popup managed here. Call
// DrawResetToDefaultsPopup() every frame from AddonOptions(), regardless of
// whether the popup is currently open, so it renders while open.
//--------------------------------------------------------------------------------
void DrawResetToDefaultsButton();
void DrawResetToDefaultsPopup();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// DrawRestoreMissingButton
//--------------------------------------------------------------------------------
// "Rebuild Missing" button for the Events tab, meant to sit right next to
// DrawResetToDefaultsButton(). Unlike Default/Reset, this is non-destructive and
// needs no confirm popup: it calls RestoreMissingDefaults() (events_storage.h)
// and RestoreMissingCategories() (events_categories.h) to add back any built-in
// Basic Event, Cyclic Group, Cyclic Slot, or category that's gone missing, while
// leaving every existing entry (customized or player-added) and every
// subscription/done-today marker untouched. Reports how many entries it added
// back (or that nothing was missing) as inline text next to the button,
// persisting until clicked again. Self-contained - no matching "every frame" draw
// call needed, unlike DrawResetToDefaultsPopup.
//--------------------------------------------------------------------------------
void DrawRestoreMissingButton();