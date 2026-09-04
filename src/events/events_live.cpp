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

//_ continentX/Y sourced same as WorldEvent's (see events_basic.cpp)
std::vector<LiveEvent> g_LiveEvents =
{
    {"5869C555-53AF-4701-876B-02BFB5F0AD7A", "Treasure Mushroom (Verdant Brink)",
        33716.0f, 32109.0f,
        1052, "[&BBwIAAA=]",
        -747.0f, 369.0f, -223.0f, 100.0f},
    {"38F28682-B136-4B4D-9F87-E15379771C72", "Treasure Mushroom (Auric Basin)",
        34980.0f, 34242.0f,
        1043, "[&BGwIAAA=]",
        412.0f, 241.0f, -196.0f, 100.0f},
    {"EAE3DF8F-244D-4BC3-9B7E-3B3A2BDAAFAB", "Treasure Mushroom (Tangled Depth)",
        36496.0f, 34112.0f,
        1045, "[&BAwIAAA=]",
        -302.0f, 108.0f, 507.0f, 100.0f},
    {"4A9E691A-BBFD-4C17-B2D6-D0B88D7C446F", "Treasure Mushroom (Dragon's Stand North)",
        35789.0f, 36581.0f,
        1041, "[&BFIIAAA=]",
        125.0f, 47.0f, 562.0f, 100.0f},
    {"32C14803-A210-4443-B8F4-F91641035B81", "Treasure Mushroom (Dragon's Stand Center)",
        36060.0f, 37314.0f,
        1041, "[&BFMIAAA=]",
        290.0f, 105.0f, 115.0f, 100.0f},
    {"1ADCD34C-3281-4A52-9C4F-DBA62B75F94C", "Treasure Mushroom (Dragon's Stand South)",
        36582.0f, 37602.0f,
        1041, "[&BGUIAAA=]",
        608.0f, 44.0f, -59.0f, 100.0f},
    {"704DF521-84B3-46C1-9FE4-304AF55E6FB6", "Treasure Mushroom (Bloodstone Fen)",
        35389.0f, 30570.0f,
        1165, "[&BD8JAAA=]",
        -118.0f, 511.0f, 92.0f, 100.0f},
    {"D3109A18-8A51-4F79-94EB-B474024063F0", "Treasure Mushroom (Draconis Mons)",
        36469.0f, 41154.0f,
        1195, "[&BNUJAAA=]",
        -102.0f, 360.0f, 30.0f, 50.0f},

    {"6BD7C8B0-2605-4819-9AE6-EF2849098090", "Rhendak the Crazed (Diessa Plateau)",
        58547.0f, 27859.0f,
        32, "[&BNwAAAA=]",
        265.0f, -57.0f, 573.0f, 25.0f},
    {"95CA969B-0CC6-4604-B166-DBCCE125864F", "Dredge Commissar (Dredgehaunt Cliffs)",
        52515.0f, 34472.0f,
        26, "[&BFYCAAA=]",
        -524.0f, 30.0f, -883.0f, 15.0f},

    {"B4E6588F-232C-4F68-9D58-8803D67E564D", "Foulbear Kraal (Fields of Ruin)",
        63363.0f, 34732.0f,
        21, "[&BE4BAAA=]",
        236.0f, 55.0f, -339.0f, 200.0f},
    {"A0796EC5-191D-4389-9C09-E48829D1FDB2", "Eye of Zhaitan (Straits of Devestation)",
        47908.0f, 41362.0f,
        51, "[&BPoCAAA=]",
        -606.0f, 15.0f, -713.0f, 200.0f},
    {"BFA71CD0-ED1E-4EE3-B8A9-B23B98C3B786", "Statue of Lyssa (Malchor's Leap)",
        45845.0f, 40318.0f,
        65, "[&BK0CAAA=]",
        403.0f, 96.0f, 156.0f, 200.0f},
    {"02DECBE6-A0BA-47CC-9256-A6D59881D92A", "High Wizard (Cursed Shore)",
        44623.0f, 44145.0f,
        62, "[&BCADAAA=]",
        438.0f, 88.0f, -303.0f, 150.0f},
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// IsPlayerNearLiveEvent   (see: events_live.h)
//--------------------------------------------------------------------------------
// Compares mumble.Context.MapID (unsigned) against event.mapId (int, GW2 API's
// map_id) via the wider type. Distance is full 3D Euclidean since the API's
// location.type is "sphere", not a flat ground radius - matters on maps with
// stacked geometry sharing the same X/Z at different elevations.
//--------------------------------------------------------------------------------
bool IsPlayerNearLiveEvent(const LiveEvent& event, const Mumble::Data& mumble)
{
    //_ mumble.Context.MapID is unsigned - compared via the wider type (see above).
    if (mumble.Context.MapID != (unsigned)event.mapId)
        return false;

    float dx = mumble.AvatarPosition.X - event.worldX;
    float dy = mumble.AvatarPosition.Y - event.worldY;
    float dz = mumble.AvatarPosition.Z - event.worldZ;

    //_ Full 3D sphere check - see events_live.h on why not a flat 2D radius.
    float distSq = dx * dx + dy * dy + dz * dz;
    return distSq <= (event.radius * event.radius);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// MapHasLiveEvents   (see: events_live.h)
//--------------------------------------------------------------------------------
bool MapHasLiveEvents(int mapId)
{
    for (const LiveEvent& ev : g_LiveEvents)
        if (ev.mapId == mapId)
            return true;
    return false;
}