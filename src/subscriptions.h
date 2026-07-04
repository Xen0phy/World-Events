#pragma once
#include <chrono>
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

// ---------------------------------------------------------------------------
// PasteToChat
// ---------------------------------------------------------------------------
// Sends `message` to whatever window was focused when this was called, via
// Enter -> Ctrl+V (paste) -> Enter, simulated on a detached background thread.
//
// NOTE: uses a deliberate mix of SendMessage (Enter/V) and SendInput (Ctrl).
// This is empirically required for the target app — see notes further down.
// Do not "simplify" to one mechanism without re-testing against a real target.
//
// Guarded by `send_in_progress` against overlapping calls (e.g. double-clicks).
// ---------------------------------------------------------------------------
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms);

// ---------------------------------------------------------------------------
// Subscriptions watchlist window
// ---------------------------------------------------------------------------
// A small, standalone, open/closeable ImGui window listing every
// subscribed Basic Event and Cyclic Event slot (see subscriptions.h), each
// with a live "Active" / "in Xm Ys" countdown — the same status text
// already shown in the map tooltips, just collected in one place so the
// user doesn't have to go hover markers on the map to check them.
//
// Visibility is a persisted setting (ShowSubscriptionsWindow in
// settings_table.h), toggled from a button/menu entry in the options
// panel — see addon_options.cpp.
// ---------------------------------------------------------------------------

// Draws the watchlist window if ShowSubscriptionsWindow is true. No-op
// (and cheap — an early-out before any ImGui calls) when false. Call this
// once per frame from AddonRender, same as RenderMapEvents/
// RenderCyclicGroups.
void RenderSubscriptionsWindow();

// ---------------------------------------------------------------------------
// Subscriptions distribution bar
// ---------------------------------------------------------------------------
// A second, alternate view of the same watchlist data already shown by
// RenderSubscriptionsWindow (subscriptions_window.h/.cpp) — instead of a
// scrollable text list, this draws every subscribed Basic Event / Cyclic
// slot as a colored segment laid out along a fixed 2-hour timeline bar,
// so upcoming/active windows read at a glance as a strip of blocks rather
// than a stack of "in Xm Ys" rows. Visually modeled on the reference
// distribution-line.html mock (segments = colored blocks along a line,
// thin background-colored notches between adjacent segments).
//
// Each segment uses the SAME color the subscribed event/slot already has
// elsewhere in the addon: a Cyclic slot uses CyclicGroup::SlotColor(), a
// Basic Event uses a stable per-name color (Basic Events have no color of
// their own outside the map's global Active/Soon/Waiting scheme — see the
// long comment on BasicEventColorFor in subscriptions_bar.cpp for why a
// deterministic name hash is used instead of reusing that scheme here).
//
// Time range is fixed at 2 hours (now .. now+2h) — NOT user-configurable,
// per the call made this session; this deliberately keeps the bar reading
// as a fixed, comparable "next two hours" strip rather than a zoomable
// timeline. A "now" marker is drawn at the left edge; segments that are
// currently active are clipped to start at the marker (they already
// started before the visible window) and segments that don't start
// within the next 2 hours are simply not drawn.
//
// Visibility is a persisted setting (ShowSubscriptionsBar in
// settings_table.h), toggled from a button/menu entry in the options
// panel — see addon_options.cpp — same pattern as ShowSubscriptionsWindow.
// ---------------------------------------------------------------------------

// Draws the distribution bar window if ShowSubscriptionsBar is true. No-op
// (cheap early-out before any ImGui calls) when false. Call this once per
// frame from AddonRender, alongside RenderSubscriptionsWindow.
void RenderSubscriptionsBar();