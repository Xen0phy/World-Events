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
    { "Dry Top",
        37129.0f, 32802.0f, 3600, COL_LWS2,
        {
            {"Crash Site", 0,    2400, ColorTier::Tertiary},
            {"Sandstorm",  2400, 1200, ColorTier::Primary},
        }
    },
    
    // ---------------------------------------------------------------------------
    // Heart of Thorns
    // ---------------------------------------------------------------------------
    { "Verdant Brink",
        35049.0f, 31779.0f, 7200, COL_HOT,
        {
            { "Night Bosses", 600,  1200, ColorTier::Primary },
            { "Securing Day", 1800, 4500, ColorTier::Tertiary },
            { "Night Enemy",  5700, 1500, ColorTier::Secondary },
        }
    },
    { "Auric Basin",
        34303.0f, 33915.0f, 7200, COL_HOT,
        {
            { "Challenges", 2700, 900,  ColorTier::Secondary },
            { "Octovine",   3600, 1200, ColorTier::Primary },
            { "Pylons",     5400, 4500, ColorTier::Tertiary },
        }
    },
    { "Tangled Depths",
        37010.0f, 35040.0f, 7200, COL_HOT,
        {
            { "Prep",        1500, 300,  ColorTier::Secondary },
            { "Chak Gerent", 1800, 2400, ColorTier::Primary },
            { "Outposts",    4200, 4500, ColorTier::Tertiary },
        }
    },
    { "Dragon's Stand",
        35709.0f, 37182.0f, 7200, COL_HOT,
        {
            { "Mordremoth Start",    5400, 1800, ColorTier::Primary },
            { "Mordremoth Progress", 0,    5400, ColorTier::Secondary },
        }
    },

    // ---------------------------------------------------------------------------
    // Living World Season 3
    // ---------------------------------------------------------------------------
    { "Lake Doric",
        45564.0f, 27004.0f, 7200, COL_LWS3,
        {
            {"Saidra's Haven",    0,    2700, ColorTier::Secondary},
            {"New Loamhurst",     2700, 2700, ColorTier::Primary},
            {"Noran's Homestead", 5400, 1800, ColorTier::Tertiary},
        }
    },

    // ---------------------------------------------------------------------------
    // Path of Fire
    // ---------------------------------------------------------------------------
    { "Crystal Oasis",
        58692.0f, 43752.0f, 7200, COL_POF,
        {
            { "Casino Rounds", 300,  900,  ColorTier::Secondary },
            { "Choya Pinata",  1200, 600,  ColorTier::Primary },
        }
    },
    { "Desert Highlands",
        59964.0f, 41384.0f, 7200, COL_POF,
        {
            { "Buried Treasure", 3600, 1200, ColorTier::Primary },
        }
    },
    { "Elon Riverlands",
        60715.0f, 45646.0f, 7200, COL_POF,
        {
            { "Path of Ascension", 5400, 1500, ColorTier::Secondary },
            { "Doppelganger",      6900, 1200, ColorTier::Primary },
        }
    },
    { "The Desolation",
        59943.0f, 50257.0f, 7200, COL_POF,
        {
            { "Junundu Rising",  1800, 1200, ColorTier::Primary, 2 },
            { "Maws of Torment", 3600, 1200, ColorTier::Secondary },
        }
    },
    { "Domain of Vabbi",
        66332.0f, 53596.0f, 7200, COL_POF,
        {
            { "Forged with Fire", 0,    1800, ColorTier::Primary, 2 },
            { "Serpents' Ire",    1800, 1800, ColorTier::Secondary },
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