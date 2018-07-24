/**********************************************************************

  Audacity: A Digital Audio Editor

  Envelope.cpp

  Dominic Mazzoni (original author)
  Dr William Bland (integration - the Calculus kind)
  Monty (xiphmont) (important bug fixes)

*******************************************************************//**

\class Envelope
\brief Draggable curve used in TrackPanel for varying amplification.

  This class manages an envelope - i.e. a piecewise linear funtion
  that the user can edit by dragging control points around.  The
  envelope is most commonly used to control the amplitude of a
  waveform, but it is also used to shape the Equalization curve.

*//****************************************************************//**

\class EnvPoint
\brief EnvPoint, derived from XMLTagHandler, provides Envelope with
a draggable point type.

*//*******************************************************************/

#include <cassert>
#include <cmath>
#include "Envelope.h"

static const double VALUE_TOLERANCE = 0.001;

Envelope::Envelope(bool exponential, double minValue, double maxValue, double defaultValue)
        : mDB(exponential), mMinValue(minValue), mMaxValue(maxValue), mDefaultValue{ClampValue(defaultValue)} {
}

Envelope::Envelope(const Envelope &orig)
        : mDB(orig.mDB), mMinValue(orig.mMinValue), mMaxValue(orig.mMaxValue), mDefaultValue(orig.mDefaultValue) {
    mOffset = orig.mOffset;
    mTrackLen = orig.mTrackLen;
    CopyRange(orig, 0, orig.GetNumberOfPoints());
}

Envelope::Envelope(const Envelope &orig, double t0, double t1)
   : mDB(orig.mDB)
   , mMinValue(orig.mMinValue)
   , mMaxValue(orig.mMaxValue)
   , mDefaultValue(orig.mDefaultValue)
{
   mOffset = std::max(t0, orig.mOffset);
   mTrackLen = std::min(t1, orig.mOffset + orig.mTrackLen) - mOffset;

   auto range1 = orig.EqualRange( t0 - orig.mOffset, 0 );
   auto range2 = orig.EqualRange( t1 - orig.mOffset, 0 );
   CopyRange(orig, range1.first, range2.second);
}

size_t Envelope::GetNumberOfPoints() const {
    return mEnv.size();
}

Envelope::~Envelope() {
}

// Accessors
double Envelope::GetValue(double t, double sampleDur) const {
    // t is absolute time
    double temp;

    GetValues(&temp, 1, t, sampleDur);
    return temp;
}

void Envelope::GetValues(double *buffer, int bufferLen,
                         double t0, double tstep) const {
    // Convert t0 from absolute to clip-relative time
    t0 -= mOffset;
    GetValuesRelative(buffer, bufferLen, t0, tstep);
}

void Envelope::CopyRange(const Envelope &orig, size_t begin, size_t end) {
    size_t len = orig.mEnv.size();
    size_t i = begin;

    // Create the point at 0 if it needs interpolated representation
    if (i > 0)
        AddPointAtEnd(0, orig.GetValue(mOffset));

    // Copy points from inside the copied region
    for (; i < end; ++i) {
        const EnvPoint &point = orig[i];
        const double when = point.GetT() + (orig.mOffset - mOffset);
        AddPointAtEnd(when, point.GetVal());
    }

    // Create the final point if it needs interpolated representation
    // If the last point of e was exatly at t1, this effectively copies it too.
    if (mTrackLen > 0 && i < len)
        AddPointAtEnd(mTrackLen, orig.GetValue(mOffset + mTrackLen));
}

std::pair<int, int> Envelope::EqualRange(double when, double sampleDur) const {
    // Find range of envelope points matching the given time coordinate
    // (within an interval of length sampleDur)
    // by binary search; if empty, it still indicates where to
    // insert.
    const auto tolerance = sampleDur / 2;
    auto begin = mEnv.begin();
    auto end = mEnv.end();
    auto first = std::lower_bound(
            begin, end,
            EnvPoint{when - tolerance, 0.0},
            [](const EnvPoint &point1, const EnvPoint &point2) { return point1.GetT() < point2.GetT(); }
    );
    auto after = first;
    while (after != end && after->GetT() <= when + tolerance)
        ++after;
    return {first - begin, after - begin};
}

void Envelope::SetTrackLen(double trackLen, double sampleDur)
// NOFAIL-GUARANTEE
{
    // Preserve the left-side limit at trackLen.
    auto range = EqualRange(trackLen, sampleDur);
    bool needPoint = (range.first == range.second && trackLen < mTrackLen);
    double value = 0.0;
    if (needPoint)
        value = GetValueRelative(trackLen);

    mTrackLen = trackLen;

    // Shrink the array.
    // If more than one point already at the end, keep only the first of them.
    int newLen = std::min(1 + range.first, range.second);
    mEnv.resize(newLen);

    if (needPoint)
        AddPointAtEnd(mTrackLen, value);
}

