//################################################################################
// time_format.cpp
//--------------------------------------------------------------------------------

#include "time_format.h"

#include <cstdio>

std::string FormatMinSec(int totalSeconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%dm %02ds", totalSeconds / 60, totalSeconds % 60);
    return buf;
}

std::string FormatCountdown(int totalSeconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    if (totalSeconds >= 3600)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%dh %02dm", totalSeconds / 3600, (totalSeconds % 3600) / 60);
        return buf;
    }
    return FormatMinSec(totalSeconds);
}