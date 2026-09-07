//################################################################################
// events_cyclic.cpp
//--------------------------------------------------------------------------------
// g_CyclicGroups              compiled-in Cyclic Group roster (see
//                             CyclicGroup, events.h)
// g_DefaultCyclicCategories   compiled-in category defaults for the roster
//                             above (see CategoryDefault, events_categories.h)
//--------------------------------------------------------------------------------
// Hand-written data only - no logic. Groups are grouped by comment banner, one
// per expansion, matching g_DefaultCyclicCategories one-for-one below (Eye of the
// North is filed under Icebrood Saga there, not Instanced - see that list).
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_categories.h"
#include <optional>

//_ RGBA tuple ColorSet::base stores; trailing hex is the original picker value.
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

//_ Each group is a 2h cycle of events; offsets are seconds from UTC midnight.
std::vector<CyclicGroup> g_CyclicGroups =
{
    //_ Instanced
    { "eye_of_the_north", "Eye of the North",
        57563.0f, 21831.0f, m120, COL_IBS,
        {
            {"twisted_marionette", "Twisted Marionette",            0, m20, ColorTier::Primary,   "[&BAkMAAA=]" },
            {"battle_for_lions_arch", "Battle for Lions Arch", m30, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"dragonstorm", "Dragonstorm",                               m60, m20, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"tower_of_nightmares", "Tower of Nightmares",       m90, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
        }
    },

    //_ Festivals
    { "festival_of_the_four_winds", "Festival of the Four Winds",
        56040.0f, 39398.0f, m120, COL_FEST,
        {
            {"skiff_race", "Skiff Race",               0, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"water_balloons", "Water Balloons", m15, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"treasure_hunt", "Treasure Hunt",    m30, m30, ColorTier::Primary, "[&BBwHAAA=]"},
            {"skimmer_race", "Skimmer Race",       m75, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"fishing", "Fishing",                      m90, m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"dolyak_race", "Dolyak Race",         m105, m10, ColorTier::Primary, "[&BBwHAAA=]"},
        }, std::nullopt, false
    },

    //_ Living World Season 2
    { "dry_top", "Dry Top",
        37129.0f, 32802.0f, m60, COL_LWS2,
        {
            //_ Main Cycle
            {"crash_site", "Crash Site",         0,    m40,  ColorTier::Tertiary, "[&BIAHAAA=]" },
            {"sandstorm", "Sandstorm",          m40,    m20,   ColorTier::Primary, "[&BIAHAAA=]" },
    
            //_ Crash Site Group A: fires at 0/15/30 within the 40-min window.
            {"tendril_a", "Tendril A",                          0,     m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"race", "Race",                                         0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"moa", "Moa",                                            0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"crash_victims", "Crash Victims",              0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"shaman", "Shaman",                                   0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"tendril_b", "Tendril B",                          0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"skritt_supplies", "Skritt Supplies",        0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"escort_rustbucket", "Escort Rustbucket",  0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
    
            //_ Crash Site detail - Group B: fires at 5/20/35.
            {"frog", "Frog",                                     0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"queen", "Queen",                                  0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"serene", "Serene",                               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"south_mine", "South Mine",                   0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"inquest_leader", "Inquest Leader",       0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"light_golem", "Light Golem",                0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"vine_bridge", "Vine Bridge",                0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"collect_beetles", "Collect Beetles",    0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
    
            //_ Crash Site Group C: fires at 10/25 only (40 is Sandstorm).
            {"basket", "Basket",                         0,     m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"town", "Town",                               0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"north_mine", "North Mine",             0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"ley_line_hub", "Ley Line Hub",       0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1,                               {}, true,     {m10, m25} },
            {"inquest_suit", "Inquest Suit",       0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1,                               {}, true,     {m10, m25} },
    
            //_ Sandstorm detail - shared between the m40 and m50 subphase.
            {"mite_farm", "Mite farm",                        0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"haze", "Haze",                                       0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"north_mine_block", "North Mine block",   0,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"stop_skritt_1", "Stop Skritt (1)",        0,     m5, ColorTier::Secondary, "[&BIcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"protect_eway", "Protect Eway",               0,     m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"giant", "Giant",                                    0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"stop_skritt_2", "Stop Skritt (2)",        0,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
    
            //_ Sandstorm detail - single-occurrence, no isVarying needed.
            {"devourer_queen", "Devourer Queen",   m45,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"rare_creature", "Rare creature",      m45,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"small_dust", "Small Dust",               m50,     m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"skritt_queen", "Skritt Queen",         m50,     m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"chickenado", "Chickenado",               m50,     m5, ColorTier::Secondary, "[&BIgHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"big_dust", "Big Dust",                     m55, MIN(2), ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"giant_beetle", "Giant Beetle",     MIN(57), MIN(3), ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
        }
    },
    
    //_ Heart of Thorns
    { "verdant_brink", "Verdant Brink",
        34944.0f, 31850.0f, m120, COL_HOT,
        {
            { "night_bosses", "Night Bosses", m10, m20, ColorTier::Primary,   "[&BAgIAAA=]" },
            { "securing_day", "Securing Day", m30, m75, ColorTier::Tertiary,  "[&BAgIAAA=]" },
            { "night_enemy", "Night Enemy", m105, m25, ColorTier::Secondary, "[&BAgIAAA=]" },
        },
        std::nullopt, true, "verdant_brink_heros_choice_chest"
    },
    { "auric_basin", "Auric Basin",
        34486.0f, 33919.0f, m120, COL_HOT,
        {
            { "challenges", "Challenges", m45, m15, ColorTier::Secondary, "[&BGwIAAA=]" },
            { "octovine", "Octovine",   m60, m20, ColorTier::Primary,   "[&BAIIAAA=]" },
            { "pylons", "Pylons",     m90, m75, ColorTier::Tertiary,  "[&BN0HAAA=]" },
        },
        std::nullopt, true, "auric_basin_heros_choice_chest"
    },
    { "tangled_depths", "Tangled Depths",
        37372.0f, 35317.0f, m120, COL_HOT,
        {
            { "prep", "Prep",        m25, m5,  ColorTier::Secondary, "[&BPUHAAA=]" },
            { "chak_gerent", "Chak Gerent", m30, m20, ColorTier::Primary,   "[&BPUHAAA=]" },
            { "outposts", "Outposts",    m50, m95, ColorTier::Tertiary,  "[&BAwIAAA=]" },
        },
        std::nullopt, true, "tangled_depths_heros_choice_chest"
    },
    { "dragons_stand", "Dragon's Stand",
        35722.0f, 37328.0f, m120, COL_HOT,
        {
            { "mordremoth_start", "Mordremoth Start",    m90, m30, ColorTier::Primary,   "[&BIgIAAA=]" },
            { "mordremoth_progress", "Mordremoth Progress", 0,   m90, ColorTier::Secondary, "[&BIgIAAA=]" },
        },
        std::nullopt, true, "dragons_stand_heros_choice_chest"
    },

    //_ Living World Season 3
    { "lake_doric", "Lake Doric",
        45564.0f, 27004.0f, m120, COL_LWS3,
        {
            {"norans_homestead", "Noran's Homestead", m30,  m30, ColorTier::Primary,   "[&BK8JAAA=]" },
            {"saidras_haven", "Saidra's Haven",          m60,  m45, ColorTier::Secondary, "[&BK0JAAA=]" },
            {"new_loamhurst", "New Loamhurst",            m105, m45, ColorTier::Tertiary,  "[&BLQJAAA=]" },
        }
    },

    //_ Path of Fire
    { "crystal_oasis", "Crystal Oasis",
        58692.0f, 43752.0f, m120, COL_POF,
        {
            { "casino_rounds", "Casino Rounds", m5, m15, ColorTier::Secondary, "[&BLsKAAA=]" },
            { "choya_pinata", "Choya Pinata",  m20,m10, ColorTier::Primary,   "[&BLsKAAA=]" },
        },
        std::nullopt, true, "crystal_oasis_heros_choice_chest"
    },
    { "desert_highlands", "Desert Highlands",
        59964.0f, 41384.0f, m120, COL_POF,
        {
            { "buried_treasure", "Buried Treasure", m60, m20, ColorTier::Primary, "[&BGsKAAA=]" },
        }
    },
    { "elon_riverlands", "Elon Riverlands",
        60715.0f, 45646.0f, m120, COL_POF,
        {
            { "the_path_to_ascension", "The Path to Ascension", m90, m25, ColorTier::Secondary, "[&BFMKAAA=]" },
            { "doppelganger", "Doppelganger",         m115, m20, ColorTier::Primary,   "[&BFMKAAA=]" },
        },
        std::nullopt, true, "elon_riverlands_heros_choice_chest"
    },
    { "the_desolation", "The Desolation",
        59943.0f, 50257.0f, m120, COL_POF,
        {
            { "junundu_rising", "Junundu Rising",  m30, m20, ColorTier::Primary,   "[&BMEKAAA=]", true, 2 },
            { "maws_of_torment", "Maws of Torment", m60, m20, ColorTier::Secondary, "[&BKMKAAA=]" },
        },
        std::nullopt, true, "the_desolation_heros_choice_chest"
    },
    { "domain_of_vabbi", "Domain of Vabbi",
        66332.0f, 53596.0f, m120, COL_POF,
        {
            { "forged_with_fire", "Forged with Fire", 0, m30, ColorTier::Primary,   "[&BO0KAAA=]", true, 2 },
            { "serpents_ire", "Serpents' Ire",  m30, m30, ColorTier::Secondary, "[&BHQKAAA=]" },
        },
        std::nullopt, true, "domain_of_vabbi_heros_choice_chest"
    },
    
    //_ Living World Season 4
    { "domain_of_istan", "Domain of Istan",
        57165.0f, 62605.0f, m120, COL_LWS4,
        {
            { "palawadan", "Palawadan", m105, m30, ColorTier::Primary, "[&BAkLAAA=]" },
        }
    },
    { "jahai_bluffs", "Jahai Bluffs",
        65135.0f, 57421.0f, m120, COL_LWS4,
        {
            { "escorts", "Escorts",                 m60,m15, ColorTier::Secondary, "[&BIMLAAA=]" },
            { "death_branded_shatterer", "Death-Branded Shatterer", m75,m15, ColorTier::Primary,   "[&BJMLAAA=]" },
        }
    },
    { "thunderhead_peaks", "Thunderhead Peaks",
        57950.0f, 37800.0f, m120, COL_LWS4,
        {
            { "the_oil_floes", "The Oil Floes",     m45, m15, ColorTier::Primary, "[&BKYLAAA=]" },
            { "thunderhead_keep", "Thunderhead Keep", m105, m20, ColorTier::Primary, "[&BLsLAAA=]" },
        }
    },

    //_ Icebrood Saga
    { "grothmar_valley", "Grothmar Valley",
        60957.0f, 19174.0f, m120, COL_IBS,
        {
            { "effigy", "Effigy",              m10,     m15, ColorTier::Primary,   "[&BA4MAAA=]" },
            { "doomlore_shrine", "Doomlore Shrine", MIN(38), MIN(22), ColorTier::Secondary, "[&BA4MAAA=]" },
            { "ooze_pits", "Ooze Pits",           m65,     m20, ColorTier::Secondary, "[&BPgLAAA=]" },
            { "metal_concert", "Metal Concert",      m100,     m15, ColorTier::Secondary, "[&BPgLAAA=]" },
        }
    },
    { "bjora_marches", "Bjora Marches",
        57267.0f, 18383.0f, m120, COL_IBS,
        {
            { "storms_of_winter", "Storms of Winter",      0,  m5, ColorTier::Primary,   "[&BCcMAAA=]" },
            { "icebrood_champions", "Icebrood Champions",   m5, m15, ColorTier::Secondary, "[&BCcMAAA=]" },
            { "drakkar", "Drakkar",             m65, m35, ColorTier::Primary,   "[&BDkMAAA=]" },
            { "defend_joras_keep", "Defend Jora's Keep", m105, m15, ColorTier::Secondary, "[&BCcMAAA=]" },
        }
    },

    //_ End of Dragons
    { "seitung_province", "Seitung Province",
        23247.0f, 102143.0f, m120, COL_EOD,
        {
            { "aetherblade_assault", "Aetherblade Assault", m90, m30, ColorTier::Primary, "[&BGUNAAA=]" },
        }
    },
    { "new_kaineng_city", "New Kaineng City",
        27975.0f, 99331.0f, m120, COL_EOD,
        {
            { "kaineng_blackout", "Kaineng Blackout", 0, m40, ColorTier::Primary, "[&BBkNAAA=]" },
        }
    },
    { "the_echovald_wilds", "The Echovald Wilds",
        31118.0f, 102764.0f, m120, COL_EOD,
        {
            { "gang_war", "Gang War",          m30, m35, ColorTier::Primary,   "[&BMwMAAA=]" },
            { "kaineng_blackout", "Kaineng Blackout", m100, m20, ColorTier::Secondary, "[&BBkNAAA=]" },
        }
    },
    { "dragons_end", "Dragon's End",
        34101.0f, 103128.0f, m120, COL_EOD,
        {
            { "jade_maw", "Jade Maw",                  0,  MIN(8), ColorTier::Secondary, "[&BKIMAAA=]", true, 1, {}, true, {m5, m45} },
            { "battle_for_the_jade_sea", "Battle for the Jade Sea", m60,     m60, ColorTier::Primary,   "[&BKIMAAA=]" },
        }
    },

    //_ Secrets of the Obscure
    { "skywatch_archipelago", "Skywatch Archipelago",
        26013.0f, 23715.0f, m120, COL_SOTO,
        {
            { "unlocking_the_wizards_tower", "Unlocking the Wizard's Tower", m60, m25, ColorTier::Primary, "[&BL4NAAA=]" },
        }
    },
    { "wizards_tower", "Wizard's Tower",
        24444.0f, 22384.0f, m120, COL_SOTO,
        {
            { "target_practice", "Target Practice", m60, m55, ColorTier::Primary,   "[&BB8OAAA=]" },
            { "fly_by_night", "Fly by Night",   m100, m40, ColorTier::Secondary, "[&BB8OAAA=]" },
        }
    },
    { "amnytas", "Amnytas",
        24082.0f, 20290.0f, m120, COL_SOTO,
        {
            { "defense_of_amnytas", "Defense of Amnytas", 0, m25, ColorTier::Primary, "[&BDQOAAA=]" },
        }
    },

    //_ Janthir Wilds
    { "janthir_syntri", "Janthir Syntri",
        39981.0f, 15269.0f, m120, COL_JW,
        {
            { "of_mists_and_monsters", "Of Mists and Monsters", m30,m25, ColorTier::Primary, "[&BCoPAAA=]" },
        }
    },
    { "bava_nisos", "Bava Nisos",
        36513.0f, 11571.0f, m120, COL_JW,
        {
            { "a_titanic_voyage", "A Titanic Voyage", m80, m25, ColorTier::Primary, "[&BGEPAAA=]" },
        }
    },

    //_ Visions of Eternity
    { "shipwreck_strand", "Shipwreck Strand",
        10515.0f, 59212.0f, m120, COL_VOE,
        {
            { "hammerhart_rumble", "Hammerhart Rumble", m40, m20, ColorTier::Primary, "[&BJEPAAA=]" },
        }
    },
    { "starlit_weald", "Starlit Weald",
        7310.0f, 58945.0f, m120, COL_VOE,
        {
            { "secrets_of_the_weald", "Secrets of the Weald", m100, m35, ColorTier::Primary, "[&BJ4PAAA=]" },
        }
    },
    { "eternitys_garden", "Eternity's Garden",
        4566.0f, 61793.0f, m120, COL_VOE,
        {
            { "shackles_of_the_ancients", "Shackles of the Ancients", m70, m25, ColorTier::Primary, "[&BPwPAAA=]" },
        }
    },
};

//_ Same idea as g_DefaultBasicCategories (events_basic.cpp), per banner.
std::vector<CategoryDefault> g_DefaultCyclicCategories =
{
    {"Living World", {
        {"dry_top"},
        {"lake_doric"},
        {"domain_of_istan"},
        {"jahai_bluffs"},
        {"thunderhead_peaks"},
    }},
    {"Heart of Thorns", {
        {"verdant_brink"},
        {"auric_basin"},
        {"tangled_depths"},
        {"dragons_stand"},
    }},
    {"Path of Fire", {
        {"crystal_oasis"},
        {"desert_highlands"},
        {"elon_riverlands"},
        {"the_desolation"},
        {"domain_of_vabbi"},
    }},
    {"Icebrood Saga", {
        {"grothmar_valley"},
        {"bjora_marches"},
        {"eye_of_the_north"},
    }},
    {"End of Dragons", {
        {"seitung_province"},
        {"new_kaineng_city"},
        {"the_echovald_wilds"},
        {"dragons_end"},
    }},
    {"Secrets of the Obscure", {
        {"skywatch_archipelago"},
        {"wizards_tower"},
        {"amnytas"},
    }},
    {"Janthir Wilds", {
        {"janthir_syntri"},
        {"bava_nisos"},
    }},
    {"Visions of Eternity", {
        {"shipwreck_strand"},
        {"starlit_weald"},
        {"eternitys_garden"},
    }},
    {"Festivals", {
        {"festival_of_the_four_winds"},
    }},
};

//_ One-time Slot corrections; see SlotOverride (events.h), EVENTS_DATA_VERSION.
std::vector<SlotOverride> g_SlotOverrides =
{
    {"verdant_brink",   "night_enemy",  m105, std::nullopt},
    {"tangled_depths",  "chak_gerent",  m30, m20},
    {"tangled_depths",  "outposts",     m50, m95},
    {"janthir_syntri",  "of_mists_and_monsters", m30,m25},
};