double Envelope::GetValueRelative(double t, bool leftLimit) const {
    double temp;

    GetValuesRelative(&temp, 1, t, 0.0, leftLimit);
    return temp;
}

void Envelope::GetValuesRelative
        (double *buffer, int bufferLen, double t0, double tstep, bool leftLimit)
const {
    // JC: If bufferLen ==0 we have probably just allocated a zero sized buffer.
    // wxASSERT( bufferLen > 0 );

    const auto epsilon = tstep / 2;
    int len = mEnv.size();

    double t = t0;
    double increment = 0;
    if (len > 1 && t <= mEnv[0].GetT() && mEnv[0].GetT() == mEnv[1].GetT())
        increment = leftLimit ? -epsilon : epsilon;

    double tprev, vprev, tnext = 0, vnext, vstep = 0;

    for (int b = 0; b < bufferLen; b++) {

        // Get easiest cases out the way first...
        // IF empty envelope THEN default value
        if (len <= 0) {
            buffer[b] = mDefaultValue;
            t += tstep;
            continue;
        }

        auto tplus = t + increment;

        // IF before envelope THEN first value
        if (leftLimit ? tplus <= mEnv[0].GetT() : tplus < mEnv[0].GetT()) {
            buffer[b] = mEnv[0].GetVal();
            t += tstep;
            continue;
        }
        // IF after envelope THEN last value
        if (leftLimit
            ? tplus > mEnv[len - 1].GetT() : tplus >= mEnv[len - 1].GetT()) {
            buffer[b] = mEnv[len - 1].GetVal();
            t += tstep;
            continue;
        }

        // be careful to get the correct limit even in case epsilon == 0
        if (b == 0 ||
            (leftLimit ? tplus > tnext : tplus >= tnext)) {

            // We're beyond our tnext, so find the next one.
            // Don't just increment lo or hi because we might
            // be zoomed far out and that could be a large number of
            // points to move over.  That's why we binary search.

            int lo, hi;
            if (leftLimit)
                BinarySearchForTime_LeftLimit(lo, hi, tplus);
            else
                BinarySearchForTime(lo, hi, tplus);

            // mEnv[0] is before tplus because of eliminations above, therefore lo >= 0
            // mEnv[len - 1] is after tplus, therefore hi <= len - 1
            assert(lo >= 0 && hi <= len - 1);

            tprev = mEnv[lo].GetT();
            tnext = mEnv[hi].GetT();

            if (hi + 1 < len && tnext == mEnv[hi + 1].GetT())
                // There is a discontinuity after this point-to-point interval.
                // Usually will stop evaluating in this interval when time is slightly
                // before tNext, then use the right limit.
                // This is the right intent
                // in case small roundoff errors cause a sample time to be a little
                // before the envelope point time.
                // Less commonly we want a left limit, so we continue evaluating in
                // this interval until shortly after the discontinuity.
                increment = leftLimit ? -epsilon : epsilon;
            else
                increment = 0;

            vprev = GetInterpolationStartValueAtPoint(lo);
            vnext = GetInterpolationStartValueAtPoint(hi);

            // Interpolate, either linear or log depending on mDB.
            double dt = (tnext - tprev);
            double to = t - tprev;
            double v;
            if (dt > 0.0) {
                v = (vprev * (dt - to) + vnext * to) / dt;
                vstep = (vnext - vprev) * tstep / dt;
            } else {
                v = vnext;
                vstep = 0.0;
            }

            // An adjustment if logarithmic scale.
            if (mDB) {
                v = pow(10.0, v);
                vstep = pow(10.0, vstep);
            }

            buffer[b] = v;
        } else {
            if (mDB) {
                buffer[b] = buffer[b - 1] * vstep;
            } else {
                buffer[b] = buffer[b - 1] + vstep;
            }
        }

        t += tstep;
    }
}

// This is used only during construction of an Envelope by complete or partial
// copy of another, or when truncating a track.
void Envelope::AddPointAtEnd(double t, double val) {
    mEnv.push_back(EnvPoint{t, val});

    // Assume copied points were stored by nondecreasing time.
    // Allow no more than two points at exactly the same time.
    // Maybe that happened, because extra points were inserted at the boundary
    // of the copied range, which were not in the source envelope.
    auto nn = mEnv.size() - 1;
    while (nn >= 2 && mEnv[nn - 2].GetT() == t) {
        // Of three or more points at the same time, erase one in the middle,
        // not the one newly added.
        mEnv.erase(mEnv.begin() + nn - 1);
        --nn;
    }
}

