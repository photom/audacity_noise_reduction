/**********************************************************************

  Audacity: A Digital Audio Editor

  NoiseReduction.cpp

  Dominic Mazzoni

  detailed rewriting by
  Paul Licameli

*******************************************************************//**

\class EffectNoiseReduction
\brief A two-pass effect to reduce background noise.

  The first pass is done over just noise.  For each windowed sample
  of the sound, we take a FFT and then statistics are tabulated for
  each frequency band.

  During the noise reduction phase, we start by setting a gain control
  for each frequency band such that if the sound has exceeded the
  previously-determined threshold, the gain is set to 0 dB, otherwise
  the gain is set lower (e.g. -18 dB), to suppress the noise.
  Then time-smoothing is applied so that the gain for each frequency
  band moves slowly, and then frequency-smoothing is applied so that a
  single frequency is never suppressed or boosted in isolation.
  Lookahead is employed; this effect is not designed for real-time
  but if it were, there would be a significant delay.

  The gain controls are applied to the complex FFT of the signal,
  and then the inverse FFT is applied.  A Hanning window may be
  applied (depending on the advanced window types setting), and then
  the output signal is then pieced together using overlap/add.

*//****************************************************************//**
*/

#include <stdlib.h>
#include <stdio.h>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstring>

#include "Audacity.h"
#include "Types.h"
#include "RealFFTf.h"
#include "NoiseReduction.h"
#include "WaveTrack.h"

// SPECTRAL_SELECTION not to affect this effect for now, as there might be no indication that it does.
// [Discussed and agreed for v2.1 by Steve, Paul, Bill].
#undef EXPERIMENTAL_SPECTRAL_EDITING

typedef std::vector<float> FloatVector;

// Define both of these to make the radio button three-way
#define RESIDUE_CHOICE
//#define ISOLATE_CHOICE

// Define for Attack and release controls.
// #define ATTACK_AND_RELEASE

// Define to expose other advanced, experimental dialog controls
//#define ADVANCED_SETTINGS

// Define to make the old statistical methods an available choice
//#define OLD_METHOD_AVAILABLE

namespace {

enum DiscriminationMethod {
    DM_MEDIAN,
    DM_SECOND_GREATEST,
    DM_OLD_METHOD,

    DM_N_METHODS,
    DM_DEFAULT_METHOD = DM_SECOND_GREATEST,
};

// magic number used only in the old statistics
// and the old discrimination
const float minSignalTime = 0.05f;

enum WindowTypes {
    WT_RECTANGULAR_HANN = 0, // 2.0.6 behavior, requires 1/2 step
    WT_HANN_RECTANGULAR, // requires 1/2 step
    WT_HANN_HANN,        // requires 1/4 step
    WT_BLACKMAN_HANN,     // requires 1/4 step
    WT_HAMMING_RECTANGULAR, // requires 1/2 step
    WT_HAMMING_HANN, // requires 1/4 step
    WT_HAMMING_INV_HAMMING, // requires 1/2 step

    WT_N_WINDOW_TYPES,
    WT_DEFAULT_WINDOW_TYPES = WT_HANN_HANN
};

const struct WindowTypesInfo {
    const std::string name;
    unsigned minSteps;
    double inCoefficients[3];
    double outCoefficients[3];
    double productConstantTerm;
} windowTypesInfo[WT_N_WINDOW_TYPES] = {
        // In all of these cases (but the last), the constant term of the product of windows
        // is the product of the windows' two constant terms,
        // plus one half the product of the first cosine coefficients.

        // Experimental only, don't need translations
        {"none, Hann (2.0.6 behavior)", 2, {1,    0,     0},    {0.5, -0.5, 0}, 0.5},
        {"Hann, none",                  2, {0.5,  -0.5,  0},    {1,   0,    0}, 0.5},
        {"Hann, Hann (default)",        4, {0.5,  -0.5,  0},    {0.5, -0.5, 0}, 0.375},
        {"Blackman, Hann",              4, {0.42, -0.5,  0.08}, {0.5, -0.5, 0}, 0.335},
        {"Hamming, none",               2, {0.54, -0.46, 0.0},  {1,   0,    0}, 0.54},
        {"Hamming, Hann",               4, {0.54, -0.46, 0.0},  {0.5, -0.5, 0}, 0.385},
        {"Hamming, Reciprocal Hamming", 2, {0.54, -0.46, 0.0},  {1,   0,    0}, 1.0}, // output window is special
};

enum {
    DEFAULT_WINDOW_SIZE_CHOICE = 8, // corresponds to 2048
    DEFAULT_STEPS_PER_WINDOW_CHOICE = 1 // corresponds to 4, minimum for WT_HANN_HANN
};

enum NoiseReductionChoice {
    NRC_REDUCE_NOISE,
    NRC_ISOLATE_NOISE,
    NRC_LEAVE_RESIDUE,
};

} // namespace

//----------------------------------------------------------------------------
// EffectNoiseReduction::Statistics
//----------------------------------------------------------------------------

class EffectNoiseReduction::Statistics {
public:
    Statistics(size_t spectrumSize, double rate, int windowTypes)
            : mRate(rate), mWindowSize((spectrumSize - 1) * 2), mWindowTypes(windowTypes), mTotalWindows(0),
              mTrackWindows(0), mSums(spectrumSize), mMeans(spectrumSize)
#ifdef OLD_METHOD_AVAILABLE
    , mNoiseThreshold(spectrumSize)
#endif
    {}

