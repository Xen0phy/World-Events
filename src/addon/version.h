//################################################################################
// version.h
//--------------------------------------------------------------------------------

#pragma once

#include <string>

constexpr int Maj = 1;   //. major version
constexpr int Min = 9;   //. minor version
constexpr int Bld = 0;   //. build number
constexpr int Rev = 0;   //. revision number

//_ Release timestamp, refreshed in place by bump_rev.py on every build.
inline const std::string DateAndTime = "2026-09-27 17:44";