// relative time
/// @param Lo returns last index at or before this time, maybe -1
/// @param Hi returns first index after this time, maybe past the end
void Envelope::BinarySearchForTime(int &Lo, int &Hi, double t) const {
    // Optimizations for the usual pattern of repeated calls with
    // small increases of t.
    {
        if (mSearchGuess >= 0 && mSearchGuess < (int) mEnv.size()) {
            if (t >= mEnv[mSearchGuess].GetT() &&
                (1 + mSearchGuess == (int) mEnv.size() ||
                 t < mEnv[1 + mSearchGuess].GetT())) {
                Lo = mSearchGuess;
                Hi = 1 + mSearchGuess;
                return;
            }
        }

        ++mSearchGuess;
        if (mSearchGuess >= 0 && mSearchGuess < (int) mEnv.size()) {
            if (t >= mEnv[mSearchGuess].GetT() &&
                (1 + mSearchGuess == (int) mEnv.size() ||
                 t < mEnv[1 + mSearchGuess].GetT())) {
                Lo = mSearchGuess;
                Hi = 1 + mSearchGuess;
                return;
            }
        }
    }

    Lo = -1;
    Hi = mEnv.size();

    // Invariants:  Lo is not less than -1, Hi not more than size
    while (Hi > (Lo + 1)) {
        int mid = (Lo + Hi) / 2;
        // mid must be strictly between Lo and Hi, therefore a valid index
        if (t < mEnv[mid].GetT())
            Hi = mid;
        else
            Lo = mid;
    }
    assert(Hi == (Lo + 1));

    mSearchGuess = Lo;
}

// relative time
/// @param Lo returns last index before this time, maybe -1
/// @param Hi returns first index at or after this time, maybe past the end
void Envelope::BinarySearchForTime_LeftLimit(int &Lo, int &Hi, double t) const {
    Lo = -1;
    Hi = mEnv.size();

    // Invariants:  Lo is not less than -1, Hi not more than size
    while (Hi > (Lo + 1)) {
        int mid = (Lo + Hi) / 2;
        // mid must be strictly between Lo and Hi, therefore a valid index
        if (t <= mEnv[mid].GetT())
            Hi = mid;
        else
            Lo = mid;
    }
    assert(Hi == (Lo + 1));

    mSearchGuess = Lo;
}

/// GetInterpolationStartValueAtPoint() is used to select either the
/// envelope value or its log depending on whether we are doing linear
/// or log interpolation.
/// @param iPoint index in env array to look at.
/// @return value there, or its (safe) log10.
double Envelope::GetInterpolationStartValueAtPoint(int iPoint) const {
    double v = mEnv[iPoint].GetVal();
    if (!mDB)
        return v;
    else
        return log10(v);
}

void Envelope::CollapseRegion(double t0, double t1, double sampleDur)
// NOFAIL-GUARANTEE
{
    if (t1 <= t0)
        return;

    // This gets called when somebody clears samples.

    // Snip points in the interval (t0, t1), shift values left at times after t1.
    // For the boundaries of the interval, preserve the left-side limit at the
    // start and right-side limit at the end.

    const auto epsilon = sampleDur / 2;
    t0 = std::max(0.0, std::min(mTrackLen, t0 - mOffset));
    t1 = std::max(0.0, std::min(mTrackLen, t1 - mOffset));
    bool leftPoint = true, rightPoint = true;

    // Determine the start of the range of points to remove from the array.
    auto range0 = EqualRange(t0, 0);
    auto begin = range0.first;
    if (begin == range0.second) {
        if (t0 > epsilon) {
            // There was no point exactly at t0;
            // insert a point to preserve the value.
            auto val = GetValueRelative(t0);
            InsertOrReplaceRelative(t0, val);
            ++begin;
        } else
            leftPoint = false;
    } else
        // We will keep the first (or only) point that was at t0.
        ++begin;

    // We want end to be the index one past the range of points to remove from
    // the array.
    // At first, find index of the first point after t1:
    auto range1 = EqualRange(t1, 0);
    auto end = range1.second;
    if (range1.first == end) {
        if (mTrackLen - t1 > epsilon) {
            // There was no point exactly at t1; insert a point to preserve the value.
            auto val = GetValueRelative(t1);
            InsertOrReplaceRelative(t1, val);
            // end is now the index of this NEW point and that is correct.
        } else
            rightPoint = false;
    } else
        // We will keep the last (or only) point that was at t1.
        --end;

    mEnv.erase(mEnv.begin() + begin, mEnv.begin() + end);

    // Shift points left after deleted region.
    auto len = mEnv.size();
    for (size_t i = begin; i < len; ++i) {
        auto &point = mEnv[i];
        if (rightPoint && (int) i == begin)
            // Avoid roundoff error.
            // Make exactly equal times of neighboring points so that we have
            // a real discontinuity.
            point.SetT(t0);
        else
            point.SetT(point.GetT() - (t1 - t0));
    }

    // See if the discontinuity is removable.
    if (rightPoint)
        RemoveUnneededPoints(begin, true);
    if (leftPoint)
        RemoveUnneededPoints(begin - 1, false);

    mTrackLen -= (t1 - t0);
}

