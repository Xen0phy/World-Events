//################################################################################
// events_basic.cpp
//--------------------------------------------------------------------------------
// g_Events                  compiled-in Basic Event roster (see WorldEvent,
//                           events.h)
// g_DefaultBasicCategories  compiled-in category defaults for the roster
//                           above (see CategoryDefault, events_categories.h)
//--------------------------------------------------------------------------------
// Hand-written data only - no logic. Rows are grouped by comment banner
// (Instanced / Core bosses / LLA / Invasions / Fractal Incursions), matching
// g_DefaultBasicCategories one-for-one below.
//--------------------------------------------------------------------------------

#include "events.h"
#include "events_categories.h"
#include <optional>

//_ Coords, most of which were taken directly from Sognus's World Bosses data.
std::vector<WorldEvent> g_Events =
{
    //_ Instanced
    {"Outer Nayos",                      24046.0f, 22754.0f, false, m10, "[&BB8OAAA=]",  true, "Convergence.png", {}, MIN(180),       m90},
    {"Mount Balrior",                    43095.0f, 22672.0f, false, m10, "[&BK4OAAA=]",  true, "Convergence.png", {}, MIN(180),         0},

    //_ Core bosses; trailing string per row is that boss's apiWorldBossId.
    {"Admiral Taidha Covington",         48872.0f, 33548.0f, false, m15, "[&BKgBAAA=]",  true, "WorldBoss.png",   {}, MIN(180),         0, "admiral_taidha_covington"},
    {"Claw of Jormag",                   56032.0f, 25417.0f, false, m15, "[&BHoCAAA=]",  true, "WorldBoss.png",   {}, MIN(180), MIN( 150), "claw_of_jormag"},
    {"Fire Elemental",                   40346.0f, 33755.0f, false, m15, "[&BEcAAAA=]",  true, "WorldBoss.png",   {},     m120,       m45, "fire_elemental"},
    {"Golem Mark II",                    53954.0f, 38916.0f, false, m15, "[&BNQCAAA=]",  true, "WorldBoss.png",   {}, MIN(180),      m120, "inquest_golem_mark_ii"},
    {"Great Jungle Wurm",                42365.0f, 33145.0f, false, m15, "[&BEEFAAA=]",  true, "WorldBoss.png",   {},     m120,       m75, "great_jungle_wurm"},
    {"Karka Queen",                      46346.0f, 35978.0f,  true, m15, "[&BNUGAAA=]",  true, "WorldBoss.png",   {
        m120,                //. 02:00 UTC+0
        MIN( 360),  //. 06:00
        MIN( 630),  //. 10:30
        MIN( 900),  //. 15:00
        MIN(1080),  //. 18:00
        MIN(1380)}, //. 23:00
        0, 0, "karka_queen"},
    {"Megadestroyer",                    51939.0f, 39395.0f, false, m15, "[&BM0CAAA=]",  true, "WorldBoss.png",   {}, MIN(180),       m30, "megadestroyer"},
    {"Modniir Ulgoth",                   49079.0f, 26174.0f, false, m15, "[&BLAAAAA=]",  true, "WorldBoss.png",   {}, MIN(180),       m90, "modniir_ulgoth"},
    {"Shadow Behemoth",                  44837.0f, 29997.0f,  false, m15, "[&BPcAAAA=]",  true, "WorldBoss.png",   {},    m120,       m105, "shadow_behemoth"},
    {"Svanir Shaman Chief",              56071.0f, 29379.0f,  false, m15, "[&BMIDAAA=]",  true, "WorldBoss.png",   {},    m120,        m15, "svanir_shaman_chief"},
    {"Tequatl the Sunless",              48412.0f, 38488.0f,   true, m15, "[&BNABAAA=]",  true, "WorldBoss.png",   { 
        0,                   //. 00:00 UTC+0
        MIN( 180),  //. 03:00
        MIN( 420),  //. 07:00
        MIN( 690),  //. 11:30
        MIN( 960),  //. 16:00
        MIN(1140)}, //. 19:00
        0, 0, "tequatl_the_sunless"},
    {"The Shatterer",                    62512.0f, 29023.0f,  false, m15, "[&BE4DAAA=]",  true, "WorldBoss.png",   {}, MIN(180),       m60, "the_shatterer"},
    {"Triple Trouble",                   49480.0f, 34069.0f,   true, m15, "[&BKoBAAA=]",  true, "WorldBoss.png",   {
        m60,                 //. 01:00 UTC+0
        MIN( 240),  //. 04:00
        MIN( 480),  //. 08:00
        MIN( 750),  //. 12:30
        MIN(1020),  //. 17:00
        MIN(1200)}, //. 20:00
        0, 0, "triple_trouble_wurm"},

    //_ Ley Line Anomaly; one chest/day, so all three share a doneGroup.
    {"Ley Line Anomaly (Timberline)",    52914.0f, 35729.0f,  false, m20, "[&BEwCAAA=]",  true, "EventBoss.png",   {}, MIN(360),      m20, "", "Ley Line Anomaly"},
    {"Ley Line Anomaly (Iron Marches)",  60822.0f, 28530.0f,  false, m20, "[&BOcBAAA=]",  true, "EventBoss.png",   {}, MIN(360), MIN(140), "", "Ley Line Anomaly"},
    {"Ley Line Anomaly (Gendarran)",     48365.0f, 29970.0f,  false, m20, "[&BOQAAAA=]",  true, "EventBoss.png",   {}, MIN(360), MIN(260), "", "Ley Line Anomaly"},

    //_ Invasions
    {"Scarlet's Portal Invasion",        47338.0f, 29795.0f,  false, m15, "[&BOQAAAA=]", false, "EventMap.png",    {},     m120,      m60},
    {"Awakened Invasion (Caledon)",      43417.0f, 34490.0f,  false, m15, "[&BD0BAAA=]", false, "EventMap.png",    {}, MIN(420),      m30},
    {"Awakened Invasion (Queensdale)",   44275.0f, 29574.0f,  false, m15, "[&BPcAAAA=]", false, "EventMap.png",    {}, MIN(420),      m90},
    {"Awakened Invasion (Wayfarer)",     55533.0f, 30058.0f,  false, m15, "[&BH0BAAA=]", false, "EventMap.png",    {}, MIN(420), MIN(150)},
    {"Awakened Invasion (Ashford)",      60209.0f, 30769.0f,  false, m15, "[&BJkDAAA=]", false, "EventMap.png",    {}, MIN(420), MIN(210)},
    {"Awakened Invasion (Gendarran)",    49271.0f, 29728.0f,  false, m15, "[&BOQAAAA=]", false, "EventMap.png",    {}, MIN(420), MIN(270)},
    {"Awakened Invasion (Southsun)",     45472.0f, 36279.0f,  false, m15, "[&BNUGAAA=]", false, "EventMap.png",    {}, MIN(420), MIN(330)},
    {"Awakened Invasion (Metrica)",      41010.0f, 35462.0f,  false, m15, "[&BEgAAAA=]", false, "EventMap.png",    {}, MIN(420), MIN(390)},

    //_ Fractal Incursions
    {"Fractal Incursion (Brisban)",      45568.0f, 32025.0f,  false, m15, "[&BBIAAAA=]", false, "EventBoss.png",   {}, MIN(240),        0},
    {"Fractal Incursion (Snowden)",      51347.0f, 29164.0f,  false, m15, "[&BLQAAAA=]", false, "EventBoss.png",   {}, MIN(240),      m60},
    {"Fractal Incursion (Kessex)",       40826.0f, 33123.0f,  false, m15, "[&BHUAAAA=]", false, "EventBoss.png",   {}, MIN(240),     m120},
    {"Fractal Incursion (Diessa)",       58085.0f, 29462.0f,  false, m15, "[&BLQAAAA=]", false, "EventBoss.png",   {}, MIN(240), MIN(180)},

    //_ Festivals
    {"Your Mad King Says...",            49178.0f, 31170.0f,  false, m10, "[&BBEEAAA=]", false,  "Festival.png",   {},     m120,        0},
    {"Dragon Bash (Wayfarer)",           55580.0f, 30795.0f,  false,  m5, "[&BH0BAAA=]", false,  "Festival.png",   {},      m60,        0},
    {"Dragon Bash (Dredgehaunt)",        53309.0f, 32602.0f,  false,  m5, "[&BGMCAAA=]", false,  "Festival.png",   {},      m60,      m15},
    {"Dragon Bash (Lornar's)",           51371.0f, 32602.0f,  false,  m5, "[&BJkBAAA=]", false,  "Festival.png",   {},      m60,      m30},
    {"Dragon Bash (Snowden)",            52475.0f, 28935.0f,  false,  m5, "[&BL4AAAA=]", false,  "Festival.png",   {},      m60,      m45},
};

