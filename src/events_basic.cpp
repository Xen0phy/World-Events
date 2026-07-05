#include "events.h"

// Continent coordinates taken directly from Sognus's fallback data.
// These are in GW2 continent 1 (Tyria) coordinates.
// Timer logic comes later — for now these are just positions.
std::vector<WorldEvent> g_Events =
{
    // Instanced
    {"Outer Nayos",                      24046.0f, 22754.0f,  false, m20, "[&BB8OAAA=]", {}, 3 * m60,  m90},
    {"Mount Balrior",                    43095.0f, 22672.0f,  false, m20, "[&BK4OAAA=]", {}, 3 * m60,     0},

    // Core bosses
    {"Admiral Taidha Covington",         48872.0f, 33548.0f,  false, m15, "[&BKgBAAA=]", {}, 3 * m60,     0},
    {"Claw of Jormag",                   56032.0f, 25417.0f,  false, m15, "[&BHoCAAA=]", {}, 3 * m60,  3 * m60 + m30},
    {"Fire Elemental",                   40346.0f, 33755.0f,  false, m15, "[&BEcAAAA=]", {},  m120,  m45},
    {"Golem Mark II",                    53954.0f, 38916.0f,  false, m15, "[&BNQCAAA=]", {}, 3 * m60,  m120},
    {"Great Jungle Wurm",                42365.0f, 33145.0f,  false, m15, "[&BEEFAAA=]", {},  m120,  m75},
    {"Karka Queen",                      46346.0f, 35978.0f,   true, m15, "[&BNUGAAA=]", {m120, 6 * m60, 10 * m60 + m30, 15 * m60, 18 * m60, 23 * m60}},
    {"Megadestroyer",                    51939.0f, 39395.0f,  false, m15, "[&BM0CAAA=]", {}, 3 * m60,  m30},
    {"Modniir Ulgoth",                   49079.0f, 26174.0f,  false, m15, "[&BLAAAAA=]", {}, 3 * m60,  m90},
    {"Shadow Behemoth",                  44837.0f, 29997.0f,  false, m15, "[&BPcAAAA=]", {},  m120,  m105},
    {"Svanir Shaman Chief",              56071.0f, 29379.0f,  false, m15, "[&BMIDAAA=]", {},  m120,  m15},
    {"Tequatl the Sunless",              48412.0f, 38488.0f,   true, m15, "[&BNABAAA=]", {0, 3 * m60,  7 * m60, 11 * m60 + m30, 16 * m60, 19 * m60}},
    {"The Shatterer",                    62512.0f, 29023.0f,  false, m15, "[&BE4DAAA=]", {}, 3 * m60,  m60},
    {"Triple Trouble",                   49480.0f, 34069.0f,   true, m15, "[&BKoBAAA=]", {m60, 4 * m60, 8 * m60, 12 * m60 + m30, 17 * m60, 20 * m60}},

    // LLA
    {"Ley Line Anomaly (Iron Marches)",  60822.0f, 28530.0f,  false, m20, "[&BOcBAAA=]", {}, 6 * m60,  m20},
    {"Ley Line Anomaly (Gendarran)",     48365.0f, 29970.0f,  false, m20, "[&BOQAAAA=]", {}, 6 * m60,  m120 + m20},
    {"Ley Line Anomaly (Timberline)",    52914.0f, 35729.0f,  false, m20, "[&BEwCAAA=]", {}, 6 * m60, 4 * m60 + m20},

    // Invasions
    {"Scarlet's Portal Invasion",        47338.0f, 29795.0f,  false, m15, "[&BOQAAAA=]", {}, m120,   m60},
    {"Awakened Invasion (Caledon)",      43417.0f, 34490.0f,  false, m15, "[&BD0BAAA=]", {}, 7 * m60,  m30},
    {"Awakened Invasion (Queensdale)",   44275.0f, 29574.0f,  false, m15, "[&BPcAAAA=]", {}, 7 * m60,  m90},
    {"Awakened Invasion (Wayfarer)",     55533.0f, 30058.0f,  false, m15, "[&BH0BAAA=]", {}, 7 * m60, 2 * m60 + m30},
    {"Awakened Invasion (Ashford)",      60209.0f, 30769.0f,  false, m15, "[&BJkDAAA=]", {}, 7 * m60, 3 * m60 + m30},
    {"Awakened Invasion (Gendarran)",    49271.0f, 29728.0f,  false, m15, "[&BOQAAAA=]", {}, 7 * m60, 4 * m60 + m30},
    {"Awakened Invasion (Southsun)",     45472.0f, 36279.0f,  false, m15, "[&BNUGAAA=]", {}, 7 * m60, 5 * m60 + m30},
    {"Awakened Invasion (Metrica)",      41010.0f, 35462.0f,  false, m15, "[&BEgAAAA=]", {}, 7 * m60, 6 * m60 + m30},

    // Fractal Incursions
    {"Fractal Incursion (Kessex)",       45568.0f, 32025.0f,  false, m15, "[&BBIAAAA=]", {}, 4 * m60,     0},
    {"Fractal Incursion (Snowden)",      51347.0f, 29164.0f,  false, m15, "[&BLQAAAA=]", {}, 4 * m60,  m60},
    {"Fractal Incursion (Brisban)",      40826.0f, 33123.0f,  false, m15, "[&BHUAAAA=]", {}, 4 * m60,  m120},
    {"Fractal Incursion (Diessa)",       58085.0f, 29462.0f,  false, m15, "[&BLQAAAA=]", {}, 4 * m60, 3 * m60},
};
