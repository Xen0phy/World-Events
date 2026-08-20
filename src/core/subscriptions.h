//################################################################################
// subscriptions.h
//--------------------------------------------------------------------------------
// CyclicSubscriptionKey                     (group name, slot offset) key
// g_SubscribedBasicEvents/CyclicSlots        the watchlist itself
// g_ToastEnabledBasicEvents/CyclicSlots      toast opt-in lists
// g_SoundEnabledBasicEvents/CyclicSlots      sound opt-in lists
// Get/SetBasicEventNotifyLevel               0..3 ladder, Basic Events
// Get/SetCyclicSlotNotifyLevel               0..3 ladder, Cyclic slots
// GetSubscriptionListGeneration()            change counter for the watchlist
// Save/LoadSubscriptionsData()               persistence to events.json
// PasteToChat/BuildChatPasteMessage          watchlist-row chat paste helpers
//--------------------------------------------------------------------------------
// Data model for the user's subscribed-events watchlist, surfaced in the
// subscriptions UI (see ui/subscriptions_ui.h). References existing event/slot
// data by name/key instead of owning a copy - the render code looks up the live
// WorldEvent/CyclicGroup::Slot in g_Events/g_CyclicGroups every frame. Basic
// Events are keyed by name; Cyclic Events are keyed per occurrence (group name,
// slot offset), since slot names aren't unique within a group but offsets are.
//
// A subscription may also opt into a toast popup and, on top of that, a
// notification sound - modeled as one 0..3 "notify level" instead of three
// independent bools, since it's a strict ladder (0 unsubscribed, 1 silent, 2
// +toast, 3 +sound): each level always implies every level below it, and
// Set...NotifyLevel brings all three lists into agreement in one call.
// Get...NotifyLevel derives its answer from those lists live, so it can never
// disagree with the Is...Subscribed/ToastEnabled/SoundEnabled queries.
//
// The three UI views over this data - RenderSubscriptionsWindow,
// RenderSubscriptionsBar, RenderSubscriptionsNotifications - are declared in
// ui/subscriptions_ui.h, with implementations in their own .cpp files, not here.
//--------------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

//********************************************************************************
// CyclicSubscriptionKey
//--------------------------------------------------------------------------------
// groupName    name of the cyclic group
// slotOffset   offset of the slot within that group
//--------------------------------------------------------------------------------
struct CyclicSubscriptionKey
{
    std::string groupName;
    int         slotOffset = 0;

    bool operator==(const CyclicSubscriptionKey& other) const
    {
        return groupName == other.groupName && slotOffset == other.slotOffset;
    }
};

extern std::vector<std::string>            g_SubscribedBasicEvents; //. keyed by WorldEvent::name
extern std::vector<CyclicSubscriptionKey>  g_SubscribedCyclicSlots; //. keyed by group/offset

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventSubscribed / ToggleBasicEventSubscription
//--------------------------------------------------------------------------------
// Query/toggle helpers over g_SubscribedBasicEvents.
//--------------------------------------------------------------------------------
bool IsBasicEventSubscribed(const std::string& eventName);
void ToggleBasicEventSubscription(const std::string& eventName);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsCyclicSlotSubscribed / ToggleCyclicSlotSubscription
//--------------------------------------------------------------------------------
// Query/toggle helpers over g_SubscribedCyclicSlots.
//--------------------------------------------------------------------------------
bool IsCyclicSlotSubscribed(const CyclicSubscriptionKey& key);
void ToggleCyclicSlotSubscription(const CyclicSubscriptionKey& key);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ClearAllSubscriptions
//--------------------------------------------------------------------------------
// Empties every list above (subscribed/toast/sound, Basic and Cyclic alike) and
// bumps the generation counter once. Unlike calling LoadSubscriptionsData with no
// file on disk - which leaves these lists untouched instead of clearing them -
// this always empties them, so it's the right call for an explicit "wipe
// subscriptions" action mid-session.
//--------------------------------------------------------------------------------
void ClearAllSubscriptions();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenameSubscribedBasicEvent
//--------------------------------------------------------------------------------
// Patches a Basic Event subscription from oldName to newName, including its
// toast/sound entries. No-op if oldName isn't subscribed.
//--------------------------------------------------------------------------------
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName);

