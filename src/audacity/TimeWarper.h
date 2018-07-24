/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2009 Audacity Team
   License: GPL v2 - see LICENSE.txt

   Dan Horgan

******************************************************************//**

\file TimeWarper.h
\brief Contains declarations for TimeWarper, IdentityTimeWarper,
ShiftTimeWarper, LinearTimeWarper, LinearInputRateSlideTimeWarper,
LinearOutputRateSlideTimeWarper, LinearInputInverseRateTimeWarper,
GeometricInputRateTimeWarper, GeometricOutputRateTimeWarper classes

\class TimeWarper
\brief Transforms one point in time to another point. For example, a time
stretching effect might use one to keep track of what happens to labels and
split points in the input.

\class IdentityTimeWarper
\brief No change to time at all

\class ShiftTimeWarper
\brief Behaves like another, given TimeWarper, except shifted by a fixed amount

\class LinearTimeWarper
\brief Linear scaling, initialised by giving two points on the line

\class LinearInputRateTimeWarper
\brief TimeScale - rate varies linearly with input

\class LinearOutputRateTimeWarper
\brief TimeScale - rate varies linearly with output

\class LinearInputInverseRateTimeWarper
\brief TimeScale - inverse rate varies linearly with input

\class GeometricInputRateTimeWarper
\brief TimeScale - rate varies geometrically with input

\class GeometricOutputRateTimeWarper
\brief TimeScale - rate varies geometrically with output

\class StepTimeWarper
\brief Like identity but with a jump

\class RegionTimeWarper
\brief No change before the specified region; during the region, warp according
to the given warper; after the region, constant shift so as to match at the end
of the warped region.

*//*******************************************************************/

#ifndef __TIMEWARPER__
#define __TIMEWARPER__

#include "MemoryX.h"


class TimeWarper /* not final */
{
public:
    virtual ~TimeWarper() {}

    virtual double Warp(double originalTime) const = 0;
};

class IdentityTimeWarper final : public TimeWarper {
public:
    double Warp(double originalTime) const override;
};


#endif
