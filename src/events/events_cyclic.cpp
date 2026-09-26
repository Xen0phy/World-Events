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
    { "eye_of_the_north",
        57563.0f, 21831.0f, m120, COL_IBS,
        {
            {"twisted_marionette",    0,   m20, ColorTier::Primary,   "[&BAkMAAA=]" },
            {"battle_for_lions_arch", m30, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"dragonstorm",           m60, m20, ColorTier::Secondary, "[&BAkMAAA=]" },
            {"tower_of_nightmares",   m90, m15, ColorTier::Secondary, "[&BAkMAAA=]" },
        }
    },

    //_ Festivals
    { "festival_of_the_four_winds",
        56040.0f, 39398.0f, m120, COL_FEST,
        {
            {"skiff_race",     0,    m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"water_balloons", m15,  m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"treasure_hunt",  m30,  m30, ColorTier::Primary, "[&BBwHAAA=]"},
            {"skimmer_slalom",   m75,  m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"fishing",        m90,  m10, ColorTier::Primary, "[&BBwHAAA=]"},
            {"dolyak_race",    m105, m10, ColorTier::Primary, "[&BBwHAAA=]"},
        }, std::nullopt, false
    },

    //_ Living World Season 2
    { "dry_top",
        37129.0f, 32802.0f, m60, COL_LWS2,
        {
            //_ Main Cycle
            {"crash_site", 0,   m40, ColorTier::Tertiary, "[&BIAHAAA=]" },
            {"sandstorm",  m40, m20, ColorTier::Primary,  "[&BIAHAAA=]" },
    
            //_ Crash Site Group A: fires at 0/15/30 within the 40-min window.
            {"tendril_a",         0, m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"race",              0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"moa",               0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"crash_victims",     0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"shaman",            0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"tendril_b",         0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"skritt_supplies",   0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
            {"escort_rustbucket", 0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.67f)}, true, {0, m15, m30} },
    
            //_ Crash Site detail - Group B: fires at 5/20/35.
            {"frog",            0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"queen",           0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"serene",          0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"south_mine",      0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"inquest_leader",  0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"light_golem",     0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"vine_bridge",     0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
            {"collect_beetles", 0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.73f)}, true, {m5, m20, m35} },
    
            //_ Crash Site Group C: fires at 10/25 only (40 is Sandstorm).
            {"basket",       0, m5, ColorTier::Secondary, "[&BIAHAAA=]", false, 1, {}, true, {m10, m25} },
            {"town",         0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {}, true, {m10, m25} },
            {"north_mine",   0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {}, true, {m10, m25} },
            {"ley_line_hub", 0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {}, true, {m10, m25} },
            {"inquest_suit", 0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {}, true, {m10, m25} },
    
            //_ Sandstorm detail - shared between the m40 and m50 subphase.
            {"mite_farm",        0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"haze",             0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"north_mine_block", 0, m5, ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"stop_skritt_1",    0, m5, ColorTier::Secondary, "[&BIcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"protect_eway",     0, m5, ColorTier::Secondary, "[&BJcHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"giant",            0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
            {"stop_skritt_2",    0, m5, ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.87f)}, true, {m40, MIN(50)} },
    
            //_ Sandstorm detail - single-occurrence, no isVarying needed.
            {"devourer_queen", m45,             m5,             ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"rare_creature",  m45,             m5,             ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"small_dust",     m50,             m5,             ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"skritt_queen",   m50,             m5,             ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"chickenado",     m50,             m5,             ColorTier::Secondary, "[&BIgHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"big_dust",       m55,             MIN(2), ColorTier::Secondary, "[&BHoHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
            {"giant_beetle",   MIN(57), MIN(3), ColorTier::Secondary, "[&BIYHAAA=]", false, 1, {ShadeU32(COL_LWS2.base, 0.93f)} },
        }
    },
    
    //_ Heart of Thorns
    { "verdant_brink",
        34944.0f, 31850.0f, m120, COL_HOT,
        {
            { "night_bosses", m10,  m20, ColorTier::Primary,   "[&BAgIAAA=]" },
            { "securing_day", m30,  m75, ColorTier::Tertiary,  "[&BAgIAAA=]" },
            { "night_enemy",  m105, m25, ColorTier::Secondary, "[&BAgIAAA=]" },
        },
        std::nullopt, true, "verdant_brink_heros_choice_chest"
    },
    { "auric_basin",
        34486.0f, 33919.0f, m120, COL_HOT,
        {
            { "challenges", m45, m15, ColorTier::Secondary, "[&BGwIAAA=]" },
            { "octovine",   m60, m20, ColorTier::Primary,   "[&BAIIAAA=]" },
            { "pylons",     m90, m75, ColorTier::Tertiary,  "[&BN0HAAA=]" },
        },
        std::nullopt, true, "auric_basin_heros_choice_chest"
    },
    { "tangled_depths",
        37372.0f, 35317.0f, m120, COL_HOT,
        {
            { "prep",        m25, m5,  ColorTier::Secondary, "[&BPUHAAA=]" },
            { "chak_gerent", m30, m20, ColorTier::Primary,   "[&BPUHAAA=]" },
            { "outposts",    m50, m95, ColorTier::Tertiary,  "[&BAwIAAA=]" },
        },
        std::nullopt, true, "tangled_depths_heros_choice_chest"
    },
    { "dragons_stand",
        35722.0f, 37328.0f, m120, COL_HOT,
        {
            { "mordremoth_start",    m90, m30, ColorTier::Primary,   "[&BIgIAAA=]" },
            { "mordremoth_progress", 0,   m90, ColorTier::Secondary, "[&BIgIAAA=]" },
        },
        std::nullopt, true, "dragons_stand_heros_choice_chest"
    },

    //_ Living World Season 3
    { "lake_doric",
        45564.0f, 27004.0f, m120, COL_LWS3,
        {
            {"norans_homestead", m30,  m30, ColorTier::Primary,   "[&BK8JAAA=]" },
            {"saidras_haven",    m60,  m45, ColorTier::Secondary, "[&BK0JAAA=]" },
            {"new_loamhurst",    m105, m45, ColorTier::Tertiary,  "[&BLQJAAA=]" },
        }
    },

    //_ Path of Fire
    { "crystal_oasis",
        58692.0f, 43752.0f, m120, COL_POF,
        {
            { "casino_rounds", m5, m15, ColorTier::Secondary, "[&BLsKAAA=]" },
            { "choya_pinata",  m20,m10, ColorTier::Primary,   "[&BLsKAAA=]" },
        },
        std::nullopt, true, "crystal_oasis_heros_choice_chest"
    },
    { "desert_highlands",
        59964.0f, 41384.0f, m120, COL_POF,
        {
            { "buried_treasure", m60, m20, ColorTier::Primary, "[&BGsKAAA=]" },
        }
    },
    { "elon_riverlands",
        60715.0f, 45646.0f, m120, COL_POF,
        {
            { "the_path_to_ascension", m90,  m25, ColorTier::Secondary, "[&BFMKAAA=]" },
            { "doppelganger",          m115, m20, ColorTier::Primary,   "[&BFMKAAA=]" },
        },
        std::nullopt, true, "elon_riverlands_heros_choice_chest"
    },
    { "the_desolation",
        59943.0f, 50257.0f, m120, COL_POF,
        {
            { "junundu_rising",  m30, m20, ColorTier::Primary,   "[&BMEKAAA=]", true, 2 },
            { "maws_of_torment", m60, m20, ColorTier::Secondary, "[&BKMKAAA=]" },
        },
        std::nullopt, true, "the_desolation_heros_choice_chest"
    },
    { "domain_of_vabbi",
        66332.0f, 53596.0f, m120, COL_POF,
        {
            { "forged_with_fire", 0,   m30, ColorTier::Primary,   "[&BO0KAAA=]", true, 2 },
            { "serpents_ire",     m30, m30, ColorTier::Secondary, "[&BHQKAAA=]" },
        },
        std::nullopt, true, "domain_of_vabbi_heros_choice_chest"
    },
    
    //_ Living World Season 4
    { "domain_of_istan",
        57165.0f, 62605.0f, m120, COL_LWS4,
        {
            { "palawadan", m105, m30, ColorTier::Primary, "[&BAkLAAA=]" },
        }
    },
    { "jahai_bluffs",
        65135.0f, 57421.0f, m120, COL_LWS4,
        {
            { "escorts",                 m60,m15, ColorTier::Secondary, "[&BIMLAAA=]" },
            { "death_branded_shatterer", m75,m15, ColorTier::Primary,   "[&BJMLAAA=]" },
        }
    },
    { "thunderhead_peaks",
        57950.0f, 37800.0f, m120, COL_LWS4,
        {
            { "the_oil_floes",     m45, m15, ColorTier::Primary, "[&BKYLAAA=]" },
            { "thunderhead_keep", m105, m20, ColorTier::Primary, "[&BLsLAAA=]" },
        }
    },

    //_ Icebrood Saga
    { "grothmar_valley",
        60957.0f, 19174.0f, m120, COL_IBS,
        {
            { "effigy",          m10,             m15, ColorTier::Primary,   "[&BA4MAAA=]" },
            { "doomlore_shrine", MIN(38), MIN(22), ColorTier::Secondary, "[&BA4MAAA=]" },
            { "ooze_pits",       m65,             m20, ColorTier::Secondary, "[&BPgLAAA=]" },
            { "metal_concert",   m100,            m15, ColorTier::Secondary, "[&BPgLAAA=]" },
        }
    },
    { "bjora_marches",
        57267.0f, 18383.0f, m120, COL_IBS,
        {
            { "storms_of_winter",   0,    m5,  ColorTier::Primary,   "[&BCcMAAA=]" },
            { "icebrood_champions", m5,   m15, ColorTier::Secondary, "[&BCcMAAA=]" },
            { "drakkar",            m65,  m35, ColorTier::Primary,   "[&BDkMAAA=]" },
            { "defend_joras_keep",  m105, m15, ColorTier::Secondary, "[&BCcMAAA=]" },
        }
    },

    //_ End of Dragons
    { "seitung_province",
        23247.0f, 102143.0f, m120, COL_EOD,
        {
            { "aetherblade_assault", m90, m30, ColorTier::Primary, "[&BGUNAAA=]" },
        }
    },
    { "new_kaineng_city",
        27975.0f, 99331.0f, m120, COL_EOD,
        {
            { "kaineng_blackout", 0, m40, ColorTier::Primary, "[&BBkNAAA=]" },
        }
    },
    { "the_echovald_wilds",
        31118.0f, 102764.0f, m120, COL_EOD,
        {
            { "gang_war",         m30,  m35, ColorTier::Primary,   "[&BMwMAAA=]" },
            { "kaineng_blackout", m100, m20, ColorTier::Secondary, "[&BBkNAAA=]" },
        }
    },
    { "dragons_end",
        34101.0f, 103128.0f, m120, COL_EOD,
        {
            { "jade_maw",                0,       MIN(8), ColorTier::Secondary, "[&BKIMAAA=]", true, 1, {}, true, {m5, m45} },
            { "battle_for_the_jade_sea", m60,     m60,            ColorTier::Primary,   "[&BKIMAAA=]" },
        }
    },

    //_ Secrets of the Obscure
    { "skywatch_archipelago",
        26013.0f, 23715.0f, m120, COL_SOTO,
        {
            { "unlocking_the_wizards_tower", m60, m25, ColorTier::Primary, "[&BL4NAAA=]" },
        }
    },
    { "wizards_tower",
        24444.0f, 22384.0f, m120, COL_SOTO,
        {
            { "target_practice", m60,  m55, ColorTier::Primary,   "[&BB8OAAA=]" },
            { "fly_by_night",    m100, m40, ColorTier::Secondary, "[&BB8OAAA=]" },
        }
    },
    { "amnytas",
        24082.0f, 20290.0f, m120, COL_SOTO,
        {
            { "defense_of_amnytas", 0, m25, ColorTier::Primary, "[&BDQOAAA=]" },
        }
    },

    //_ Janthir Wilds
    { "janthir_syntri",
        39981.0f, 15269.0f, m120, COL_JW,
        {
            { "of_mists_and_monsters", m30,m25, ColorTier::Primary, "[&BCoPAAA=]" },
        }
    },
    { "bava_nisos",
        36513.0f, 11571.0f, m120, COL_JW,
        {
            { "a_titanic_voyage", m80, m25, ColorTier::Primary, "[&BGEPAAA=]" },
        }
    },

    //_ Visions of Eternity
    { "shipwreck_strand",
        10515.0f, 59212.0f, m120, COL_VOE,
        {
            { "hammerhart_rumble", m40, m20, ColorTier::Primary, "[&BJEPAAA=]" },
        }
    },
    { "starlit_weald",
        7310.0f, 58945.0f, m120, COL_VOE,
        {
            { "secrets_of_the_weald", m100, m35, ColorTier::Primary, "[&BJ4PAAA=]" },
        }
    },
    { "eternitys_garden",
        4566.0f, 61793.0f, m120, COL_VOE,
        {
            { "shackles_of_the_ancients", m75, m25, ColorTier::Primary, "[&BPwPAAA=]" },
        }
    },
    { "leyspring_hollows",
        5384.0f, 57790.0f, MIN(180), COL_VOE,
        {
            { "nexus_of_eternity",     m60,  m25, ColorTier::Secondary, "[&BB8QAAA=]" },
            { "the_depths_of_cruelty", m115, m25, ColorTier::Primary,   "[&BDYQAAA=]" },
        }
    },
};