    // Noise profile statistics follow

    double mRate; // Rate of profile track(s) -- processed tracks must match
    size_t mWindowSize;
    int mWindowTypes;

    int mTotalWindows;
    int mTrackWindows;
    FloatVector mSums;
    FloatVector mMeans;

#ifdef OLD_METHOD_AVAILABLE
    // Old statistics:
    FloatVector mNoiseThreshold;
#endif
};

//----------------------------------------------------------------------------
// EffectNoiseReduction::Settings
//----------------------------------------------------------------------------

// This object is the memory of the effect between uses
// (other than noise profile statistics)
class EffectNoiseReduction::Settings {
public:
    Settings();

    ~Settings() {}

    bool PrefsIO(bool read);

    bool Validate(EffectNoiseReduction *effect) const;

    size_t WindowSize() const { return 1u << (3 + mWindowSizeChoice); }

    unsigned StepsPerWindow() const { return 1u << (1 + mStepsPerWindowChoice); }

    bool mDoProfile;

    // Stored in preferences:

    // Basic:
    double mNewSensitivity;   // - log10 of a probability... yeah.
    double mFreqSmoothingBands; // really an integer
    double mNoiseGain;         // in dB, positive
    double mAttackTime;        // in secs
    double mReleaseTime;       // in secs

    // Advanced:
    double mOldSensitivity;    // in dB, plus or minus

    // Basic:
    int mNoiseReductionChoice;

    // Advanced:
    int mWindowTypes;
    int mWindowSizeChoice;
    int mStepsPerWindowChoice;
    int mMethod;
};

EffectNoiseReduction::Settings::Settings()
        : mDoProfile(true) {
    PrefsIO(true);
}

//----------------------------------------------------------------------------
// EffectNoiseReduction::Worker
//----------------------------------------------------------------------------

// This object holds information needed only during effect calculation
class EffectNoiseReduction::Worker {
public:
    typedef EffectNoiseReduction::Settings Settings;
    typedef EffectNoiseReduction::Statistics Statistics;

    Worker(const Settings &settings, double sampleRate
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
            , double f0, double f1
#endif
    );

    ~Worker();

    bool Process(EffectNoiseReduction &effect, WaveTrack *track,
                 Statistics &statistics, TrackFactory &factory, double mT0, double mT1);

private:
    bool ProcessOne(EffectNoiseReduction &effect,
                    Statistics &statistics, TrackFactory &factory,
                    int count, WaveTrack *track,
                    sampleCount start, sampleCount len);

    void StartNewTrack();

    void ProcessSamples(Statistics &statistics,
                        WaveTrack *outputTrack, size_t len, float *buffer);

    void FillFirstHistoryWindow();

    void ApplyFreqSmoothing(FloatVector &gains);

    void GatherStatistics(Statistics &statistics);

    inline bool Classify(const Statistics &statistics, int band);

    void ReduceNoise(const Statistics &statistics, WaveTrack *outputTrack);

    void RotateHistoryWindows();

    void FinishTrackStatistics(Statistics &statistics);

    void FinishTrack(Statistics &statistics, WaveTrack *outputTrack);

private:

    const bool mDoProfile;

    const double mSampleRate;

    const size_t mWindowSize;
    // These have that size:
    HFFT hFFT;
    FloatVector mFFTBuffer;
    FloatVector mInWaveBuffer;
    FloatVector mOutOverlapBuffer;
    // These have that size, or 0:
    FloatVector mInWindow;
    FloatVector mOutWindow;

    const size_t mSpectrumSize;
    FloatVector mFreqSmoothingScratch;
    const size_t mFreqSmoothingBins;
    // When spectral selection limits the affected band:
    int mBinLow;  // inclusive lower bound
    int mBinHigh; // exclusive upper bound

    const int mNoiseReductionChoice;
    const unsigned mStepsPerWindow;
    const size_t mStepSize;
    const int mMethod;
    const double mNewSensitivity;


    sampleCount mInSampleCount;
    sampleCount mOutStepCount;
    int mInWavePos;

    float mOneBlockAttack;
    float mOneBlockRelease;
    float mNoiseAttenFactor;
    float mOldSensitivityFactor;

    unsigned mNWindowsToExamine;
    unsigned mCenter;
    unsigned mHistoryLen;

    struct Record {
        Record(size_t spectrumSize)
                : mSpectrums(spectrumSize), mGains(spectrumSize), mRealFFTs(spectrumSize - 1),
                  mImagFFTs(spectrumSize - 1) {
        }

        FloatVector mSpectrums;
        FloatVector mGains;
        FloatVector mRealFFTs;
        FloatVector mImagFFTs;
    };

    std::vector<std::unique_ptr<Record>> mQueue;
};

EffectNoiseReduction::EffectNoiseReduction()
        : mSettings(std::make_unique<EffectNoiseReduction::Settings>()) {
    Init();
}

bool EffectNoiseReduction::Init() {
    return true;
}

EffectNoiseReduction::~EffectNoiseReduction() {
}

namespace {
template<typename StructureType, typename FieldType>
struct PrefsTableEntry {
    typedef FieldType (StructureType::*MemberPointer);

    MemberPointer field;
    const std::string name;
    FieldType defaultValue;
};
}

