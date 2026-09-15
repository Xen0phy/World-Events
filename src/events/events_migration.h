//################################################################################
// events_migration.h
//--------------------------------------------------------------------------------
// MigrateLegacyEventsFile   one-shot forward migration from the pre-1.8.0.0
//                           name-keyed events.json into current id-keyed state
//--------------------------------------------------------------------------------
// Must run before LoadEventsData/LoadCategoriesData/LoadSubscriptionsData, while
// g_Events/g_CyclicGroups/g_Subscribed... still hold pristine compiled-in
// defaults (see addon.cpp) - it edits those in place by matching old rows to
// today's compiled defaults by coordinate, then deletes the legacy file so the
// normal Load*Data calls that follow see a fresh install and simply persist the
// now-migrated state. Only "shown" (map visibility) and subscription/notify-
// level state carry forward, each only when it disagrees with the new build's
// default; everything else stays at the compiled default (see
// events_migration.cpp for the matching and field rules). No-op, and doesn't
// touch the file, once LastKnownVersion is already 1080000 or newer.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

void MigrateLegacyEventsFile(const std::string& addonDir);