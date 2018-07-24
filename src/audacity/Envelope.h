/**********************************************************************

  Audacity: A Digital Audio Editor

  Envelope.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_ENVELOPE__
#define __AUDACITY_ENVELOPE__

#include <stdlib.h>
#include <algorithm>
#include <vector>

class Envelope;

class EnvPoint {

public:
    EnvPoint() {}

    inline EnvPoint(double t, double val) : mT{t}, mVal{val} {}

    double GetT() const { return mT; }

    void SetT(double t) { mT = t; }

    double GetVal() const { return mVal; }

    inline void SetVal(Envelope *pEnvelope, double val);

private:
    double mT{};
    double mVal{};

};


typedef std::vector<EnvPoint> EnvArray;

class Envelope {
public:
    // Envelope can define a piecewise linear function, or piecewise exponential.
    Envelope(bool exponential, double minValue, double maxValue, double defaultValue);

    Envelope(const Envelope &orig);

    // Create from a subrange of another envelope.
    Envelope(const Envelope &orig, double t0, double t1);

    void Initialize(int numPoints);

    /** \brief Return number of points */
    size_t GetNumberOfPoints() const;

    virtual ~Envelope();

    // Accessors
    /** \brief Get envelope value at time t */
    double GetValue(double t, double sampleDur = 0) const;

    /** \brief Get many envelope points at once.
     *
     * This is much faster than calling GetValue() multiple times if you need
     * more than one value in a row. */
    void GetValues(double *buffer, int len, double t0, double tstep) const;

    void CopyRange(const Envelope &orig, size_t begin, size_t end);

    double ClampValue(double value) { return std::max(mMinValue, std::min(mMaxValue, value)); }

    void SetTrackLen(double trackLen, double sampleDur = 0.0);


    std::pair<int, int> EqualRange(double when, double sampleDur) const;

    double GetValueRelative(double t, bool leftLimit = false) const;

    void GetValuesRelative
            (double *buffer, int len, double t0, double tstep, bool leftLimit = false) const;

    void AddPointAtEnd(double t, double val);

    // relative time
    void BinarySearchForTime(int &Lo, int &Hi, double t) const;

    void BinarySearchForTime_LeftLimit(int &Lo, int &Hi, double t) const;

    double GetInterpolationStartValueAtPoint(int iPoint) const;

    // Handling Cut/Copy/Paste events
    // sampleDur determines when the endpoint of the collapse is near enough
    // to an endpoint of the domain, that an extra control point is not needed.
    void CollapseRegion(double t0, double t1, double sampleDur);

    int InsertOrReplaceRelative(double when, double value);

    void RemoveUnneededPoints
            (size_t startAt, bool rightward, bool testNeighbors = true);

    /** \brief DELETE a point by its position in array */
    void Delete(int point);

    /** \brief insert a point */
    void Insert(int point, const EnvPoint &p);

    void RescaleTimes(double newLength);

    // Envelope has no notion of rate and control point times are not quantized;
    // but a tolerance is needed in the Paste routine, and better to inform it
    // of an appropriate number, than use hidden arbitrary constants.
    void Paste(double t0, const Envelope *e, double sampleDur);

private:

    // The list of envelope control points.
    EnvArray mEnv;
    /** \brief The length of the envelope, which is the same as the length of the
     * underlying track (normally) */
    double mTrackLen{0.0};
    bool mDB;
    double mMinValue, mMaxValue;
    double mDefaultValue;
    mutable int mSearchGuess{-2};

    /** \brief The time at which the envelope starts, i.e. the start offset */
    double mOffset{0.0};

    /** \brief Accessor for points */
    const EnvPoint &operator[](int index) const {
        return mEnv[index];
    }
   std::pair< int, int > ExpandRegion
      ( double t0, double tlen, double *pLeftVal, double *pRightVal );

   // Return true if violations of point ordering invariants were detected
   // and repaired
   bool ConsistencyCheck();

};

#endif
