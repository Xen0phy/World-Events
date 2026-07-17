#include "events.h"
#include "events_categories.h"

// Continent coordinates taken directly from Sognus's fallback data.
// These are in GW2 continent 1 (Tyria) coordinates.
std::vector<WorldEvent> g_Events =
{
    // Instanced
    {"Outer Nayos",                      24046.0f, 22754.0f, false, m20, "[&BB8OAAA=]",  true, "Convergence.png", {}, 3 * m60,           m90},
    {"Mount Balrior",                    43095.0f, 22672.0f, false, m20, "[&BK4OAAA=]",  true, "Convergence.png", {}, 3 * m60,             0},

    // Core bosses
    // Trailing string on each of these 13 rows is the GW2 API's
    // /v2/worldbosses id for this boss (verified against the wiki's
    // documented list) — see WorldEvent::apiWorldBossId in events.h and
    // gw2_api.h for what it's used for. No other section in this file
    // gets one: these 13 are the ONLY events the public API can confirm
    // "already done today" for.
    {"Admiral Taidha Covington",         48872.0f, 33548.0f, false, m15, "[&BKgBAAA=]",  true, "WorldBoss.png",   {}, 3 * m60,             0, "admiral_taidha_covington"},
    {"Claw of Jormag",                   56032.0f, 25417.0f, false, m15, "[&BHoCAAA=]",  true, "WorldBoss.png",   {}, 3 * m60, 2 * m60 + m30, "claw_of_jormag"},
    {"Fire Elemental",                   40346.0f, 33755.0f, false, m15, "[&BEcAAAA=]",  true, "WorldBoss.png",   {},    m120,           m45, "fire_elemental"},
    {"Golem Mark II",                    53954.0f, 38916.0f, false, m15, "[&BNQCAAA=]",  true, "WorldBoss.png",   {}, 3 * m60,          m120, "inquest_golem_mark_ii"},
    {"Great Jungle Wurm",                42365.0f, 33145.0f, false, m15, "[&BEEFAAA=]",  true, "WorldBoss.png",   {},    m120,           m75, "great_jungle_wurm"},
    {"Karka Queen",                      46346.0f, 35978.0f,  true, m15, "[&BNUGAAA=]",  true, "WorldBoss.png",   {      m120, 
                                                                                                                                                               6 * m60,
                                                                                                                                                              10 * m60 + m30,
                                                                                                                                                              15 * m60,
                                                                                                                                                              18 * m60,
                                                                                                                                                              23 * m60}, 0, 0, "karka_queen"},
    {"Megadestroyer",                    51939.0f, 39395.0f, false, m15, "[&BM0CAAA=]",  true, "WorldBoss.png",   {}, 3 * m60,           m30, "megadestroyer"},
    {"Modniir Ulgoth",                   49079.0f, 26174.0f, false, m15, "[&BLAAAAA=]",  true, "WorldBoss.png",   {}, 3 * m60,           m90, "modniir_ulgoth"},
    {"Shadow Behemoth",                  44837.0f, 29997.0f,  false, m15, "[&BPcAAAA=]",  true, "WorldBoss.png",   {},    m120,          m105, "shadow_behemoth"},
    {"Svanir Shaman Chief",              56071.0f, 29379.0f,  false, m15, "[&BMIDAAA=]",  true, "WorldBoss.png",   {},    m120,           m15, "svanir_shaman_chief"},
    {"Tequatl the Sunless",              48412.0f, 38488.0f,   true, m15, "[&BNABAAA=]",  true, "WorldBoss.png",   {         0,
                                                                                                                                                               3 * m60,
                                                                                                                                                               7 * m60,
                                                                                                                                                              11 * m60 + m30,
                                                                                                                                                              16 * m60,
                                                                                                                                                              19 * m60}, 0, 0, "tequatl_the_sunless"},
    {"The Shatterer",                    62512.0f, 29023.0f,  false, m15, "[&BE4DAAA=]",  true, "WorldBoss.png",   {}, 3 * m60,           m60, "the_shatterer"},
    {"Triple Trouble",                   49480.0f, 34069.0f,   true, m15, "[&BKoBAAA=]",  true, "WorldBoss.png",   {       m60,
                                                                                                                                                               4 * m60,
                                                                                                                                                               8 * m60,
                                                                                                                                                              12 * m60 + m30,
                                                                                                                                                              17 * m60,
                                                                                                                                                              20 * m60}, 0, 0, "triple_trouble_wurm"},

    // Ley Line Anomaly
    {"Ley Line Anomaly (Iron Marches)",  60822.0f, 28530.0f,  false, m20, "[&BOcBAAA=]",  true, "EventBoss.png",   {}, 6 * m60,           m20},
    {"Ley Line Anomaly (Gendarran)",     48365.0f, 29970.0f,  false, m20, "[&BOQAAAA=]",  true, "EventBoss.png",   {}, 6 * m60,    m120 + m20},
    {"Ley Line Anomaly (Timberline)",    52914.0f, 35729.0f,  false, m20, "[&BEwCAAA=]",  true, "EventBoss.png",   {}, 6 * m60, 4 * m60 + m20},

    // Invasions
    {"Scarlet's Portal Invasion",        47338.0f, 29795.0f,  false, m15, "[&BOQAAAA=]", false, "EventMap.png",    {},    m120,           m60},
    {"Awakened Invasion (Caledon)",      43417.0f, 34490.0f,  false, m15, "[&BD0BAAA=]", false, "EventMap.png",    {}, 7 * m60,           m30},
    {"Awakened Invasion (Queensdale)",   44275.0f, 29574.0f,  false, m15, "[&BPcAAAA=]", false, "EventMap.png",    {}, 7 * m60,           m90},
    {"Awakened Invasion (Wayfarer)",     55533.0f, 30058.0f,  false, m15, "[&BH0BAAA=]", false, "EventMap.png",    {}, 7 * m60, 2 * m60 + m30},
    {"Awakened Invasion (Ashford)",      60209.0f, 30769.0f,  false, m15, "[&BJkDAAA=]", false, "EventMap.png",    {}, 7 * m60, 3 * m60 + m30},
    {"Awakened Invasion (Gendarran)",    49271.0f, 29728.0f,  false, m15, "[&BOQAAAA=]", false, "EventMap.png",    {}, 7 * m60, 4 * m60 + m30},
    {"Awakened Invasion (Southsun)",     45472.0f, 36279.0f,  false, m15, "[&BNUGAAA=]", false, "EventMap.png",    {}, 7 * m60, 5 * m60 + m30},
    {"Awakened Invasion (Metrica)",      41010.0f, 35462.0f,  false, m15, "[&BEgAAAA=]", false, "EventMap.png",    {}, 7 * m60, 6 * m60 + m30},

    // Fractal Incursions
    {"Fractal Incursion (Kessex)",       45568.0f, 32025.0f,  false, m15, "[&BBIAAAA=]", false, "EventBoss.png",   {}, 4 * m60,             0},
    {"Fractal Incursion (Snowden)",      51347.0f, 29164.0f,  false, m15, "[&BLQAAAA=]", false, "EventBoss.png",   {}, 4 * m60,           m60},
    {"Fractal Incursion (Brisban)",      40826.0f, 33123.0f,  false, m15, "[&BHUAAAA=]", false, "EventBoss.png",   {}, 4 * m60,          m120},
    {"Fractal Incursion (Diessa)",       58085.0f, 29462.0f,  false, m15, "[&BLQAAAA=]", false, "EventBoss.png",   {}, 4 * m60,       3 * m60},
};

// Compiled-in default categories for the options-panel list, one per
// comment-grouping above — see CategoryDefault in events_categories.h. Built on
// first load (or whenever EVENTS_DATA_VERSION advances past what a user's
// saved file has), then fully user-editable from there: renaming, deleting,
// or moving members around all stick, since LoadCategoriesData only
// re-applies these when the file is genuinely behind this build's content.
std::vector<CategoryDefault> g_DefaultBasicCategories =
{
    {"Instanced", {
        {"Outer Nayos"},
        {"Mount Balrior"},
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
        {"Ley Line Anomaly (Iron Marches)"},
        {"Ley Line Anomaly (Gendarran)"},
        {"Ley Line Anomaly (Timberline)"},
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
        {"Fractal Incursion (Kessex)"},
        {"Fractal Incursion (Snowden)"},
        {"Fractal Incursion (Brisban)"},
        {"Fractal Incursion (Diessa)"},
    }},
};