bool EffectNoiseReduction::Settings::PrefsIO(bool read) {
    static const double DEFAULT_OLD_SENSITIVITY = 0.0;

    static const PrefsTableEntry<Settings, double> doubleTable[] = {
            {&Settings::mNewSensitivity,     "Sensitivity",    6.0},
            {&Settings::mNoiseGain,          "Gain",           12.0},
            {&Settings::mAttackTime,         "AttackTime",     0.02},
            {&Settings::mReleaseTime,        "ReleaseTime",    0.10},
            {&Settings::mFreqSmoothingBands, "FreqSmoothing",  3.0},

            // Advanced settings
            {&Settings::mOldSensitivity,     "OldSensitivity", DEFAULT_OLD_SENSITIVITY},
    };
    static int doubleTableSize = sizeof(doubleTable) / sizeof(doubleTable[0]);

    static const PrefsTableEntry<Settings, int> intTable[] = {
            {&Settings::mNoiseReductionChoice, "ReductionChoice", NRC_REDUCE_NOISE},

            // Advanced settings
            {&Settings::mWindowTypes,          "WindowTypes",     WT_DEFAULT_WINDOW_TYPES},
            {&Settings::mWindowSizeChoice,     "WindowSize",      DEFAULT_WINDOW_SIZE_CHOICE},
            {&Settings::mStepsPerWindowChoice, "StepsPerWindow",  DEFAULT_STEPS_PER_WINDOW_CHOICE},
            {&Settings::mMethod,               "Method",          DM_DEFAULT_METHOD},
    };
    static int intTableSize = sizeof(intTable) / sizeof(intTable[0]);

    static const std::string prefix("/Effects/NoiseReduction/");

    if (read) {
        mNewSensitivity = doubleTable[0].defaultValue;
        mNoiseGain = doubleTable[1].defaultValue;
        mAttackTime = doubleTable[2].defaultValue;
        mReleaseTime = doubleTable[3].defaultValue;
        mFreqSmoothingBands = doubleTable[4].defaultValue;
        mOldSensitivity = doubleTable[5].defaultValue;

        // Ignore preferences for unavailable options.
#ifndef RESIDUE_CHOICE
        if (mNoiseReductionChoice == NRC_LEAVE_RESIDUE)
           mNoiseReductionChoice = NRC_ISOLATE_NOISE;
#endif

#ifndef ADVANCED_SETTINGS
        // Initialize all hidden advanced settings to defaults.
        mWindowTypes = WT_DEFAULT_WINDOW_TYPES;
        mWindowSizeChoice = DEFAULT_WINDOW_SIZE_CHOICE;
        mStepsPerWindowChoice = DEFAULT_STEPS_PER_WINDOW_CHOICE;
        mMethod = DM_DEFAULT_METHOD;
        mOldSensitivity = DEFAULT_OLD_SENSITIVITY;
#endif

#ifndef OLD_METHOD_AVAILABLE
        if (mMethod == DM_OLD_METHOD)
            mMethod = DM_DEFAULT_METHOD;
#endif

        return true;
    } else {
        return true;
    }
}

bool EffectNoiseReduction::Settings::Validate(EffectNoiseReduction *effect) const {
    if (StepsPerWindow() < windowTypesInfo[mWindowTypes].minSteps) {
        std::cerr << "Steps per block are too few for the window types." << std::endl;
        return false;
    }

    if (StepsPerWindow() > WindowSize()) {
        std::cerr << "Steps per block cannot exceed the window size." << std::endl;
        return false;
    }

    if (mMethod == DM_MEDIAN && StepsPerWindow() > 4) {
        std::cerr << "Median method is not implemented for more than four steps per window." << std::endl;
        return false;
    }

    return true;
}

bool EffectNoiseReduction::GetProfile(WaveTrack *track, double t0, double t1,
                                      double noiseGain, double sensitivity, double freqSmoothingBands,
                                      TrackFactory *factory) {
    mFactory = factory;
    mSettings->mDoProfile = true;
    mSettings->mFreqSmoothingBands = freqSmoothingBands;
    mSettings->mNoiseGain = noiseGain;
    mSettings->mNewSensitivity = sensitivity;

    double duration = 0.0;
    mT0 = t0;
    mT1 = t1;
    if (mT1 > mT0) {
        // there is a selection: let's fit in there...
        // MJS: note that this is just for the TTC and is independent of the track rate
        // but we do need to make sure we have the right number of samples at the project rate
        double quantMT0 = QUANTIZED_TIME(mT0, track->GetRate());
        double quantMT1 = QUANTIZED_TIME(mT1, track->GetRate());
        duration = quantMT1 - quantMT0;
        mT1 = mT0 + duration;
    }

    // Note: Init may read parameters from preferences
    if (!Init()) {
        return false;
    }

    return Process(track);
}

bool
EffectNoiseReduction::ReduceNoise(WaveTrack *track, double noiseGain, double sensitivity, double freqSmoothingBands,
                                  TrackFactory *factory) {
    mFactory = factory;
    mSettings->mDoProfile = false;
    mSettings->mFreqSmoothingBands = freqSmoothingBands;
    mSettings->mNoiseGain = noiseGain;
    mSettings->mNewSensitivity = sensitivity;

    mT0 = track->GetStartTime();
    mT1 = track->GetEndTime();
    double duration = 0.0;
    if (mT1 > mT0) {
        // there is a selection: let's fit in there...
        // MJS: note that this is just for the TTC and is independent of the track rate
        // but we do need to make sure we have the right number of samples at the project rate
        double quantMT0 = QUANTIZED_TIME(mT0, track->GetRate());
        double quantMT1 = QUANTIZED_TIME(mT1, track->GetRate());
        duration = quantMT1 - quantMT0;
        mT1 = mT0 + duration;
    }

    return Process(track);
}

