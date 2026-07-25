//################################################################################
// time_format.h
//--------------------------------------------------------------------------------
// FormatMinSec(s)    formats a duration as "Xm Ys"
// FormatCountdown(s) formats as "Xh Ym" past an hour, else same as FormatMinSec
//--------------------------------------------------------------------------------
// Small formatting helpers for the countdown text shared by maprender.cpp,
// cyclicrender.cpp, and subscriptions_window.cpp, so the minute/second
// (and hour/minute) split exists in exactly one place. Callers still own
// their own surrounding wording ("Active (ends in ...)", " -- in ...",
// etc.); only the numeric formatting itself is shared.
//--------------------------------------------------------------------------------

#pragma once

#include <string>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// FormatMinSec / FormatCountdown
//--------------------------------------------------------------------------------
// FormatMinSec formats a duration as "Xm Ys" (seconds zero-padded to 2
// digits), e.g. "5m 03s", and never switches to an hour form.
// FormatCountdown formats as "Xh Ym" once the duration reaches a full hour
// (3600s), otherwise falls back to FormatMinSec's "Xm Ys" form. Use this one
// for any "starts in ..." countdown that can span past 60 minutes.
//--------------------------------------------------------------------------------
std::string FormatMinSec(int totalSeconds);
std::string FormatCountdown(int totalSeconds);