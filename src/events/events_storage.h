#pragma once
#include <string>

// ---------------------------------------------------------------------------
// events_storage.h
// ---------------------------------------------------------------------------
// JSON persistence for g_Events and g_CyclicGroups, both stored together in
// "<addonDir>/events.json". See events_storage.cpp for the full file format
// and the name-keyed merge rules used on load.
//
// Both functions swallow exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveEventsData(const std::string& addonDir);
bool LoadEventsData(const std::string& addonDir);
