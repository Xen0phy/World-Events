//################################################################################
// events_storage.h
//--------------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in
// "<addonDir>/events.json". See events_storage.cpp for the file format and
// merge rules used on load.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveEventsData / LoadEventsData
//--------------------------------------------------------------------------------
// Both functions swallow exceptions and return false on failure.
//--------------------------------------------------------------------------------
bool SaveEventsData(const std::string& addonDir);
bool LoadEventsData(const std::string& addonDir);