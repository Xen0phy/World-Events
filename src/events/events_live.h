//################################################################################
// events_live.h
//--------------------------------------------------------------------------------
// LiveEvent            one player-reportable live event
// g_LiveEvents          compiled-in roster (see events_live.cpp)
//--------------------------------------------------------------------------------
// Third event category, alongside "Basic Events" (WorldEvent, events.h) and
// "Cyclic Events" (CyclicGroup, events.h). Unlike those two, a LiveEvent has NO
// schedule of its own - GW2 doesn't expose one, which is the entire reason the
// live-reporting feature (see networking-handoff.md) exists: players report "it's
// up right now" instead of the addon predicting it.
//
// Compiled-in, not user-editable. g_Events/g_CyclicGroups go through
// events_storage.cpp's JSON merge and maprender.cpp's drag-to-reposition edit
// mode (EditTarget::BasicEvent/CyclicGroup) so a user's own additions/tweaks
// survive updates; g_LiveEvents gets neither. Position, name, and id are only
// ever meaningful if they match what every other client and the relay server
// agree on, so they ship compiled-in only, the same way events_icons.h's icon
// table isn't user-editable.
//
// What IS user-controlled is whether a given LiveEvent's dot is shown at all -
// see IsLiveEventActivated()/ToggleLiveEventActivation() below. Opt-in and
// defaulting to false, unlike WorldEvent::shown (opt-out, defaults true): there's
// no schedule to filter noise by here, so an unfiltered map would be wall-to-wall
// dots for events most players will never care about.
//--------------------------------------------------------------------------------

#pragma once

#include "Mumble.h"

#include <string>
#include <vector>

//********************************************************************************
// LiveEvent
//--------------------------------------------------------------------------------
// eventId       GW2 API v2 /events GUID; doubles as the wire protocol's
//               event_id (networking-handoff.md #5) - same id for both.
// name          display name
// continentX/Y  map coords (continent 1 / Tyria) - same space as
//               WorldEvent::continentX/Y, what actually places the dot
// mapId         GW2 map id (API's map_id); gates which map's overlay/report
//               button offers this dot - only relevant on the map it occurs on
// worldX/Y/Z    API's location.center, in-world (not continent) coordinates -
//               same space as Mumble Link's raw avatar position (see below)
// radius        API's location.radius; sphere radius in the same space/units
//               as worldX/Y/Z (see IsPlayerNearLiveEvent)
//--------------------------------------------------------------------------------
// One live, player-reported event: an always-available, always-compiled-in dot
// definition with no timer of its own. Whether it's currently "up" comes entirely
// from GetRecentReports(eventId) (ws_client.h) at render time, not from anything
// stored here.
//
// worldX/Y/Z is a separate 3D gameplay-space coordinate from continentX/Y's 2D
// map-UI projection, not interchangeable - comparing the two directly looks
// meaningless because it's not supposed to match; worldX/Y/Z is only ever meant
// to be compared against the player's live position (IsPlayerNearLiveEvent).
//--------------------------------------------------------------------------------
struct LiveEvent
{
    std::string eventId;
    std::string name;
    float       continentX;
    float       continentY;
    int         mapId;

    float       worldX;
    float       worldY;
    float       worldZ;
    float       radius;
};

//_ Populated in events_live.cpp. Compiled-in only - see file header.
extern std::vector<LiveEvent> g_LiveEvents;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsLiveEventActivated / ToggleLiveEventActivation
//--------------------------------------------------------------------------------
// User's per-event opt-in for whether the dot is drawn at all (see file header -
// this is the ONLY thing about a LiveEvent a user controls). Keyed by eventId.
// Defaults to false/not-activated for any id not yet toggled on.
//
// Kept separate from g_SubscribedBasicEvents/g_SubscribedCyclicSlots
// (subscriptions.h): Subscriptions opt into notifications for a scheduled
// occurrence ("tell me when this starts"), which doesn't apply here - there's no
// schedule to be notified ahead of. This is purely "show/hide this dot," closer
// to WorldEvent::shown than to a subscription, just opt-in instead of opt-out.
//--------------------------------------------------------------------------------
bool IsLiveEventActivated(const std::string& eventId);
void ToggleLiveEventActivation(const std::string& eventId);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsPlayerNearLiveEvent
//--------------------------------------------------------------------------------
// True only when the player is on event.mapId AND within event.radius (full 3D
// sphere) of (event.worldX, event.worldY, event.worldZ). Gates the report button
// per networking-handoff.md #3/#4 - a LiveEvent's button should only be offered
// when this returns true, so players can't report something they aren't actually
// near. See events_live.cpp for the unit-conversion story.
//--------------------------------------------------------------------------------
bool IsPlayerNearLiveEvent(const LiveEvent& event, const Mumble::Data& mumble);