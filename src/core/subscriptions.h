//################################################################################
// subscriptions.h
//--------------------------------------------------------------------------------
// CyclicSubscriptionKey                     (group name, slot offset) key
// SubscriptionKind                          Basic / Cyclic / Live
// g_SubscribedBasicEvents/CyclicSlots        the watchlist itself
// g_ToastEnabledBasicEvents/CyclicSlots      toast opt-in lists
// g_SoundEnabledBasicEvents/CyclicSlots      sound opt-in lists
// g_SubscribedLiveEvents                     Live Events watchlist - toast
//                                             opt-in only, see below
// Get/SetBasicEventNotifyLevel               0..3 ladder, Basic Events
// Get/SetCyclicSlotNotifyLevel               0..3 ladder, Cyclic slots
// GetSubscriptionListGeneration()            change counter for the watchlist
// Save/LoadSubscriptionsData()               persistence to events.json
// PasteToChat/BuildChatPasteMessage          watchlist-row chat paste helpers
// WhisperToChat                              arbitrary-target /w, chat paste
// GetMumbleCharacterName                     local player's name, for reports
//--------------------------------------------------------------------------------
// Data model for the user's subscribed-events watchlist, surfaced in the
// subscriptions UI (see ui/subscriptions_ui.h). References existing event/slot
// data by name/key instead of owning a copy - the render code looks up the live
// WorldEvent/CyclicGroup::Slot in g_Events/g_CyclicGroups every frame. Basic
// Events are keyed by name; Cyclic Events are keyed per occurrence (group name,
// slot offset), since slot names aren't unique within a group but offsets are.
//
// A Basic/Cyclic subscription may also opt into a toast popup and, on top of
// that, a notification sound - one 0..3 "notify level" instead of three
// independent bools (0 unsubscribed, 1 silent, 2 +toast, 3 +sound), each level
// implying every level below it. Set...NotifyLevel brings all three lists into
// agreement in one call; Get...NotifyLevel derives its answer from them live.
//
// The three UI views over this data - RenderSubscriptionsWindow/Bar/
// Notifications - are declared in ui/subscriptions_ui.h, implemented in their own
// .cpp files, not here.
//
// g_SubscribedLiveEvents is simpler: keyed by LiveEvent::eventId, one list
// instead of three - subscribing is itself the toast opt-in, independent of
// LiveEventsSubscribed (settings_table.h), which gates reporting instead.
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// SubscriptionKind
//--------------------------------------------------------------------------------
// Which of the three event flavors a row/candidate/popup/deep-link target
// identifies (live-toast-handoff.md section 6). Basic/Cyclic identify by name /
// CyclicSubscriptionKey as before; Live identifies by LiveEvent::eventId, carried
// alongside as a plain string wherever this enum appears - an enum instead of the
// old binary isBasic bool, so a Live identity has somewhere to go instead of
// being forced into one of the other two.
//--------------------------------------------------------------------------------
enum class SubscriptionKind
{
    Basic,
    Cyclic,
    Live,
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
// Empties every list above (subscribed/toast/sound for Basic and Cyclic, plus the
// Live Events watchlist) and bumps the generation counter once. Unlike calling
// LoadSubscriptionsData with no file on disk - which leaves these lists untouched
// instead of clearing them - this always empties them, so it's the right call for
// an explicit "wipe subscriptions" action mid-session.
//--------------------------------------------------------------------------------
void ClearAllSubscriptions();

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// RenameSubscribedBasicEvent
//--------------------------------------------------------------------------------
// Patches a Basic Event subscription from oldName to newName, including its
// toast/sound entries. No-op if oldName isn't subscribed.
//--------------------------------------------------------------------------------
void RenameSubscribedBasicEvent(const std::string& oldName, const std::string& newName);

extern std::vector<std::string> g_SubscribedLiveEvents; //. keyed by LiveEvent::eventId

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsLiveEventSubscribed / ToggleLiveEventSubscription
//--------------------------------------------------------------------------------
// Query/toggle helpers over g_SubscribedLiveEvents. Subscribing is itself the
// toast opt-in (see file header) - no separate toast/sound tier the way Basic
// Events and Cyclic slots have.
//--------------------------------------------------------------------------------
bool IsLiveEventSubscribed(const std::string& eventId);
void ToggleLiveEventSubscription(const std::string& eventId);

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
// Persists/loads the seven lists above (Basic/Cyclic subscribed+toast+sound, plus
// g_SubscribedLiveEvents) as top-level keys in the same events.json used by
// g_Events/g_CyclicGroups/categories. Call SaveEventsData() first - this reads
// the file back in and rewrites it. Both swallow exceptions and return false on
// failure.
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

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// WhisperToChat   (pairs with: PasteToChat)
//--------------------------------------------------------------------------------
// Sends "/w <targetName> <message>" via the same three-segment
// PasteSegmentsToChat shape PasteToChat's own /w branch uses, generalized to an
// arbitrary target instead of always whispering the local player's own name
// (live-toast-handoff.md section 6). ChatChannelPrefix plays no part here - the
// whisper channel is fixed regardless of the user's configured prefix. Used for a
// Live Event toast whose reporter shared their name
// (subscriptions_notification.cpp); a no-op on an empty targetName.
//--------------------------------------------------------------------------------
void WhisperToChat(const std::string& targetName, const std::string& message, std::chrono::milliseconds delay_ms);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// GetMumbleCharacterName
//--------------------------------------------------------------------------------
// Local player's character name, straight from Mumble identity - empty if
// unavailable (MumbleLink not ready, malformed identity, no name). Used as the
// reporter_name passed to SendReport when ShareNameInReports is on
// (live_events_ui.cpp); see subscriptions.cpp for the UTF-16 parse/narrow this
// does to get there.
//--------------------------------------------------------------------------------
std::string GetMumbleCharacterName();