bool EffectNoiseReduction::Process(WaveTrack *track) {
    // Initialize statistics if gathering them, or check for mismatched (advanced)
    // settings if reducing noise.
    if (mSettings->mDoProfile) {
        size_t spectrumSize = 1 + mSettings->WindowSize() / 2;
        mStatistics = std::make_unique<Statistics>
                (spectrumSize, track->GetRate(), mSettings->mWindowTypes);
    } else if (mStatistics->mWindowSize != mSettings->WindowSize()) {
        // possible only with advanced settings
        std::cerr << "You must specify the same window size for steps 1 and 2." << std::endl;
        return false;
    } else if (mStatistics->mWindowTypes != mSettings->mWindowTypes) {
        // A warning only
        std::cerr << "Warning: window types are not the same as for profiling." << std::endl;
    }

    Worker worker(*mSettings, mStatistics->mRate
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
            , mF0, mF1
#endif
    );
    bool bGoodResult = worker.Process(*this, track, *mStatistics, *mFactory, mT0, mT1);
    if (mSettings->mDoProfile) {
        if (bGoodResult)
            mSettings->mDoProfile = false; // So that "repeat last effect" will reduce noise
        else
            mStatistics.reset(); // So that profiling must be done again before noise reduction
    }

    return bGoodResult;
}

EffectNoiseReduction::Worker::~Worker() {
}

bool EffectNoiseReduction::Worker::Process
        (EffectNoiseReduction &effect, WaveTrack *track, Statistics &statistics,
         TrackFactory &factory, double inT0, double inT1) {
    int count = 0;
    if (track->GetRate() != mSampleRate) {
        if (mDoProfile)
            std::cerr << "All noise profile data must have the same sample rate." << std::endl;
        else
            std::cerr << "The sample rate of the noise profile must match that of the sound to be processed."
                      << std::endl;
        return false;
    }

    double trackStart = track->GetStartTime();
    double trackEnd = track->GetEndTime();
    double t0 = std::max(trackStart, inT0);
    double t1 = std::min(trackEnd, inT1);

    if (t1 > t0) {
        auto start = track->TimeToLongSamples(t0);
        auto end = track->TimeToLongSamples(t1);
        auto len = end - start;

        if (!ProcessOne(effect, statistics, factory,
                        count, track, start, len))
            return false;
    }
    ++count;

    if (mDoProfile) {
        if (statistics.mTotalWindows == 0) {
            std::cerr << "Selected noise profile is too short." << std::endl;
            return false;
        }
    }

    return true;
}

void EffectNoiseReduction::Worker::ApplyFreqSmoothing(FloatVector &gains) {
    // Given an array of gain mutipliers, average them
    // GEOMETRICALLY.  Don't multiply and take nth root --
    // that may quickly cause underflows.  Instead, average the logs.

    if (mFreqSmoothingBins == 0)
        return;

    {
        float *pScratch = &mFreqSmoothingScratch[0];
        std::fill(pScratch, pScratch + mSpectrumSize, 0.0f);
    }

    for (size_t ii = 0; ii < mSpectrumSize; ++ii)
        gains[ii] = log(gains[ii]);

    // ii must be signed
    for (int ii = 0; ii < (int) mSpectrumSize; ++ii) {
        const int j0 = std::max(0, ii - (int) mFreqSmoothingBins);
        const int j1 = std::min(mSpectrumSize - 1, ii + mFreqSmoothingBins);
        for (int jj = j0; jj <= j1; ++jj) {
            mFreqSmoothingScratch[ii] += gains[jj];
        }
        mFreqSmoothingScratch[ii] /= (j1 - j0 + 1);
    }

    for (size_t ii = 0; ii < mSpectrumSize; ++ii)
        gains[ii] = exp(mFreqSmoothingScratch[ii]);
}

EffectNoiseReduction::Worker::Worker
        (const Settings &settings, double sampleRate
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
        , double f0, double f1
#endif
)
        : mDoProfile(settings.mDoProfile), mSampleRate(sampleRate), mWindowSize(settings.WindowSize()),
          hFFT(GetFFT(mWindowSize)), mFFTBuffer(mWindowSize), mInWaveBuffer(mWindowSize),
          mOutOverlapBuffer(mWindowSize), mInWindow(), mOutWindow(), mSpectrumSize(1 + mWindowSize / 2),
          mFreqSmoothingScratch(mSpectrumSize), mFreqSmoothingBins((int) (settings.mFreqSmoothingBands)), mBinLow(0),
          mBinHigh(mSpectrumSize), mNoiseReductionChoice(settings.mNoiseReductionChoice),
          mStepsPerWindow(settings.StepsPerWindow()), mStepSize(mWindowSize / mStepsPerWindow),
          mMethod(settings.mMethod)

