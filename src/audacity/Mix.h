/**********************************************************************

  Audacity: A Digital Audio Editor

  Mix.h

  Dominic Mazzoni
  Markus Meyer

********************************************************************//**

\class ArrayOf
\brief Memory.h template class for making an array of float, bool, etc.

\class ArraysOf
\brief memory.h template class for making an array of arrays.

*//********************************************************************/

#ifndef __AUDACITY_MIX__
#define __AUDACITY_MIX__

#include "MemoryX.h"
#include "WaveTrack.h"
#include "Resample.h"

using WaveTrackConstArray = std::vector<std::shared_ptr<const WaveTrack> >;

class MixerSpec {
    unsigned mNumTracks, mNumChannels, mMaxNumChannels;

    void Alloc();

public:
    ArraysOf<bool> mMap;

    MixerSpec(unsigned numTracks, unsigned maxNumChannels);

    MixerSpec(const MixerSpec &mixerSpec);

    virtual ~MixerSpec();

    bool SetNumChannels(unsigned numChannels);

    unsigned GetNumChannels() { return mNumChannels; }

    unsigned GetMaxNumChannels() { return mMaxNumChannels; }

    unsigned GetNumTracks() { return mNumTracks; }

    MixerSpec &operator=(const MixerSpec &mixerSpec);
};


class Mixer {
public:

    Mixer(const WaveTrackConstArray &inputTracks, bool mayThrow,
          double startTime, double stopTime,
          unsigned numOutChannels, size_t outBufferSize, bool outInterleaved,
          double outRate, sampleFormat outFormat,
          bool highQuality = true, MixerSpec *mixerSpec = nullptr);

    virtual ~ Mixer();

    /// Process a maximum of 'maxSamples' samples and put them into
    /// a buffer which can be retrieved by calling GetBuffer().
    /// Returns number of output samples, or 0, if there are no
    /// more samples that must be processed.
    size_t Process(size_t maxSamples);

   /// Retrieve the main buffer or the interleaved buffer
   samplePtr GetBuffer();

private:

    void Clear();

    void MakeResamplers();

    size_t MixSameRate(int *channelFlags, WaveTrackCache &cache,
                       sampleCount *pos);

    size_t MixVariableRates(int *channelFlags, WaveTrackCache &cache,
                            sampleCount *pos, float *queue,
                            int *queueStart, int *queueLen,
                            Resample *pResample);

    // Input
    size_t mNumInputTracks;
    ArrayOf<WaveTrackCache> mInputTrack;
    bool mbVariableRates;
    ArrayOf<sampleCount> mSamplePos;
    bool mApplyTrackGains;
    Doubles mEnvValues;
    double mT0; // Start time
    double mT1; // Stop time (none if mT0==mT1)
    double mTime;  // Current time (renamed from mT to mTime for consistency with AudioIO - mT represented warped time there)
    ArrayOf<std::unique_ptr<Resample>> mResample;
    size_t mQueueMaxLen;
    FloatBuffers mSampleQueue;
    ArrayOf<int> mQueueStart;
    ArrayOf<int> mQueueLen;
    size_t mProcessLen;
    MixerSpec *mMixerSpec;

    // Output
    size_t mMaxOut;
    unsigned mNumChannels;
    Floats mGains;
    unsigned mNumBuffers;
    size_t mBufferSize;
    size_t mInterleavedBufferSize;
    sampleFormat mFormat;
    bool mInterleaved;
    ArrayOf<SampleBuffer> mBuffer, mTemp;
    Floats mFloatBuffer;
    double mRate;
    double mSpeed;
    bool mHighQuality;
    std::vector<double> mMinFactor, mMaxFactor;

    bool mMayThrow;
};

#endif
