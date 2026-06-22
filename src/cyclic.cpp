#include "cyclic.h"

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
// DarkenColor scales the R, G, B channels by `factor` while keeping alpha.
// Use it to derive secondary/tertiary shades from a single seed color.
// ---------------------------------------------------------------------------
ColorSet COL_LWS2 { 0x715F18FF };
ColorSet COL_HOT  { 0x667118FF };
ColorSet COL_LWS3 { 0x52763BFF };
ColorSet COL_POF  { 0x976320FF };
ColorSet COL_LWS4 { 0x7E3494FF };
ColorSet COL_IBS  { 0x206697FF };
ColorSet COL_EOD  { 0x208B97FF };
ColorSet COL_SOTO { 0xD19C3CFF };
ColorSet COL_JW   { 0x18347EFF };
ColorSet COL_VOE  { 0xAB401AFF };

std::vector<CyclicGroup> g_CyclicGroups =
{
    // — each map has its own 2h cycle with sequential events.
    // Offsets are seconds from UTC midnight of each event's first occurrence.

    // ---------------------------------------------------------------------------
    // Living World Season 2
    // ---------------------------------------------------------------------------
    { // Dry Top
        37129.0f, 32802.0f, 3600,
        {
            {"Crash Site", 0,    2400, COL_LWS2.ter()},
            {"Crash Site", 2400, 1200, COL_LWS2.pri()},
        }
    },
    
    // ---------------------------------------------------------------------------
    // Heart of Thorns
    // ---------------------------------------------------------------------------
    { // Verdant Brink
        35049.0f, 31779.0f, 7200,
        {
            { "Night Bosses", 600,  1200, COL_HOT.pri() },
            { "Securing Day", 1800, 4500, COL_HOT.ter() },
            { "Night Enemy",  5700, 1500, COL_HOT.sec() },
        }
    },
    { // Auric Basin
        34303.0f, 33915.0f, 7200,
        {
            { "Challenges", 2700, 900,  COL_HOT.sec() },
            { "Octovine",   3600, 1200, COL_HOT.pri() },
            { "Rest",       4800, 600,  COL_HOT.sec() },
            { "Pylons",     5400, 4500, COL_HOT.ter() },
        }
    },
    { // Tangled Depths
        37010.0f, 35040.0f, 7200,
        {
            { "Prep",        1500, 300,  COL_HOT.sec() },
            { "Chak Gerent", 1800, 2400, COL_HOT.pri() },
            { "Outposts",    4200, 4500, COL_HOT.ter() },
        }
    },
    { // Dragon's Stand
        35709.0f, 37182.0f, 7200,
        {
            { "Mordremoth Start",    5400, 1800, COL_HOT.pri() },
            { "Mordremoth Progress", 0,    5400, COL_HOT.sec() },
        }
    },

    // ---------------------------------------------------------------------------
    // Living World Season 3
    // ---------------------------------------------------------------------------
    { // Lake Doric
        45564.0f, 27004.0f, 7200,
        {
            {"Saidra's Haven",    0,    2700, COL_LWS3.sec()},
            {"New Loamhurst",     2700, 2700, COL_LWS3.pri()},
            {"Noran's Homestead", 5400, 1800, COL_LWS3.ter()},
        }
    },

    // ---------------------------------------------------------------------------
    // Path of Fire
    // ---------------------------------------------------------------------------
    { // Crystal Oasis
        58692.0f, 43752.0f, 7200,
        {
            { "Casino Rounds", 300,  900,  COL_POF.sec() },
            { "Choya Pinata",  1200, 600,  COL_POF.pri() },
            { "Rest",          1800, 5700, COL_POF.ter() },
        }
    },
    { // Desert Highlands
        59964.0f, 41384.0f, 7200,
        {
            { "Buried Treasure", 3600, 1200, COL_POF.pri() },
            { "Rest",            4800, 6000, COL_POF.ter() },
        }
    },
    { // Elon Riverlands
        60715.0f, 45646.0f, 7200,
        {
            { "Path of Ascension", 5400, 1500, COL_POF.sec() },
            { "Doppelganger",      6900, 1200, COL_POF.pri() },
            { "Rest",              900,  4500, COL_POF.ter() },
        }
    },
    { // The Desolation
        59943.0f, 50257.0f, 7200,
        {
            { "Junundu Rising",  1800, 1200, COL_POF.pri() },
            { "Rest",            3000, 600,  COL_POF.ter() },
            { "Maws of Torment", 3600, 1200, COL_POF.sec() },
            { "Rest",            4800, 600,  COL_POF.ter() },
            { "Junundu Rising",  5400, 1200, COL_POF.pri() },
            { "Rest",            6600, 2400, COL_POF.ter() },
        }
    },
    { // Domain of Vabbi
        66332.0f, 53596.0f, 7200,
        {
            { "Forged with Fire", 0,    1800, COL_POF.pri() },
            { "Serpents' Ire",    1800, 1800, COL_POF.sec() },
            { "Forged with Fire", 3600, 1800, COL_POF.pri() },
            { "Rest",             5400, 1800, COL_POF.ter() },
        }
    },
    
    // ---------------------------------------------------------------------------
    // Living World Season 4
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // Icebrood Saga
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // End of Dragons
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // Secrets of the Obscure
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // Janthir Wilds
    // ---------------------------------------------------------------------------

    // ---------------------------------------------------------------------------
    // Visions of Eternity
    // ---------------------------------------------------------------------------
};