//_ Same idea as g_DefaultBasicCategories (events_basic.cpp), per banner.
std::vector<CategoryDefault> g_DefaultCyclicCategories =
{
    {"living_world", {
        {"dry_top"},
        {"lake_doric"},
        {"domain_of_istan"},
        {"jahai_bluffs"},
        {"thunderhead_peaks"},
    }},
    {"heart_of_thorns", {
        {"verdant_brink"},
        {"auric_basin"},
        {"tangled_depths"},
        {"dragons_stand"},
    }},
    {"path_of_fire", {
        {"crystal_oasis"},
        {"desert_highlands"},
        {"elon_riverlands"},
        {"the_desolation"},
        {"domain_of_vabbi"},
    }},
    {"icebrood_saga", {
        {"grothmar_valley"},
        {"bjora_marches"},
        {"eye_of_the_north"},
    }},
    {"end_of_dragons", {
        {"seitung_province"},
        {"new_kaineng_city"},
        {"the_echovald_wilds"},
        {"dragons_end"},
    }},
    {"secrets_of_the_obscure", {
        {"skywatch_archipelago"},
        {"wizards_tower"},
        {"amnytas"},
    }},
    {"janthir_wilds", {
        {"janthir_syntri"},
        {"bava_nisos"},
    }},
    {"visions_of_eternity", {
        {"shipwreck_strand"},
        {"starlit_weald"},
        {"eternitys_garden"},
        {"leyspring_hollows"},
    }},
    {"festivals", {
        {"festival_of_the_four_winds"},
    }},
};

//_ One-time Slot corrections; see SlotOverride (events.h), EVENTS_DATA_VERSION.
std::vector<SlotOverride> g_SlotOverrides =
{
    //_ example: {"verdant_brink",   "night_enemy",  m105, std::nullopt},
    {"eternitys_garden", "shackles_of_the_ancients", m75}
};