// Sensitivity setting is a base 10 log, turn it into a natural log
        , mNewSensitivity(settings.mNewSensitivity * log(10.0)), mInSampleCount(0), mOutStepCount(0), mInWavePos(0) {
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
    {
       const double bin = mSampleRate / mWindowSize;
       if (f0 >= 0.0 )
          mBinLow = floor(f0 / bin);
       if (f1 >= 0.0)
          mBinHigh = ceil(f1 / bin);
    }
#endif

    const double noiseGain = -settings.mNoiseGain;
    const unsigned nAttackBlocks = 1 + (int) (settings.mAttackTime * sampleRate / mStepSize);
    const unsigned nReleaseBlocks = 1 + (int) (settings.mReleaseTime * sampleRate / mStepSize);
    // Applies to amplitudes, divide by 20:
    mNoiseAttenFactor = DB_TO_LINEAR(noiseGain);
    // Apply to gain factors which apply to amplitudes, divide by 20:
    mOneBlockAttack = DB_TO_LINEAR(noiseGain / nAttackBlocks);
    mOneBlockRelease = DB_TO_LINEAR(noiseGain / nReleaseBlocks);
    // Applies to power, divide by 10:
    mOldSensitivityFactor = pow(10.0, settings.mOldSensitivity / 10.0);

    mNWindowsToExamine = (mMethod == DM_OLD_METHOD)
                         ? std::max(2, (int) (minSignalTime * sampleRate / mStepSize))
                         : 1 + mStepsPerWindow;

    mCenter = mNWindowsToExamine / 2;
    assert(mCenter >= 1); // release depends on this assumption

    if (mDoProfile)
#ifdef OLD_METHOD_AVAILABLE
        mHistoryLen = mNWindowsToExamine;
#else
        mHistoryLen = 1;
#endif
    else {
        // Allow long enough queue for sufficient inspection of the middle
        // and for attack processing
        // See ReduceNoise()
        mHistoryLen = std::max(mNWindowsToExamine, mCenter + nAttackBlocks);
    }

    mQueue.resize(mHistoryLen);
    for (unsigned ii = 0; ii < mHistoryLen; ++ii)
        mQueue[ii] = std::make_unique<Record>(mSpectrumSize);

    // Create windows

    const double constantTerm =
            windowTypesInfo[settings.mWindowTypes].productConstantTerm;

    // One or the other window must by multiplied by this to correct for
    // overlap.  Must scale down as steps get smaller, and overlaps larger.
    const double multiplier = 1.0 / (constantTerm * mStepsPerWindow);

    // Create the analysis window
    switch (settings.mWindowTypes) {
        case WT_RECTANGULAR_HANN:
            break;
        default: {
            const bool rectangularOut =
                    settings.mWindowTypes == WT_HAMMING_RECTANGULAR ||
                    settings.mWindowTypes == WT_HANN_RECTANGULAR;
            const double m =
                    rectangularOut ? multiplier : 1;
            const double *const coefficients =
                    windowTypesInfo[settings.mWindowTypes].inCoefficients;
            const double c0 = coefficients[0];
            const double c1 = coefficients[1];
            const double c2 = coefficients[2];
            mInWindow.resize(mWindowSize);
            for (size_t ii = 0; ii < mWindowSize; ++ii)
                mInWindow[ii] = m *
                                (c0 + c1 * cos((2.0 * M_PI * ii) / mWindowSize)
                                 + c2 * cos((4.0 * M_PI * ii) / mWindowSize));
        }
            break;
    }

    if (!mDoProfile) {
        // Create the synthesis window
        switch (settings.mWindowTypes) {
            case WT_HANN_RECTANGULAR:
            case WT_HAMMING_RECTANGULAR:
                break;
            case WT_HAMMING_INV_HAMMING: {
                mOutWindow.resize(mWindowSize);
                for (size_t ii = 0; ii < mWindowSize; ++ii)
                    mOutWindow[ii] = multiplier / mInWindow[ii];
            }
                break;
            default: {
                const double *const coefficients =
                        windowTypesInfo[settings.mWindowTypes].outCoefficients;
                const double c0 = coefficients[0];
                const double c1 = coefficients[1];
                const double c2 = coefficients[2];
                mOutWindow.resize(mWindowSize);
                for (size_t ii = 0; ii < mWindowSize; ++ii)
                    mOutWindow[ii] = multiplier *
                                     (c0 + c1 * cos((2.0 * M_PI * ii) / mWindowSize)
                                      + c2 * cos((4.0 * M_PI * ii) / mWindowSize));
            }
                break;
        }
    }
}

void EffectNoiseReduction::Worker::StartNewTrack() {
    float *pFill;
    for (unsigned ii = 0; ii < mHistoryLen; ++ii) {
        Record &record = *mQueue[ii];

        pFill = &record.mSpectrums[0];
        std::fill(pFill, pFill + mSpectrumSize, 0.0f);

        pFill = &record.mRealFFTs[0];
        std::fill(pFill, pFill + mSpectrumSize - 1, 0.0f);

        pFill = &record.mImagFFTs[0];
        std::fill(pFill, pFill + mSpectrumSize - 1, 0.0f);

        pFill = &record.mGains[0];
        std::fill(pFill, pFill + mSpectrumSize, mNoiseAttenFactor);
    }

    pFill = &mOutOverlapBuffer[0];
    std::fill(pFill, pFill + mWindowSize, 0.0f);

    pFill = &mInWaveBuffer[0];
    std::fill(pFill, pFill + mWindowSize, 0.0f);

    if (mDoProfile) {
        // We do not want leading zero padded windows
        mInWavePos = 0;
        mOutStepCount = -(int) (mHistoryLen - 1);
    } else {
        // So that the queue gets primed with some windows,
        // zero-padded in front, the first having mStepSize
        // samples of wave data:
        mInWavePos = mWindowSize - mStepSize;
        // This starts negative, to count up until the queue fills:
        mOutStepCount = -(int) (mHistoryLen - 1)
                        // ... and then must pass over the padded windows,
                        // before the first full window:
                        - (int) (mStepsPerWindow - 1);
    }

    mInSampleCount = 0;
}

