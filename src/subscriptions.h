#pragma once
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------
// A user-picked "watchlist" of events, surfaced in a small standalone
// window (see subscriptions_window.h) instead of requiring the user to
// hover markers on the map. Like Category (categories.h), this is a thin
// layer that references existing data BY NAME rather than owning/copying
// it — a subscription never stores its own copy of a WorldEvent or a
// CyclicGroup::Slot, it just remembers which one(s) the user checked, and
// the render/window code looks the live data up in g_Events /
// g_CyclicGroups by name every frame.
//
// Basic Events are identified by their own name alone, same as
// everywhere else in this codebase (categories, the merge key in
// events_storage.cpp, etc).
//
// Cyclic Events are subscribed to PER SLOT (an individual occurrence
// within a group, e.g. just "Crash Site" at 14:00 UTC, not all of "Dry
// Top"), per the call made this session. A bare slot name is NOT a safe
// key on its own — slot names aren't unique even within one group (Dry
// Top ships two "Crash Site" slots at different offsets, same as the
// duplicate-name warning logic in addon_options.cpp already has to
// account for), so a cyclic subscription is keyed by (group name, slot
// offset) instead of (group name, slot name). offset is stable identity
// for a slot in a way name isn't: two slots can share a name, but two
// slots can't occupy the same offset within the same group without being
// the same schedule entry. If the user renames a slot, the subscription
// silently keeps pointing at the right occurrence — no rename-patching
// needed here, unlike Category's name-keyed membership.
// ---------------------------------------------------------------------------
struct CyclicSubscriptionKey
{
    std::string groupName;
    int         slotOffset = 0;

    bool operator==(const CyclicSubscriptionKey& other) const
    {
        return groupName == other.groupName && slotOffset == other.slotOffset;
    }
};

// Subscribed Basic Events, referenced by WorldEvent::name.
extern std::vector<std::string> g_SubscribedBasicEvents;

// Subscribed Cyclic Event occurrences (individual slots), referenced by
// (group name, slot offset).
extern std::vector<CyclicSubscriptionKey> g_SubscribedCyclicSlots;

// ---------------------------------------------------------------------------
// IsBasicEventSubscribed / ToggleBasicEventSubscription
// IsCyclicSlotSubscribed / ToggleCyclicSlotSubscription
// ---------------------------------------------------------------------------
// Small query/toggle helpers so call sites (the per-row checkbox in
// addon_options.cpp, the watchlist window) don't each hand-roll a linear
// search over the vectors above.
// ---------------------------------------------------------------------------
bool IsBasicEventSubscribed(const std::string& eventName);
void ToggleBasicEventSubscription(const std::string& eventName);

bool IsCyclicSlotSubscribed(const CyclicSubscriptionKey& key);
void ToggleCyclicSlotSubscription(const CyclicSubscriptionKey& key);

// ---------------------------------------------------------------------------
// RenameSubscribedBasicEvent
// ---------------------------------------------------------------------------
// Basic Event subscriptions ARE keyed by name (unlike cyclic slots, see
// above), so a rename needs the same old-name -> new-name patch that
// RenameCategoryMember does for categories. Call this from the same place
// a Basic Event rename happens in addon_options.cpp. No-op and safe if
// oldName isn't currently subscribed.
// ---------------------------------------------------------------------------
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName);

// ---------------------------------------------------------------------------
// SaveSubscriptionsData / LoadSubscriptionsData
// ---------------------------------------------------------------------------
// Persisted in the SAME events.json file as g_Events/g_CyclicGroups/
// categories (see events_storage.cpp / categories.cpp) — two more
// top-level keys, "subscribedBasicEvents" and "subscribedCyclicSlots",
// read/added without disturbing the rest of the file.
//
// ORDERING: call SaveEventsData() BEFORE this, same rule as
// SaveCategoriesData (see categories.h for the full reasoning — this
// reads the file first and writes it back, so it must run after whatever
// else is writing fresh events/cyclicGroups data that same pass).
//
// Both swallow exceptions and return false on failure.
// ---------------------------------------------------------------------------
bool SaveSubscriptionsData(const std::string& addonDir);
bool LoadSubscriptionsData(const std::string& addonDir);
