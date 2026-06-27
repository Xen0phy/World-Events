#include "events.h"

// Continent coordinates taken directly from Sognus's fallback data.
// These are in GW2 continent 1 (Tyria) coordinates.
// Timer logic comes later — for now these are just positions.
std::vector<WorldEvent> g_Events =
{

    //{ "Tyrian Day",              10.0f,    10.0f},
    //{ "Canthan Day",             10.0f,    110.0f},

    // Core bosses (a few to test spread)
    {"Admiral Taidha Covington", 48872.0f, 33548.0f,  false,  900, {}, 10800,    0},
    {"Claw of Jormag",           56032.0f, 25417.0f,  false,  900, {}, 10800, 9000},
    {"Evolved Jungle Wurm",      49480.0f, 34069.0f,   true,  900, {3600, 14400, 28800, 45000, 61200, 72000}},
    {"Fire Elemental",           40346.0f, 33755.0f,  false,  900, {},  7200, 2700},
    {"Golem Mark II",            53954.0f, 38916.0f,  false,  900, {}, 10800, 7200},
    {"Great Jungle Wurm",        42365.0f, 33145.0f,  false,  900, {},  7200, 4500},
    {"Karka Queen",              46346.0f, 35978.0f,   true,  900, {7200, 21600, 37800, 54000, 64800, 82800}},
    {"Megadestroyer",            51939.0f, 39395.0f,  false,  900, {}, 10800, 1800},
    {"Modniir Ulgoth",           49079.0f, 26174.0f,  false,  900, {}, 10800, 5400},
    {"Shadow Behemoth",          44837.0f, 29997.0f,  false,  900, {},  7200, 6300},
    {"Shatterer",                62512.0f, 29023.0f,  false,  900, {}, 10800, 3600},
    {"Svanir Shaman Chief",      56071.0f, 29379.0f,  false,  900, {},  7200,  900},
    {"Tequatl",                  48412.0f, 38488.0f,   true,  900, {0, 10800, 25200, 41400, 57600, 68400}},
};