//_ Compiled-in category defaults, one per group above (CategoryDefault).
std::vector<CategoryDefault> g_DefaultBasicCategories =
{
    {"Instanced", {
        {"Outer Nayos", false, std::nullopt, m10},
        {"Mount Balrior", false, std::nullopt, m10},
    }},
    {"Core bosses", {
        {"Admiral Taidha Covington"},
        {"Claw of Jormag"},
        {"Fire Elemental"},
        {"Golem Mark II"},
        {"Great Jungle Wurm"},
        {"Karka Queen"},
        {"Megadestroyer"},
        {"Modniir Ulgoth"},
        {"Shadow Behemoth"},
        {"Svanir Shaman Chief"},
        {"Tequatl the Sunless"},
        {"The Shatterer"},
        {"Triple Trouble"},
    }},
    {"LLA", {
        {"Ley Line Anomaly (Timberline)"},
        {"Ley Line Anomaly (Iron Marches)"},
        {"Ley Line Anomaly (Gendarran)"},
    }},
    {"Invasions", {
        {"Scarlet's Portal Invasion"},
        {"Awakened Invasion (Caledon)"},
        {"Awakened Invasion (Queensdale)"},
        {"Awakened Invasion (Wayfarer)"},
        {"Awakened Invasion (Ashford)"},
        {"Awakened Invasion (Gendarran)"},
        {"Awakened Invasion (Southsun)"},
        {"Awakened Invasion (Metrica)"},
    }},
    {"Fractal Incursions", {
        {"Fractal Incursion (Brisban)", true, 0},
        {"Fractal Incursion (Snowden)", true},
        {"Fractal Incursion (Kessex)", true, m120},
        {"Fractal Incursion (Diessa)", true},
    }},
    {"Festivals", {
        {"Your Mad King Says..."},
        {"Dragon Bash (Wayfarer)", true},
        {"Dragon Bash (Dredgehaunt)", true},
        {"Dragon Bash (Lornar's)", true},
        {"Dragon Bash (Snowden)", true},
    }},
};