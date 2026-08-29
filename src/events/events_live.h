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
// agree on, so they ship compiled-in only, the same way bundled_icons.h's icon
// table isn't user-editable.
//
// What IS user-controlled is whether the whole feature is on at all - see
// LiveEventsSubscribed (settings_table.h). There's no per-event opt-in: when
// subscribed, every entry in g_LiveEvents is followed at once.
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
// radius        API's location.radius, in meters - same units as
//               continentX/Y and worldX/Y/Z (see IsPlayerNearLiveEvent).
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
// IsPlayerNearLiveEvent
//--------------------------------------------------------------------------------
// True only when the player is on event.mapId AND within event.radius (full 3D
// sphere) of (event.worldX, event.worldY, event.worldZ). Gates the report button
// per networking-handoff.md #3/#4 - a LiveEvent's button should only be offered
// when this returns true, so players can't report something they aren't actually
// near. See events_live.cpp for the unit-conversion story.
//--------------------------------------------------------------------------------
bool IsPlayerNearLiveEvent(const LiveEvent& event, const Mumble::Data& mumble);

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MapHasLiveEvents
//--------------------------------------------------------------------------------
// True if any entry in g_LiveEvents has this mapId. Gates whether the client
// opens a shard connection at all (live_events_ui.cpp) - the relay server
// enforces the same allow-list independently (server/src/index.ts).
//--------------------------------------------------------------------------------
bool MapHasLiveEvents(int mapId);