void EffectNoiseReduction::Worker::ProcessSamples
        (Statistics &statistics, WaveTrack *outputTrack,
         size_t len, float *buffer) {
    while (len && mOutStepCount * mStepSize < mInSampleCount) {
        auto avail = std::min(len, mWindowSize - mInWavePos);
        memmove(&mInWaveBuffer[mInWavePos], buffer, avail * sizeof(float));
        buffer += avail;
        len -= avail;
        mInWavePos += avail;

        if (mInWavePos == (int) mWindowSize) {
            FillFirstHistoryWindow();
            if (mDoProfile)
                GatherStatistics(statistics);
            else
                ReduceNoise(statistics, outputTrack);
            ++mOutStepCount;
            RotateHistoryWindows();

            // Rotate for overlap-add
            memmove(&mInWaveBuffer[0], &mInWaveBuffer[mStepSize],
                    (mWindowSize - mStepSize) * sizeof(float));
            mInWavePos -= mStepSize;
        }
    }
}

void EffectNoiseReduction::Worker::FillFirstHistoryWindow() {
    // Transform samples to frequency domain, windowed as needed
    if (mInWindow.size() > 0)
        for (size_t ii = 0; ii < mWindowSize; ++ii)
            mFFTBuffer[ii] = mInWaveBuffer[ii] * mInWindow[ii];
    else
        memmove(&mFFTBuffer[0], &mInWaveBuffer[0], mWindowSize * sizeof(float));
    RealFFTf(&mFFTBuffer[0], hFFT.get());

    Record &record = *mQueue[0];

    // Store real and imaginary parts for later inverse FFT, and compute
    // power
    {
        float *pReal = &record.mRealFFTs[1];
        float *pImag = &record.mImagFFTs[1];
        float *pPower = &record.mSpectrums[1];
        int *pBitReversed = &hFFT->BitReversed[1];
        const auto last = mSpectrumSize - 1;
        for (unsigned int ii = 1; ii < last; ++ii) {
            const int kk = *pBitReversed++;
            const float realPart = *pReal++ = mFFTBuffer[kk];
            const float imagPart = *pImag++ = mFFTBuffer[kk + 1];
            *pPower++ = realPart * realPart + imagPart * imagPart;
        }
        // DC and Fs/2 bins need to be handled specially
        const float dc = mFFTBuffer[0];
        record.mRealFFTs[0] = dc;
        record.mSpectrums[0] = dc * dc;

        const float nyquist = mFFTBuffer[1];
        record.mImagFFTs[0] = nyquist; // For Fs/2, not really imaginary
        record.mSpectrums[last] = nyquist * nyquist;
    }

    if (mNoiseReductionChoice != NRC_ISOLATE_NOISE) {
        // Default all gains to the reduction factor,
        // until we decide to raise some of them later
        float *pGain = &record.mGains[0];
        std::fill(pGain, pGain + mSpectrumSize, mNoiseAttenFactor);
    }
}

void EffectNoiseReduction::Worker::RotateHistoryWindows() {
    std::rotate(mQueue.begin(), mQueue.end() - 1, mQueue.end());
}

void EffectNoiseReduction::Worker::FinishTrackStatistics(Statistics &statistics) {
    const int windows = statistics.mTrackWindows;
    const int multiplier = statistics.mTotalWindows;
    const int denom = windows + multiplier;

    // Combine averages in case of multiple profile tracks.
    if (windows)
        for (int ii = 0, nn = statistics.mMeans.size(); ii < nn; ++ii) {
            float &mean = statistics.mMeans[ii];
            float &sum = statistics.mSums[ii];
            mean = (mean * multiplier + sum) / denom;
            // Reset for next track
            sum = 0;
        }

    // Reset for next track
    statistics.mTrackWindows = 0;
    statistics.mTotalWindows = denom;
}

void EffectNoiseReduction::Worker::FinishTrack
        (Statistics &statistics, WaveTrack *outputTrack) {
    // Keep flushing empty input buffers through the history
    // windows until we've output exactly as many samples as
    // were input.
    // Well, not exactly, but not more than one step-size of extra samples
    // at the end.
    // We'll DELETE them later in ProcessOne.

    FloatVector empty(mStepSize);

    while (mOutStepCount * mStepSize < mInSampleCount) {
        ProcessSamples(statistics, outputTrack, mStepSize, &empty[0]);
    }
}

void EffectNoiseReduction::Worker::GatherStatistics(Statistics &statistics) {
    ++statistics.mTrackWindows;

    {
        // NEW statistics
        const float *pPower = &mQueue[0]->mSpectrums[0];
        float *pSum = &statistics.mSums[0];
        for (size_t jj = 0; jj < mSpectrumSize; ++jj) {
            *pSum++ += *pPower++;
        }
    }

#ifdef OLD_METHOD_AVAILABLE
    // The noise threshold for each frequency is the maximum
    // level achieved at that frequency for a minimum of
    // mMinSignalBlocks blocks in a row - the max of a min.

    auto finish = mHistoryLen;

    {
       // old statistics
       const float *pPower = &mQueue[0]->mSpectrums[0];
       float *pThreshold = &statistics.mNoiseThreshold[0];
       for (int jj = 0; jj < mSpectrumSize; ++jj) {
          float min = *pPower++;
          for (unsigned ii = 1; ii < finish; ++ii)
             min = std::min(min, mQueue[ii]->mSpectrums[jj]);
          *pThreshold = std::max(*pThreshold, min);
          ++pThreshold;
       }
    }
#endif
}

