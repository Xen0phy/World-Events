//################################################################################
// events_live.cpp
//--------------------------------------------------------------------------------
// g_LiveEvents   compiled-in LiveEvent roster (see events_live.h)
//--------------------------------------------------------------------------------
// Hand-written roster, same spirit as events_basic.cpp/events_cyclic.cpp: no
// logic, grouped by comment banner. eventId/name/mapId/continentX/Y and the
// worldX/Y/Z+radius sphere come straight from the GW2 API v2 /events endpoint -
// see events_live.h for why worldX/Y/Z/radius aren't used for anything yet.
//--------------------------------------------------------------------------------

#include "events_live.h"

//_ continentX/Y sourced same as WorldEvent's (see events_basic.cpp); worldX/Y/Z/radius copied verbatim from the API's location.center/radius, not yet used.
std::vector<LiveEvent> g_LiveEvents =
{
    {"5869C555-53AF-4701-876B-02BFB5F0AD7A", "Treasure Mushroom (Verdant Brink)",
        33980.0f, 33242.0f,
        1052, -747.0f, 369.0f, -223.0f, 100.0f},
    {"38F28682-B136-4B4D-9F87-E15379771C72", "Treasure Mushroom (Auric Basin)",
        34980.0f, 34242.0f,
        1043, 412.0f, 241.0f, -196.0f, 100.0f},
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsPlayerNearLiveEvent   (see: events_live.h)
//--------------------------------------------------------------------------------
// Compares mumble.Context.MapID (unsigned) against event.mapId (int, GW2 API's
// map_id) via the wider type. Distance is full 3D Euclidean since the API's
// location.type is "sphere", not a flat ground radius - matters on maps with
// stacked geometry sharing the same X/Z at different elevations.
//
// CONFIRMED IN-GAME: no unit conversion needed between mumble.AvatarPosition and
// worldX/Y/Z. An earlier revision assumed Mumble's generic meters spec applied
// and multiplied by 39.3701 for the API's inches; untested and wrong - GW2's own
// implementation already reports raw engine units. The conversion in place never
// triggered proximity on a real live event; removing it fixed it.
//--------------------------------------------------------------------------------
bool IsPlayerNearLiveEvent(const LiveEvent& event, const Mumble::Data& mumble)
{
    //_ mumble.Context.MapID is unsigned - compared via the wider type (see above).
    if (mumble.Context.MapID != (unsigned)event.mapId)
        return false;

    //_ No unit conversion - worldX/Y/Z is already in mumble.AvatarPosition's raw engine units (see above).
    float dx = mumble.AvatarPosition.X - event.worldX;
    float dy = mumble.AvatarPosition.Y - event.worldY;
    float dz = mumble.AvatarPosition.Z - event.worldZ;

    //_ Full 3D sphere check - see events_live.h on why not a flat 2D radius.
    float distSq = dx * dx + dy * dy + dz * dz;
    return distSq <= (event.radius * event.radius);
}

bool MapHasLiveEvents(int mapId)
{
    for (const LiveEvent& ev : g_LiveEvents)
        if (ev.mapId == mapId)
            return true;
    return false;
}