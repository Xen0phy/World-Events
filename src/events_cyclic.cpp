#include "events.h"
#include "categories.h"

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
// DarkenColor scales the R, G, B channels by `factor` while keeping alpha.
// Use it to derive secondary/tertiary shades from a single seed color.
// ---------------------------------------------------------------------------
constexpr ColorSet COL_LWS2 { 0x715F18FF };
constexpr ColorSet COL_HOT  { 0x667118FF };
constexpr ColorSet COL_LWS3 { 0x52763BFF };
constexpr ColorSet COL_POF  { 0x976320FF };
constexpr ColorSet COL_LWS4 { 0x7E3494FF };
constexpr ColorSet COL_IBS  { 0x206697FF };
constexpr ColorSet COL_EOD  { 0x208B97FF };
constexpr ColorSet COL_SOTO { 0xD19C3CFF };
constexpr ColorSet COL_JW   { 0x18347EFF };
constexpr ColorSet COL_VOE  { 0xAB401AFF };

std::vector<CyclicGroup> g_CyclicGroups =
{
    // — each map has its own 2h cycle with sequential events.
    // Offsets are seconds from UTC midnight of each event's first occurrence.

    // ---------------------------------------------------------------------------
    // Instanced
    // ---------------------------------------------------------------------------
    { "Eye of the North",
        57563.0f, 21831.0f, m120, COL_IBS,
        {
            {"Twisted Marionette",      0, m20, ColorTier::Primary  , "[&BAkMAAA=]" },
            {"Battle for Lions Arch", m30, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"Dragonsorm",            m60, m20, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"Tower of Nightmres",    m90, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Living World Season 2
    // ---------------------------------------------------------------------------
    { "Dry Top",
        37129.0f, 32802.0f, m60, COL_LWS2,
        {
            {"Crash Site",  0, m40, ColorTier::Tertiary, "[&BIAHAAA=]" },
            {"Sandstorm", m40, m20, ColorTier::Primary,  "[&BIAHAAA=]" },
        }
    },
    
    // ---------------------------------------------------------------------------
    // Heart of Thorns
    // ---------------------------------------------------------------------------
    { "Verdant Brink",
        34944.0f, 31850.0f, m120, COL_HOT,
        {
            { "Night Bosses", m10, m20, ColorTier::Primary,   "[&BAgIAAA=]" },
            { "Securing Day", m30, m75, ColorTier::Tertiary,  "[&BAgIAAA=]" },
            { "Night Enemy",  m95, m25, ColorTier::Secondary, "[&BAgIAAA=]" },
        }
    },
    { "Auric Basin",
        34486.0f, 33919.0f, m120, COL_HOT,
        {
            { "Challenges", m45,m15,  ColorTier::Secondary, "[&BGwIAAA=]" },
            { "Octovine",   m60, m20, ColorTier::Primary,   "[&BAIIAAA=]" },
            { "Pylons",     m90, m75, ColorTier::Tertiary,  "[&BN0HAAA=]" },
        }
    },
    { "Tangled Depths",
        37372.0f, 35317.0f, m120, COL_HOT,
        {
            { "Prep",        m25, m5,  ColorTier::Secondary, "[&BPUHAAA=]" },
            { "Chak Gerent", m30, m40, ColorTier::Primary,   "[&BPUHAAA=]" },
            { "Outposts",    m70, m75, ColorTier::Tertiary,  "[&BAwIAAA=]" },
        }
    },
    { "Dragon's Stand",
        35722.0f, 37328.0f, m120, COL_HOT,
        {
            { "Mordremoth Start",    m90, m30, ColorTier::Primary,   "[&BIgIAAA=]" },
            { "Mordremoth Progress", 0,   m90, ColorTier::Secondary, "[&BIgIAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Living World Season 3
    // ---------------------------------------------------------------------------
    { "Lake Doric",
        45564.0f, 27004.0f, m120, COL_LWS3,
        {
            {"Noran's Homestead", m30,  m30, ColorTier::Primary,   "[&BK8JAAA=]" },
            {"Saidra's Haven",    m60,  m45, ColorTier::Secondary, "[&BK0JAAA=]" },
            {"New Loamhurst",     m105, m45, ColorTier::Tertiary,  "[&BLQJAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Path of Fire
    // ---------------------------------------------------------------------------
    { "Crystal Oasis",
        58692.0f, 43752.0f, m120, COL_POF,
        {
            { "Casino Rounds", m5, m15, ColorTier::Secondary, "[&BLsKAAA=]" },
            { "Choya Pinata",  m20,m10, ColorTier::Primary,   "[&BLsKAAA=]" },
        }
    },
    { "Desert Highlands",
        59964.0f, 41384.0f, m120, COL_POF,
        {
            { "Buried Treasure", m60, m20, ColorTier::Primary, "[&BGsKAAA=]" },
        }
    },
    { "Elon Riverlands",
        60715.0f, 45646.0f, m120, COL_POF,
        {
            { "The Path to Ascension", m90, m25, ColorTier::Secondary, "[&BFMKAAA=]" },
            { "Doppelganger",         m115, m20, ColorTier::Primary,   "[&BFMKAAA=]" },
        }
    },
    { "The Desolation",
        59943.0f, 50257.0f, m120, COL_POF,
        {
            { "Junundu Rising",  m30, m20, ColorTier::Primary,   "[&BMEKAAA=]", false, 2 },
            { "Maws of Torment", m60, m20, ColorTier::Secondary, "[&BKMKAAA=]" },
        }
    },
    { "Domain of Vabbi",
        66332.0f, 53596.0f, m120, COL_POF,
        {
            { "Forged with Fire", 0, m30, ColorTier::Primary,   "[&BO0KAAA=]", false, 2 },
            { "Serpents' Ire",  m30, m30, ColorTier::Secondary, "[&BHQKAAA=]" },
        }
    },
    
    // ---------------------------------------------------------------------------
    // Living World Season 4
    // ---------------------------------------------------------------------------
    { "Domain of Istan",
        57165.0f, 62605.0f, m120, COL_LWS4,
        {
            { "Palawadan", m105, m30, ColorTier::Primary, "[&BAkLAAA=]" },
        }
    },
    { "Jahai Bluffs",
        65135.0f, 57421.0f, m120, COL_LWS4,
        {
            { "Escorts",                 m60,m15, ColorTier::Secondary, "[&BIMLAAA=]" },
            { "Death-Branded Shatterer", m75,m15, ColorTier::Primary,   "[&BJMLAAA=]" },
        }
    },
    { "Thunderhead Peaks",
        57950.0f, 37800.0f, m120, COL_LWS4,
        {
            { "The Oil Floes",     m45, m15, ColorTier::Primary, "[&BKYLAAA=]" },
            { "Thunderhead Keep", m105, m20, ColorTier::Primary, "[&BLsLAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Icebrood Saga
    // ---------------------------------------------------------------------------
    { "Grothmar Valley",
        60957.0f, 19174.0f, m120, COL_IBS,
        {
            { "Effigy",              m10,     m15, ColorTier::Primary,   "[&BA4MAAA=]" },
            { "Doomlore Shrine", MIN(38), MIN(22), ColorTier::Secondary, "[&BA4MAAA=]" },
            { "Ooze Pits",           m65,     m20, ColorTier::Secondary, "[&BPgLAAA=]" },
            { "Doomlore Shrine",    m100,     m15, ColorTier::Secondary, "[&BPgLAAA=]" },
        }
    },
    { "Bjora Marches",
        57267.0f, 18383.0f, m120, COL_IBS,
        {
            { "Storms of Winter",      0,  m5, ColorTier::Primary,   "[&BCcMAAA=]" },
            { "Icebrood Champions",   m5, m15, ColorTier::Secondary, "[&BCcMAAA=]" },
            { "Drakkar",             m65, m35, ColorTier::Primary,   "[&BDkMAAA=]" },
            { "Defend Jora's Keep", m105, m15, ColorTier::Secondary, "[&BCcMAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // End of Dragons
    // ---------------------------------------------------------------------------
    { "Seitung Province",
        23247.0f, 102143.0f, m120, COL_EOD,
        {
            { "Aetherblade Assault", m90, m30, ColorTier::Primary, "[&BGUNAAA=]" },
        }
    },
    { "New Kaineng City",
        27975.0f, 99331.0f, m120, COL_EOD,
        {
            { "Kaineng Blackout", 0, m40, ColorTier::Primary, "[&BBkNAAA=]" },
        }
    },
    { "The Echovald Wilds",
        31118.0f, 102764.0f, m120, COL_EOD,
        {
            { "Gang War",          m30, m35, ColorTier::Primary,   "[&BMwMAAA=]" },
            { "Kaineng Blackout", m100, m20, ColorTier::Secondary, "[&BBkNAAA=]" },
        }
    },
    { "Dragon's End",
        34101.0f, 103128.0f, m120, COL_EOD,
        {
            { "Jade Maw",                 m5, MIN(8), ColorTier::Secondary, "[&BKIMAAA=]" },
            { "Jade Maw",                m45, MIN(8), ColorTier::Secondary, "[&BKIMAAA=]" },
            { "Battle for the Jade Sea", m60,    m60, ColorTier::Primary,   "[&BKIMAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Secrets of the Obscure
    // ---------------------------------------------------------------------------
    { "Skywatch Archipelago",
        26013.0f, 23715.0f, m120, COL_SOTO,
        {
            { "Unlocking the Wizard's Tower", m60, m25, ColorTier::Primary, "[&BL4NAAA=]" },
        }
    },
    { "Wizard's Tower",
        24444.0f, 22384.0f, m120, COL_SOTO,
        {
            { "Target Practice", m60, m55, ColorTier::Primary,   "[&BB8OAAA=]" },
            { "Fly by Night",   m100, m40, ColorTier::Secondary, "[&BB8OAAA=]" },
        }
    },
    { "Amnytas",
        24082.0f, 20290.0f, m120, COL_SOTO,
        {
            { "Defense of Amnytas", 0, m25, ColorTier::Primary, "[&BDQOAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Janthir Wilds
    // ---------------------------------------------------------------------------
    { "Janthir Syntri",
        39981.0f, 15269.0f, m120, COL_JW,
        {
            { "Of Mists and Monsters", m30,m15, ColorTier::Primary, "[&BCoPAAA=]" },
        }
    },
    { "Bava Nisos",
        36513.0f, 11571.0f, m120, COL_JW,
        {
            { "A Titanic Voyage", m80, m25, ColorTier::Primary, "[&BGEPAAA=]" },
        }
    },

    // ---------------------------------------------------------------------------
    // Visions of Eternity
    // ---------------------------------------------------------------------------
    { "Shipwreck Strand",
        10515.0f, 59212.0f, m120, COL_VOE,
        {
            { "Hammerhart Rumble", m40, m20, ColorTier::Primary, "[&BJEPAAA=]" },
        }
    },
    { "Starlit Weald",
        7310.0f, 58945.0f, m120, COL_VOE,
        {
            { "Secrets of the Weald", m100, m35, ColorTier::Primary, "[&BJ4PAAA=]" },
        }
    },
    { "Eternity's Garden",
        4566.0f, 61793.0f, m120, COL_VOE,
        {
            { "Shackles of the Ancients", m70, m25, ColorTier::Primary, "[&BPwPAAA=]" },
        }
    },
};

// Compiled-in default categories for the options-panel list, one per
// expansion-grouping comment above — see CategoryDefault in categories.h
// and the matching g_DefaultBasicCategories in events_basic.cpp. Built on
// first load (or whenever EVENTS_DATA_VERSION advances past what a user's
// saved file has), then fully user-editable from there.
std::vector<CategoryDefault> g_DefaultCyclicCategories =
{
    {"Living World", {
        {"Dry Top"},
        {"Lake Doric"},
        {"Domain of Istan"},
        {"Jahai Bluffs"},
        {"Thunderhead Peaks"},
    }},
    {"Heart of Thorns", {
        {"Verdant Brink"},
        {"Auric Basin"},
        {"Tangled Depths"},
        {"Dragon's Stand"},
    }},
    {"Path of Fire", {
        {"Crystal Oasis"},
        {"Desert Highlands"},
        {"Elon Riverlands"},
        {"The Desolation"},
        {"Domain of Vabbi"},
    }},
    {"Icebrood Saga", {
        {"Grothmar Valley"},
        {"Bjora Marches"},
        {"Eye of the North"},
    }},
    {"End of Dragons", {
        {"Seitung Province"},
        {"New Kaineng City"},
        {"The Echovald Wilds"},
        {"Dragon's End"},
    }},
    {"Secrets of the Obscure", {
        {"Skywatch Archipelago"},
        {"Wizard's Tower"},
        {"Amnytas"},
    }},
    {"Janthir Wilds", {
        {"Janthir Syntri"},
        {"Bava Nisos"},
    }},
    {"Visions of Eternity", {
        {"Shipwreck Strand"},
        {"Starlit Weald"},
        {"Eternity's Garden"},
    }},
};