// Return true iff the given band of the "center" window looks like noise.
// Examine the band in a few neighboring windows to decide.
inline
bool EffectNoiseReduction::Worker::Classify(const Statistics &statistics, int band) {
    switch (mMethod) {
#ifdef OLD_METHOD_AVAILABLE
        case DM_OLD_METHOD:
           {
              float min = mQueue[0]->mSpectrums[band];
              for (unsigned ii = 1; ii < mNWindowsToExamine; ++ii)
                 min = std::min(min, mQueue[ii]->mSpectrums[band]);
              return min <= mOldSensitivityFactor * statistics.mNoiseThreshold[band];
           }
#endif
        // New methods suppose an exponential distribution of power values
        // in the noise; NEW sensitivity is meant to be log of probability
        // that noise strays above the threshold.  Call that probability
        // 1 - F.  The quantile function of an exponential distribution is
        // log (1 - F) * mean.  Thus simply multiply mean by sensitivity
        // to get the threshold.
        case DM_MEDIAN:
            // This method examines the window and all windows
            // that partly overlap it, and takes a median, to
            // avoid being fooled by up and down excursions into
            // either the mistake of classifying noise as not noise
            // (leaving a musical noise chime), or the opposite
            // (distorting the signal with a drop out).
            if (mNWindowsToExamine == 3)
                // No different from second greatest.
                goto secondGreatest;
            else if (mNWindowsToExamine == 5) {
                float greatest = 0.0, second = 0.0, third = 0.0;
                for (unsigned ii = 0; ii < mNWindowsToExamine; ++ii) {
                    const float power = mQueue[ii]->mSpectrums[band];
                    if (power >= greatest)
                        third = second, second = greatest, greatest = power;
                    else if (power >= second)
                        third = second, second = power;
                    else if (power >= third)
                        third = power;
                }
                return third <= mNewSensitivity * statistics.mMeans[band];
            } else {
                return true;
            }
        secondGreatest:
        case DM_SECOND_GREATEST: {
            // This method just throws out the high outlier.  It
            // should be less prone to distortions and more prone to
            // chimes.
            float greatest = 0.0, second = 0.0;
            for (unsigned ii = 0; ii < mNWindowsToExamine; ++ii) {
                const float power = mQueue[ii]->mSpectrums[band];
                if (power >= greatest)
                    second = greatest, greatest = power;
                else if (power >= second)
                    second = power;
            }
            return second <= mNewSensitivity * statistics.mMeans[band];
        }
        default:
            assert(false);
            return true;
    }

}