/** @brief Add a control point to the envelope
 *
 * @param when the time in seconds when the envelope point should be created.
 * @param value the envelope value to use at the given point.
 * @return the index of the NEW envelope point within array of envelope points.
 */
int Envelope::InsertOrReplaceRelative(double when, double value) {
#if defined(__WXDEBUG__)
    // in debug builds, do a spot of argument checking
    if(when > mTrackLen + 0.0000001)
    {
       wxString msg;
       msg = wxString::Format(wxT("when %.20f mTrackLen %.20f diff %.20f"), when, mTrackLen, when-mTrackLen);
       wxASSERT_MSG(when <= (mTrackLen), msg);
    }
    if(when < 0)
    {
       wxString msg;
       msg = wxString::Format(wxT("when %.20f mTrackLen %.20f"), when, mTrackLen);
       wxASSERT_MSG(when >= 0, msg);
    }
#endif

    when = std::max(0.0, std::min(mTrackLen, when));

    auto range = EqualRange(when, 0);
    int index = range.first;

    if (index < range.second)
        // modify existing
        // In case of a discontinuity, ALWAYS CHANGING LEFT LIMIT ONLY!
        mEnv[index].SetVal(this, value);
    else
        // Add NEW
        Insert(index, EnvPoint{when, value});

    return index;
}

void Envelope::RemoveUnneededPoints
        (size_t startAt, bool rightward, bool testNeighbors)
// NOFAIL-GUARANTEE
{
    // startAt is the index of a recently inserted point which might make no
    // difference in envelope evaluation, or else might cause nearby points to
    // make no difference.

    auto isDiscontinuity = [this](size_t index) {
        // Assume array accesses are in-bounds
        const EnvPoint &point1 = mEnv[index];
        const EnvPoint &point2 = mEnv[index + 1];
        return point1.GetT() == point2.GetT() &&
               fabs(point1.GetVal() - point2.GetVal()) > VALUE_TOLERANCE;
    };

    auto remove = [this](size_t index, bool leftLimit) {
        // Assume array accesses are in-bounds
        const auto &point = mEnv[index];
        auto when = point.GetT();
        auto val = point.GetVal();
        Delete(index);  // try it to see if it's doing anything
        auto val1 = GetValueRelative(when, leftLimit);
        if (fabs(val - val1) > VALUE_TOLERANCE) {
            // put it back, we needed it
            Insert(index, EnvPoint{when, val});
            return false;
        } else
            return true;
    };

    auto len = mEnv.size();

    bool leftLimit =
            !rightward && startAt + 1 < len && isDiscontinuity(startAt);

    bool removed = remove(startAt, leftLimit);

    if (removed)
        // The given point was removable.  Done!
        return;

    if (!testNeighbors)
        return;

    // The given point was not removable.  But did its insertion make nearby
    // points removable?

    int index = startAt + (rightward ? 1 : -1);
    while (index >= 0 && index < (int) len) {
        // Stop at any discontinuity
        if (index > 0 && isDiscontinuity(index - 1))
            break;
        if ((index + 1) < (int) len && isDiscontinuity(index))
            break;

        if (!remove(index, false))
            break;

        --len;
        if (!rightward)
            --index;
    }
}

void Envelope::Delete(int point) {
    mEnv.erase(mEnv.begin() + point);
}

void Envelope::Insert(int point, const EnvPoint &p) {
    mEnv.insert(mEnv.begin() + point, p);
}

void Envelope::RescaleTimes(double newLength)
// NOFAIL-GUARANTEE
{
    if (mTrackLen == 0) {
        for (auto &point : mEnv)
            point.SetT(0);
    } else {
        auto ratio = newLength / mTrackLen;
        for (auto &point : mEnv)
            point.SetT(point.GetT() * ratio);
    }
    mTrackLen = newLength;
}

