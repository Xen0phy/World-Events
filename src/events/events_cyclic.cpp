//################################################################################
// events_cyclic.cpp
//--------------------------------------------------------------------------------
// g_CyclicGroups              compiled-in Cyclic Group roster (see
//                             CyclicGroup, events.h)
// g_DefaultCyclicCategories   compiled-in category defaults for the roster
//                             above (see CategoryDefault, events_categories.h)
//--------------------------------------------------------------------------------
// Hand-written data only - no logic. Groups are grouped by comment banner,
// one per expansion, matching g_DefaultCyclicCategories one-for-one below
// (Eye of the North is filed under Icebrood Saga there, not Instanced -
// see that list).
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_categories.h"
#include <optional>

//_ Each COL_* is the RGBA float tuple ColorSet::base actually stores (see
// events.h); the trailing hex comment is only the original color-picker
// value, kept for reference if a future edit wants to reason in hex.
const ColorSet COL_LWS2 { ImVec4(0.443f, 0.373f, 0.094f, 1.000f) }; //. 0x715F18FF
const ColorSet COL_HOT  { ImVec4(0.400f, 0.443f, 0.094f, 1.000f) }; //. 0x667118FF
const ColorSet COL_LWS3 { ImVec4(0.322f, 0.463f, 0.231f, 1.000f) }; //. 0x52763BFF
const ColorSet COL_POF  { ImVec4(0.592f, 0.388f, 0.125f, 1.000f) }; //. 0x976320FF
const ColorSet COL_LWS4 { ImVec4(0.494f, 0.204f, 0.580f, 1.000f) }; //. 0x7E3494FF
const ColorSet COL_IBS  { ImVec4(0.125f, 0.400f, 0.592f, 1.000f) }; //. 0x206697FF
const ColorSet COL_EOD  { ImVec4(0.125f, 0.545f, 0.592f, 1.000f) }; //. 0x208B97FF
const ColorSet COL_SOTO { ImVec4(0.820f, 0.612f, 0.235f, 1.000f) }; //. 0xD19C3CFF
const ColorSet COL_JW   { ImVec4(0.094f, 0.204f, 0.494f, 1.000f) }; //. 0x18347EFF
const ColorSet COL_VOE  { ImVec4(0.671f, 0.251f, 0.102f, 1.000f) }; //. 0xAB401AFF
const ColorSet COL_FEST { ImVec4(1.000f, 1.000f, 1.000f, 1.000f) }; //. 0xFFFFFFFF

