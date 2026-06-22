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

    // Living World
    { "Dry top",                 37129.0f, 32802.0f,  false,  900, {}},
    // { "Lake Doric", 45564.0f, 27004.0f,  false,  900, {}}, // multiple events, needs circle
    { "Domain of Istan",         57165.0f, 62605.0f,  false, 1800, {},  7200, 6300},
    { "Jahai Bluffs",            65135.0f, 57421.0f,  false,  900, {},  7200, 4500},// multiple events, needs circle
    { "Thunderhead Peaks",       57950.0f, 37800.0f,  false,  900, {},  3600, 2700},// multiple events, needs circle

    // Heart of Thorns
    { "Verdant Brink",           35049.0f, 31779.0f,  false,  900, {},  7200,  600},
    { "Auric Basin",             34303.0f, 33915.0f,  false,  900, {},  7200, 3600},
    { "Tangled Depths",          37010.0f, 35040.0f,  false,  900, {},  7200, 1800},
    { "Dragon's Stand",          35709.0f, 37182.0f,  false,  900, {},  7200, 5400},

    // Path of Fire
    { "Crystal Oasis",           58692.0f, 43752.0f,  false,  900, {},  7200, 1200},// multiple events, needs circle
    { "Desert Highlands",        59964.0f, 41384.0f,  false,  900, {},  7200, 3600},
    { "Elon Riverlands",         60715.0f, 45646.0f,  false,  900, {},  7200, 5400},// multiple events, needs circle
    // { "The Desolation",  59943.0f, 50257.0f,  false,  900, {}},// multiple events, needs circle
    { "Domain of Vabbi",         66332.0f, 53596.0f,  false,  900, {},  3600,    0},// multiple events, needs circle

    // Ice Brood Saga
    // { "Grothmar Valley", 60957.0f, 19174.0f,  false,  900, {}, 7200},// multiple events, needs circle
    { "Bjora Marches",           57267.0f, 18383.0f,  false,  900, {},  7200, 3900},// multiple events, needs circle

    // End of Dragons
    { "Seitung Province",        23247.0f, 102143.0f, false,  900, {},  7200, 5400},
    { "New Kaineng City",        27975.0f, 99331.0f,  false,  900, {},  7200,    0},
    { "The Echovald Wilds",      31118.0f, 102764.0f, false,  900, {},  7200, 1800},// multiple events, needs circle
    { "Dragon's End",            34101.0f, 103128.0f, false,  900, {},  7200, 3600},// multiple events, needs circle

    // Secrets of the Obscure
    { "Skywatch archipelago",    25433.0f, 23305.0f,  false,  900, {},  7200, 3600},
    { "Amnytas",                 24087.0f, 20402.0f,  false,  900, {},  7200,    0},
    // { "Wizard's Tower", 24668.0f, 22698.0f,  false,  900, {}, 7200},// multiple events, needs circle

    // Janthir Wilds
    { "Janthir Syntri",          39981.0f, 15269.0f,  false,  900, {},  7200, 1800},
    { "Bava Nisos",              36513.0f, 11571.0f,  false,  900, {},  7200, 4800},
    
    // Visions of Eternity
    { "Shipwreck Stand",         10515.0f, 59212.0f,  false,  900, {},  7200, 2400},
    { "Starlit Weald",           7310.0f,  58945.0f,  false,  900, {},  7200, 6000},
    { "Eternity's Garden",       4566.0f,  61793.0f,  false,  900, {},  7200, 4200},

    // Public Instances
    // { "Dragonstorm",             57090.0f, 21795.0f,  false,  900, {}, 7200},
    { "Convergences (SotO)",     23503.0f, 22698.0f,  false,  900, {}, 10800, 5400},
    { "Convergences (JW)",       43127.0f, 22669.0f,  false,  900, {}, 10800,    0},
};