void EffectNoiseReduction::Worker::ReduceNoise
        (const Statistics &statistics, WaveTrack *outputTrack) {
    // Raise the gain for elements in the center of the sliding history
    // or, if isolating noise, zero out the non-noise
    {
        float *pGain = &mQueue[mCenter]->mGains[0];
        if (mNoiseReductionChoice == NRC_ISOLATE_NOISE) {
            // All above or below the selected frequency range is non-noise
            std::fill(pGain, pGain + mBinLow, 0.0f);
            std::fill(pGain + mBinHigh, pGain + mSpectrumSize, 0.0f);
            pGain += mBinLow;
            for (int jj = mBinLow; jj < mBinHigh; ++jj) {
                const bool isNoise = Classify(statistics, jj);
                *pGain++ = isNoise ? 1.0f : 0.0f;
            }
        } else {
            // All above or below the selected frequency range is non-noise
            std::fill(pGain, pGain + mBinLow, 1.0f);
            std::fill(pGain + mBinHigh, pGain + mSpectrumSize, 1.0f);
            pGain += mBinLow;
            for (int jj = mBinLow; jj < mBinHigh; ++jj) {
                const bool isNoise = Classify(statistics, jj);
                if (!isNoise)
                    *pGain = 1.0;
                ++pGain;
            }
        }
    }

    if (mNoiseReductionChoice != NRC_ISOLATE_NOISE) {
        // In each direction, define an exponential decay of gain from the
        // center; make actual gains the maximum of mNoiseAttenFactor, and
        // the decay curve, and their prior values.

        // First, the attack, which goes backward in time, which is,
        // toward higher indices in the queue.
        for (size_t jj = 0; jj < mSpectrumSize; ++jj) {
            for (unsigned ii = mCenter + 1; ii < mHistoryLen; ++ii) {
                const float minimum =
                        std::max(mNoiseAttenFactor,
                                 mQueue[ii - 1]->mGains[jj] * mOneBlockAttack);
                float &gain = mQueue[ii]->mGains[jj];
                if (gain < minimum)
                    gain = minimum;
                else
                    // We can stop now, our attack curve is intersecting
                    // the decay curve of some window previously processed.
                    break;
            }
        }

        // Now, release.  We need only look one window ahead.  This part will
        // be visited again when we examine the next window, and
        // carry the decay further.
        {
            float *pNextGain = &mQueue[mCenter - 1]->mGains[0];
            const float *pThisGain = &mQueue[mCenter]->mGains[0];
            for (int nn = mSpectrumSize; nn--;) {
                *pNextGain =
                        std::max(*pNextGain,
                                 std::max(mNoiseAttenFactor,
                                          *pThisGain++ * mOneBlockRelease));
                ++pNextGain;
            }
        }
    }


    if (mOutStepCount >= -(int) (mStepsPerWindow - 1)) {
        Record &record = *mQueue[mHistoryLen - 1];  // end of the queue
        const auto last = mSpectrumSize - 1;

        if (mNoiseReductionChoice != NRC_ISOLATE_NOISE)
            // Apply frequency smoothing to output gain
            // Gains are not less than mNoiseAttenFactor
            ApplyFreqSmoothing(record.mGains);

        // Apply gain to FFT
        {
            const float *pGain = &record.mGains[1];
            const float *pReal = &record.mRealFFTs[1];
            const float *pImag = &record.mImagFFTs[1];
            float *pBuffer = &mFFTBuffer[2];
            auto nn = mSpectrumSize - 2;
            if (mNoiseReductionChoice == NRC_LEAVE_RESIDUE) {
                for (; nn--;) {
                    // Subtract the gain we would otherwise apply from 1, and
                    // negate that to flip the phase.
                    const double gain = *pGain++ - 1.0;
                    *pBuffer++ = *pReal++ * gain;
                    *pBuffer++ = *pImag++ * gain;
                }
                mFFTBuffer[0] = record.mRealFFTs[0] * (record.mGains[0] - 1.0f);
                // The Fs/2 component is stored as the imaginary part of the DC component
                mFFTBuffer[1] = record.mImagFFTs[0] * (record.mGains[last] - 1.0f);
            } else {
                for (; nn--;) {
                    const double gain = *pGain++;
                    *pBuffer++ = *pReal++ * gain;
                    *pBuffer++ = *pImag++ * gain;
                }
                mFFTBuffer[0] = record.mRealFFTs[0] * record.mGains[0];
                // The Fs/2 component is stored as the imaginary part of the DC component
                mFFTBuffer[1] = record.mImagFFTs[0] * record.mGains[last];
            }
        }

        // Invert the FFT into the output buffer
        InverseRealFFTf(&mFFTBuffer[0], hFFT.get());

        // Overlap-add
        if (mOutWindow.size() > 0) {
            float *pOut = &mOutOverlapBuffer[0];
            float *pWindow = &mOutWindow[0];
            int *pBitReversed = &hFFT->BitReversed[0];
            for (unsigned int jj = 0; jj < last; ++jj) {
                int kk = *pBitReversed++;
                *pOut++ += mFFTBuffer[kk] * (*pWindow++);
                *pOut++ += mFFTBuffer[kk + 1] * (*pWindow++);
            }
        } else {
            float *pOut = &mOutOverlapBuffer[0];
            int *pBitReversed = &hFFT->BitReversed[0];
            for (unsigned int jj = 0; jj < last; ++jj) {
                int kk = *pBitReversed++;
                *pOut++ += mFFTBuffer[kk];
                *pOut++ += mFFTBuffer[kk + 1];
            }
        }

        float *buffer = &mOutOverlapBuffer[0];
        if (mOutStepCount >= 0) {
            // Output the first portion of the overlap buffer, they're done
            outputTrack->Append((samplePtr) buffer, floatSample, mStepSize);
        }

        // Shift the remainder over.
        memmove(buffer, buffer + mStepSize, sizeof(float) * (mWindowSize - mStepSize));
        std::fill(buffer + mWindowSize - mStepSize, buffer + mWindowSize, 0.0f);
    }
}

bool EffectNoiseReduction::Worker::ProcessOne
        (EffectNoiseReduction &effect, Statistics &statistics, TrackFactory &factory,
         int count, WaveTrack *track, sampleCount start, sampleCount len) {
    if (track == nullptr)
        return false;

    StartNewTrack();

    WaveTrack::Holder outputTrack;
    if (!mDoProfile)
        outputTrack = factory.NewWaveTrack(track->GetSampleFormat(), track->GetRate());

    auto bufferSize = track->GetMaxBlockSize();
    FloatVector buffer(bufferSize);

    auto samplePos = start;
    while (samplePos < start + len) {
        //Get a blockSize of samples (smaller than the size of the buffer)
        const auto blockSize = limitSampleBufferSize(
                track->GetBestBlockSize(samplePos),
                start + len - samplePos
        );

        //Get the samples from the track and put them in the buffer
        track->Get((samplePtr) &buffer[0], floatSample, samplePos, blockSize);
        samplePos += blockSize;

        mInSampleCount += blockSize;
        ProcessSamples(statistics, outputTrack.get(), blockSize, &buffer[0]);

    }

    if (mDoProfile)
        FinishTrackStatistics(statistics);
    else
        FinishTrack(statistics, &*outputTrack);

    if (!mDoProfile) {
        // Flush the output WaveTrack (since it's buffered)
        outputTrack->Flush();

        // Take the output track and insert it in place of the original
        // sample data (as operated on -- this may not match mT0/mT1)
        double t0 = outputTrack->LongSamplesToTime(start);
        double tLen = outputTrack->LongSamplesToTime(len);
        // Filtering effects always end up with more data than they started with.  Delete this 'tail'.
        outputTrack->HandleClear(tLen, outputTrack->GetEndTime(), false, false);
        track->ClearAndPaste(t0, t0 + tLen, &*outputTrack, true, false);
    }

    return true;
}
