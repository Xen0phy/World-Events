//################################################################################
// version.h
//--------------------------------------------------------------------------------

#pragma once

#include <string>

constexpr int Maj = 1;   //. major version
constexpr int Min = 8;   //. minor version
constexpr int Bld = 14;   //. build number
constexpr int Rev = 2;   //. revision number

//_ Release timestamp, refreshed in place by bump_rev.py on every build.
inline const std::string DateAndTime = "2026-09-27 12:41";