extern std::vector<std::string>            g_ToastEnabledBasicEvents;  //. toast opt-in, Basic Events
extern std::vector<CyclicSubscriptionKey>  g_ToastEnabledCyclicSlots;  //. toast opt-in, Cyclic slots

extern std::vector<std::string>            g_SoundEnabledBasicEvents;  //. sound opt-in, Basic Events
extern std::vector<CyclicSubscriptionKey>  g_SoundEnabledCyclicSlots;  //. sound opt-in, Cyclic slots

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventToastEnabled / IsCyclicSlotToastEnabled
//--------------------------------------------------------------------------------
// Query helpers over the toast opt-in lists above.
//--------------------------------------------------------------------------------
bool IsBasicEventToastEnabled(const std::string& eventName);
bool IsCyclicSlotToastEnabled(const CyclicSubscriptionKey& key);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsBasicEventSoundEnabled / IsCyclicSlotSoundEnabled
//--------------------------------------------------------------------------------
// Query helpers over the sound opt-in lists above.
//--------------------------------------------------------------------------------
bool IsBasicEventSoundEnabled(const std::string& eventName);
bool IsCyclicSlotSoundEnabled(const CyclicSubscriptionKey& key);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetBasicEventNotifyLevel / SetBasicEventNotifyLevel
//--------------------------------------------------------------------------------
// 0..3 notify level for a Basic Event subscription (see file header). Set clamps
// level to 0..3 and brings the toast/sound lists into agreement.
//--------------------------------------------------------------------------------
int  GetBasicEventNotifyLevel(const std::string& eventName);
void SetBasicEventNotifyLevel(const std::string& eventName, int level);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetCyclicSlotNotifyLevel / SetCyclicSlotNotifyLevel
//--------------------------------------------------------------------------------
// Same as GetBasicEventNotifyLevel/SetBasicEventNotifyLevel, for Cyclic slots.
//--------------------------------------------------------------------------------
int  GetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key);
void SetCyclicSlotNotifyLevel(const CyclicSubscriptionKey& key, int level);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetSubscriptionListGeneration
//--------------------------------------------------------------------------------
// Bumped by exactly 1 on every change to the subscribed lists themselves
// (Toggle.../RenameSubscribedBasicEvent/LoadSubscriptionsData), so
// subscriptions_cache.cpp can cheaply detect that without re-deriving anything.
// NOT bumped by toast/sound-list-only changes: nothing in that cache reads those
// lists, so subscriptions_notification.cpp just reads them directly every frame
// instead.
//--------------------------------------------------------------------------------
uint64_t GetSubscriptionListGeneration();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SaveSubscriptionsData / LoadSubscriptionsData
//--------------------------------------------------------------------------------
// Persists/loads the six lists above as top-level keys in the same events.json
// used by g_Events/g_CyclicGroups/categories. Call SaveEventsData() first - this
// reads the file back in and rewrites it. Both swallow exceptions and return
// false on failure.
//--------------------------------------------------------------------------------
bool SaveSubscriptionsData(const std::string& addonDir);
bool LoadSubscriptionsData(const std::string& addonDir);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// PasteToChat / BuildChatPasteMessage
//--------------------------------------------------------------------------------
// BuildChatPasteMessage builds the unprefixed body for a watchlist
// row/segment/toast click - "<name>: <chatCode>" (or just <name>). Shared by
// subscriptions_window.cpp, subscriptions_bar.cpp, and
// subscriptions_notification.cpp.
//
// PasteToChat sends that body to the focused window, prefixed with the user's
// configured channel command (Settings::ChatChannelPrefix), via Enter -> Ctrl+V
// -> Enter on a detached background thread; send_in_progress guards against
// overlapping calls. Whisper (ChatChannelPrefix == "/w ") is the exception: see
// subscriptions.cpp.
//--------------------------------------------------------------------------------
void PasteToChat(const std::string& message, std::chrono::milliseconds delay_ms);
std::string BuildChatPasteMessage(const std::string& name, const std::string& chatCode);