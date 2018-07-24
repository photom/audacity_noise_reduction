/**********************************************************************

   Audacity - A Digital Audio Editor
   Copyright 1999-2009 Audacity Team
   License: GPL v2 - see LICENSE.txt

   Dan Horgan

******************************************************************//**

\file TimeWarper.cpp
\brief Contains definitions for IdentityTimeWarper, ShiftTimeWarper,
LinearTimeWarper, LogarithmicTimeWarper, QuadraticTimeWarper,
Geometric TimeWarper classes

*//*******************************************************************/

#include "Audacity.h"
#include "TimeWarper.h"

#include <string>
#include <cmath>

double IdentityTimeWarper::Warp(double originalTime) const {
    return originalTime;
}