// This operation is trickier than it looks; the basic rub is that
// a track's envelope runs the range from t=0 to t=tracklen; the t=0
// envelope point applies to the first sample, but the t=tracklen
// envelope point applies one-past the last actual sample.
// t0 should be in the domain of this; if not, it is trimmed.
void Envelope::Paste(double t0, const Envelope *e, double sampleDur)
// NOFAIL-GUARANTEE
{
    const bool wasEmpty = (this->mEnv.size() == 0);
    auto otherSize = e->mEnv.size();
    const double otherDur = e->mTrackLen;
    const auto otherOffset = e->mOffset;
    const auto deltat = otherOffset + otherDur;

    if (otherSize == 0 && wasEmpty && e->mDefaultValue == this->mDefaultValue) {
        // msmeyer: The envelope is empty and has the same default value, so
        // there is nothing that must be inserted, just return. This avoids
        // the creation of unnecessary duplicate control points
        // MJS: but the envelope does get longer
        // PRL:  Assuming t0 is in the domain of the envelope
        mTrackLen += deltat;
        return;
    }

    // Make t0 relative and trim it to the domain of this
    t0 = std::min(mTrackLen, std::max(0.0, t0 - mOffset));

    // Adjust if the insertion point rounds off near a discontinuity in this
    if (true) {
        double newT0;
        auto range = EqualRange(t0, sampleDur);
        auto index = range.first;
        if (index + 2 == range.second &&
            (newT0 = mEnv[index].GetT()) == mEnv[1 + index].GetT())
            t0 = newT0;
    }

    // Open up a space
    double leftVal = e->GetValue(0);
    double rightVal = e->GetValueRelative(otherDur);
    // This range includes the right-side limit of the left end of the space,
    // and the left-side limit of the right end:
    const auto range = ExpandRegion(t0, deltat, &leftVal, &rightVal);
    // Where to put the copied points from e -- after the first of the
    // two points in range:
    auto insertAt = range.first + 1;

    // Copy points from e -- maybe skipping those at the extremes
    auto end = e->mEnv.end();
    if (otherSize != 0 && e->mEnv[otherSize - 1].GetT() == otherDur)
        // ExpandRegion already made an equivalent limit point
        --end, --otherSize;
    auto begin = e->mEnv.begin();
    if (otherSize != 0 && otherOffset == 0.0 && e->mEnv[0].GetT() == 0.0)
        ++begin, --otherSize;
    mEnv.insert(mEnv.begin() + insertAt, begin, end);

    // Adjust their times
    for (size_t index = insertAt, last = insertAt + otherSize;
         index < last; ++index) {
        auto &point = mEnv[index];
        point.SetT(point.GetT() + otherOffset + t0);
    }

    // Treat removable discontinuities
    // Right edge outward:
    RemoveUnneededPoints(insertAt + otherSize + 1, true);
    // Right edge inward:
    RemoveUnneededPoints(insertAt + otherSize, false, false);

    // Left edge inward:
    RemoveUnneededPoints(range.first, true, false);
    // Left edge outward:
    RemoveUnneededPoints(range.first - 1, false);
}

std::pair< int, int > Envelope::ExpandRegion
   ( double t0, double tlen, double *pLeftVal, double *pRightVal )
// NOFAIL-GUARANTEE
{
   // t0 is relative time

   double val = GetValueRelative( t0 );
   const auto range = EqualRange( t0, 0 );

   // Preserve the left-side limit.
   int index = 1 + range.first;
   if ( index <= range.second )
      // There is already a control point.
      ;
   else {
      // Make a control point.
      Insert( range.first, EnvPoint{ t0, val } );
   }

   // Shift points.
   auto len = mEnv.size();
   for ( unsigned int ii = index; ii < len; ++ii ) {
      auto &point = mEnv[ ii ];
      point.SetT( point.GetT() + tlen );
   }

   mTrackLen += tlen;

   // Preserve the right-side limit.
   if ( index < range.second )
      // There was a control point already.
      ;
   else
      // Make a control point.
      Insert( index, EnvPoint{ t0 + tlen, val } );

   // Make discontinuities at ends, maybe:

   if ( pLeftVal )
      // Make a discontinuity at the left side of the expansion
      Insert( index++, EnvPoint{ t0, *pLeftVal } );

   if ( pRightVal )
      // Make a discontinuity at the right side of the expansion
      Insert( index++, EnvPoint{ t0 + tlen, *pRightVal } );

   // Return the range of indices that includes the inside limiting points,
   // none, one, or two
   return { 1 + range.first, index };
}

inline void EnvPoint::SetVal( Envelope *pEnvelope, double val )
{
   if ( pEnvelope )
      val = pEnvelope->ClampValue(val);
   mVal = val;
}