//_ Each group is its own 2h cycle of sequential events; offsets are
// seconds from UTC midnight of each event's first occurrence.
std::vector<CyclicGroup> g_CyclicGroups =
{
    //_ Instanced
    { "Eye of the North",
        57563.0f, 21831.0f, m120, COL_IBS,
        {
            {"Twisted Marionette",      0, m20, ColorTier::Primary,   "[&BAkMAAA=]" },
            {"Battle for Lions Arch", m30, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"Dragonstorm",           m60, m20, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"Tower of Nightmares",   m90, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
        }
    },

    //_ Festivals
    { "Festival of the Four Winds",
        56040.0f, 39398.0f, m120, COL_FEST,
        {
            {"Skiff Race",       0, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"Water Balloons", m15, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"Treasure Hunt",  m30, m30, ColorTier::Primary, "[&BBwHAAA=]"},
            {"Skimmer Race",   m75, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"Fishing",        m90, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"Dolyak Race",   m105, m10, ColorTier::Primary, "[&BBwHAAA=]"},
        }, std::nullopt, false
    },

    //_ Living World Season 2
    { "Dry Top",
        37129.0f, 32802.0f, m60, COL_LWS2,
        {
            //_ Main Cycle
            {"Crash Site",         0,    m40,  ColorTier::Tertiary, "[&BIAHAAA=]" },
            {"Sandstorm",        m40,    m20,   ColorTier::Primary, "[&BIAHAAA=]" },
    
            //_ Crash Site detail - Group A: fires at 0/15/30 within the 40-min
            // Crash Site window.
            {"Tendril A",          0,     m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Race",               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Moa",                0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Crash Victims",      0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Shaman",             0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Tendril B",          0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Skritt Supplies",    0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"Escort Rustbucket",  0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
    
            //_ Crash Site detail - Group B: fires at 5/20/35.
            {"Frog",               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Queen",              0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Serene",             0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"South Mine",         0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Inquest Leader",     0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Light Golem",        0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Vine Bridge",        0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"Collect Beetles",    0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
    
            //_ Crash Site detail - Group C: fires at 10/25 only (40 is
            // Sandstorm, so no third occurrence).
            {"Basket",             0,     m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"Town",               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"North Mine",         0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"Ley Line Hub",       0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"Inquest Suit",       0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1,                               {}, true,     {m10, m25} },
    
            //_ Sandstorm detail - shared between the m40 and m50 subphase.
            {"Mite farm",          0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"Haze",               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"North Mine block",   0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"Stop Skritt (1)",    0,     m5, ColorTier::Secondary, "[&BIcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"Protect Eway",       0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"Giant",              0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"Stop Skritt (2)",    0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
    
            //_ Sandstorm detail - single-occurrence, no isVarying needed.
            {"Devourer Queen",   m45,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Rare creature",    m45,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Small Dust",       m50,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Skritt Queen",     m50,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Chickenado",       m50,     m5, ColorTier::Secondary, "[&BIgHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Big Dust",         m55, MIN(2), ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"Giant Beetle", MIN(57), MIN(3), ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
        }
    },
    
    //_ Heart of Thorns
    { "Verdant Brink",
        34944.0f, 31850.0f, m120, COL_HOT,
        {
            { "Night Bosses", m10, m20, ColorTier::Primary,   "[&BAgIAAA=]" },
            { "Securing Day", m30, m75, ColorTier::Tertiary,  "[&BAgIAAA=]" },
            { "Night Enemy",  m95, m25, ColorTier::Secondary, "[&BAgIAAA=]" },
        },
        std::nullopt, true, "verdant_brink_heros_choice_chest"
    },
    { "Auric Basin",
        34486.0f, 33919.0f, m120, COL_HOT,
        {
            { "Challenges", m45,m15,  ColorTier::Secondary, "[&BGwIAAA=]" },
            { "Octovine",   m60, m20, ColorTier::Primary,   "[&BAIIAAA=]" },
            { "Pylons",     m90, m75, ColorTier::Tertiary,  "[&BN0HAAA=]" },
        },
        std::nullopt, true, "auric_basin_heros_choice_chest"
    },
    { "Tangled Depths",
        37372.0f, 35317.0f, m120, COL_HOT,
        {
            { "Prep",        m25, m5,  ColorTier::Secondary, "[&BPUHAAA=]" },
            { "Chak Gerent", m30, m15, ColorTier::Primary,   "[&BPUHAAA=]" },
            { "Outposts",    m70, m75, ColorTier::Tertiary,  "[&BAwIAAA=]" },
        },
        std::nullopt, true, "tangled_depths_heros_choice_chest"
    },
    { "Dragon's Stand",
        35722.0f, 37328.0f, m120, COL_HOT,
        {
            { "Mordremoth Start",    m90, m30, ColorTier::Primary,   "[&BIgIAAA=]" },
            { "Mordremoth Progress", 0,   m90, ColorTier::Secondary, "[&BIgIAAA=]" },
        },
        std::nullopt, true, "dragons_stand_heros_choice_chest"
    },

    //_ Living World Season 3
    { "Lake Doric",
        45564.0f, 27004.0f, m120, COL_LWS3,
        {
            {"Noran's Homestead", m30,  m30, ColorTier::Primary,   "[&BK8JAAA=]" },
            {"Saidra's Haven",    m60,  m45, ColorTier::Secondary, "[&BK0JAAA=]" },
            {"New Loamhurst",     m105, m45, ColorTier::Tertiary,  "[&BLQJAAA=]" },
        }
    },

    //_ Path of Fire
    { "Crystal Oasis",
        58692.0f, 43752.0f, m120, COL_POF,
        {
            { "Casino Rounds", m5, m15, ColorTier::Secondary, "[&BLsKAAA=]" },
            { "Choya Pinata",  m20,m10, ColorTier::Primary,   "[&BLsKAAA=]" },
        },
        std::nullopt, true, "crystal_oasis_heros_choice_chest"
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
        },
        std::nullopt, true, "elon_riverlands_heros_choice_chest"
    },
    { "The Desolation",
        59943.0f, 50257.0f, m120, COL_POF,
        {
            { "Junundu Rising",  m30, m20, ColorTier::Primary,   "[&BMEKAAA=]", true, 2 },
            { "Maws of Torment", m60, m20, ColorTier::Secondary, "[&BKMKAAA=]" },
        },
        std::nullopt, true, "the_desolation_heros_choice_chest"
    },
    { "Domain of Vabbi",
        66332.0f, 53596.0f, m120, COL_POF,
        {
            { "Forged with Fire", 0, m30, ColorTier::Primary,   "[&BO0KAAA=]", true, 2 },
            { "Serpents' Ire",  m30, m30, ColorTier::Secondary, "[&BHQKAAA=]" },
        },
        std::nullopt, true, "domain_of_vabbi_heros_choice_chest"
    },
    
    //_ Living World Season 4
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

    //_ Icebrood Saga
    { "Grothmar Valley",
        60957.0f, 19174.0f, m120, COL_IBS,
        {
            { "Effigy",              m10,     m15, ColorTier::Primary,   "[&BA4MAAA=]" },
            { "Doomlore Shrine", MIN(38), MIN(22), ColorTier::Secondary, "[&BA4MAAA=]" },
            { "Ooze Pits",           m65,     m20, ColorTier::Secondary, "[&BPgLAAA=]" },
            { "Metal Concert",      m100,     m15, ColorTier::Secondary, "[&BPgLAAA=]" },
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

    //_ End of Dragons
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
            { "Jade Maw",                  0,  MIN(8), ColorTier::Secondary, "[&BKIMAAA=]", true, 1, {}, true, {m5, m45} },
            { "Battle for the Jade Sea", m60,     m60, ColorTier::Primary,   "[&BKIMAAA=]" },
        }
    },

    //_ Secrets of the Obscure
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

    //_ Janthir Wilds
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

    //_ Visions of Eternity
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

//_ Same idea as g_DefaultBasicCategories (events_basic.cpp) - one entry
// per expansion banner above, for the Cyclic Group list instead.
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
    {"Festivals", {
        {"Festival of the Four Winds"},
    }},
};

//_ One-time-pushed Slot corrections; see SlotOverride (events.h) for what
// this does and why EVENTS_DATA_VERSION must be bumped alongside any
// entry added here.
std::vector<SlotOverride> g_SlotOverrides =
{
    //_ Chak Gerent's duration was wrong in files saved before this fix;
    // the roster above already has the correct m15, this pushes it onto
    // whatever a pre-existing local file has stored.
    {"Tangled Depths", "Chak Gerent", std::nullopt, m15},
};