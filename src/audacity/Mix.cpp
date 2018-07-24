/**********************************************************************

  Audacity: A Digital Audio Editor

  Mix.cpp

  Dominic Mazzoni
  Markus Meyer
  Vaughan Johnson

*******************************************************************//**

\class Mixer
\brief Functions for doing the mixdown of the tracks.

*//****************************************************************//**

\class MixerSpec
\brief Class used with Mixer.

*//*******************************************************************/

#include <cstring>
#include "Mix.h"

MixerSpec::MixerSpec(unsigned numTracks, unsigned maxNumChannels) {
    mNumTracks = mNumChannels = numTracks;
    mMaxNumChannels = maxNumChannels;

    if (mNumChannels > mMaxNumChannels)
        mNumChannels = mMaxNumChannels;

    Alloc();

    for (unsigned int i = 0; i < mNumTracks; i++)
        for (unsigned int j = 0; j < mNumChannels; j++)
            mMap[i][j] = (i == j);
}

MixerSpec::MixerSpec(const MixerSpec &mixerSpec) {
    mNumTracks = mixerSpec.mNumTracks;
    mMaxNumChannels = mixerSpec.mMaxNumChannels;
    mNumChannels = mixerSpec.mNumChannels;

    Alloc();

    for (unsigned int i = 0; i < mNumTracks; i++)
        for (unsigned int j = 0; j < mNumChannels; j++)
            mMap[i][j] = mixerSpec.mMap[i][j];
}

void MixerSpec::Alloc() {
    mMap.reinit(mNumTracks, mMaxNumChannels);
}

MixerSpec::~MixerSpec() {
}

Mixer::Mixer(const WaveTrackConstArray &inputTracks,
             bool mayThrow,
             double startTime, double stopTime,
             unsigned numOutChannels, size_t outBufferSize, bool outInterleaved,
             double outRate, sampleFormat outFormat,
             bool highQuality, MixerSpec *mixerSpec)
        : mNumInputTracks{inputTracks.size()}

        // This is the number of samples grabbed in one go from a track
        // and placed in a queue, when mixing with resampling.
        // (Should we use WaveTrack::GetBestBlockSize instead?)
        , mQueueMaxLen{65536}, mSampleQueue{mNumInputTracks, mQueueMaxLen}, mNumChannels{numOutChannels},
          mGains{mNumChannels}, mMayThrow{mayThrow} {
    mHighQuality = highQuality;
    mInputTrack.reinit(mNumInputTracks);

    // mSamplePos holds for each track the next sample position not
    // yet processed.
    mSamplePos.reinit(mNumInputTracks);
    for (size_t i = 0; i < mNumInputTracks; i++) {
        mInputTrack[i].SetTrack(inputTracks[i]);
        mSamplePos[i] = inputTracks[i]->TimeToLongSamples(startTime);
    }
    mT0 = startTime;
    mT1 = stopTime;
    mTime = startTime;
    mBufferSize = outBufferSize;
    mInterleaved = outInterleaved;
    mRate = outRate;
    mSpeed = 1.0;
    mFormat = outFormat;
    mApplyTrackGains = true;
    if (mixerSpec && mixerSpec->GetNumChannels() == mNumChannels &&
        mixerSpec->GetNumTracks() == mNumInputTracks)
        mMixerSpec = mixerSpec;
    else
        mMixerSpec = NULL;

    if (mInterleaved) {
        mNumBuffers = 1;
        mInterleavedBufferSize = mBufferSize * mNumChannels;
    } else {
        mNumBuffers = mNumChannels;
        mInterleavedBufferSize = mBufferSize;
    }

    mBuffer.reinit(mNumBuffers);
    mTemp.reinit(mNumBuffers);
    for (unsigned int c = 0; c < mNumBuffers; c++) {
        mBuffer[c].Allocate(mInterleavedBufferSize, mFormat);
        mTemp[c].Allocate(mInterleavedBufferSize, floatSample);
    }
    mFloatBuffer = Floats{mInterleavedBufferSize};

    // But cut the queue into blocks of this finer size
    // for variable rate resampling.  Each block is resampled at some
    // constant rate.
    mProcessLen = 1024;

    // Position in each queue of the start of the next block to resample.
    mQueueStart.reinit(mNumInputTracks);

    // For each queue, the number of available samples after the queue start.
    mQueueLen.reinit(mNumInputTracks);
    mResample.reinit(mNumInputTracks);
    mMinFactor.resize(mNumInputTracks);
    mMaxFactor.resize(mNumInputTracks);
    for (size_t i = 0; i < mNumInputTracks; i++) {
        double factor = (mRate / mInputTrack[i].GetTrack()->GetRate());

        // constant rate resampling
        mbVariableRates = false;
        mMinFactor[i] = mMaxFactor[i] = factor;

        mQueueStart[i] = 0;
        mQueueLen[i] = 0;
    }

    MakeResamplers();

    const auto envLen = std::max(mQueueMaxLen, mInterleavedBufferSize);
    mEnvValues.reinit(envLen);
}

void Mixer::MakeResamplers() {
    for (size_t i = 0; i < mNumInputTracks; i++)
        mResample[i] = std::make_unique<Resample>(mHighQuality, mMinFactor[i], mMaxFactor[i]);
}

Mixer::~Mixer() {
}

void Mixer::Clear() {
    for (unsigned int c = 0; c < mNumBuffers; c++) {
        memset(mTemp[c].ptr(), 0, mInterleavedBufferSize * SAMPLE_SIZE(floatSample));
    }
}

size_t Mixer::Process(size_t maxToProcess) {
    // MB: this is wrong! mT represented warped time, and mTime is too inaccurate to use
    // it here. It's also unnecessary I think.
    //if (mT >= mT1)
    //   return 0;

    decltype(Process(0)) maxOut = 0;
    ArrayOf<int> channelFlags{mNumChannels};

    mMaxOut = maxToProcess;

    Clear();
    for (size_t i = 0; i < mNumInputTracks; i++) {
        const WaveTrack *const track = mInputTrack[i].GetTrack();
        for (size_t j = 0; j < mNumChannels; j++)
            channelFlags[j] = 0;

        if (mMixerSpec) {
            //ignore left and right when downmixing is not required
            for (size_t j = 0; j < mNumChannels; j++)
                channelFlags[j] = mMixerSpec->mMap[i][j] ? 1 : 0;
        } else {
            switch (track->GetChannel()) {
                case WaveTrack::MonoChannel:
                default:
                    for (size_t j = 0; j < mNumChannels; j++)
                        channelFlags[j] = 1;
                    break;
                case WaveTrack::LeftChannel:
                    channelFlags[0] = 1;
                    break;
                case WaveTrack::RightChannel:
                    if (mNumChannels >= 2)
                        channelFlags[1] = 1;
                    else
                        channelFlags[0] = 1;
                    break;
            }
        }
        if (mbVariableRates || track->GetRate() != mRate)
            maxOut = std::max(maxOut,
                              MixVariableRates(channelFlags.get(), mInputTrack[i],
                                               &mSamplePos[i], mSampleQueue[i].get(),
                                               &mQueueStart[i], &mQueueLen[i], mResample[i].get()));
        else
            maxOut = std::max(maxOut,
                              MixSameRate(channelFlags.get(), mInputTrack[i], &mSamplePos[i]));

        double t = mSamplePos[i].as_double() / (double) track->GetRate();
        if (mT0 > mT1)
            // backwards (as possibly in scrubbing)
            mTime = std::max(std::min(t, mTime), mT1);
        else
            // forwards (the usual)
            mTime = std::min(std::max(t, mTime), mT1);
    }
    if (mInterleaved) {
        for (size_t c = 0; c < mNumChannels; c++) {
            CopySamples(mTemp[0].ptr() + (c * SAMPLE_SIZE(floatSample)),
                        floatSample,
                        mBuffer[0].ptr() + (c * SAMPLE_SIZE(mFormat)),
                        mFormat,
                        maxOut,
                        mHighQuality,
                        mNumChannels,
                        mNumChannels);
        }
    } else {
        for (size_t c = 0; c < mNumBuffers; c++) {
            CopySamples(mTemp[c].ptr(),
                        floatSample,
                        mBuffer[c].ptr(),
                        mFormat,
                        maxOut,
                        mHighQuality);
        }
    }
    // MB: this doesn't take warping into account, replaced with code based on mSamplePos
    //mT += (maxOut / mRate);

    return maxOut;
}

void MixBuffers(unsigned numChannels, int *channelFlags, float *gains,
                samplePtr src, SampleBuffer *dests,
                int len, bool interleaved)
{
   for (unsigned int c = 0; c < numChannels; c++) {
      if (!channelFlags[c])
         continue;

      samplePtr destPtr;
      unsigned skip;

      if (interleaved) {
         destPtr = dests[0].ptr() + c*SAMPLE_SIZE(floatSample);
         skip = numChannels;
      } else {
         destPtr = dests[c].ptr();
         skip = 1;
      }

      float gain = gains[c];
      float *dest = (float *)destPtr;
      float *temp = (float *)src;
      for (int j = 0; j < len; j++) {
         *dest += temp[j] * gain;   // the actual mixing process
         dest += skip;
      }
   }
}

size_t Mixer::MixVariableRates(int *channelFlags, WaveTrackCache &cache,
                               sampleCount *pos, float *queue,
                               int *queueStart, int *queueLen,
                               Resample *pResample) {
    const WaveTrack *const track = cache.GetTrack();
    const double trackRate = track->GetRate();
    const double initialWarp = mRate / mSpeed / trackRate;
    const double tstep = 1.0 / trackRate;
    auto sampleSize = SAMPLE_SIZE(floatSample);

    decltype(mMaxOut) out = 0;

    /* time is floating point. Sample rate is integer. The number of samples
     * has to be integer, but the multiplication gives a float result, which we
     * round to get an integer result. TODO: is this always right or can it be
     * off by one sometimes? Can we not get this information directly from the
     * clip (which must know) rather than convert the time?
     *
     * LLL:  Not at this time.  While WaveClips provide methods to retrieve the
     *       start and end sample, they do the same float->sampleCount conversion
     *       to calculate the position.
     */

    // Find the last sample
    double endTime = track->GetEndTime();
    double startTime = track->GetStartTime();
    const bool backwards = (mT1 < mT0);
    const double tEnd = backwards
                        ? std::max(startTime, mT1)
                        : std::min(endTime, mT1);
    const auto endPos = track->TimeToLongSamples(tEnd);
    // Find the time corresponding to the start of the queue, for use with time track
    double t = ((*pos).as_long_long() +
                (backwards ? *queueLen : -*queueLen)) / trackRate;

    while (out < mMaxOut) {
        if (*queueLen < (int) mProcessLen) {
            // Shift pending portion to start of the buffer
            memmove(queue, &queue[*queueStart], (*queueLen) * sampleSize);
            *queueStart = 0;

            auto getLen = limitSampleBufferSize(
                    mQueueMaxLen - *queueLen,
                    backwards ? *pos - endPos : endPos - *pos
            );

            // Nothing to do if past end of play interval
            if (getLen > 0) {
                if (backwards) {
                    auto results = cache.Get(floatSample, *pos - (getLen - 1), getLen, mMayThrow);
                    if (results)
                        memcpy(&queue[*queueLen], results, sizeof(float) * getLen);
                    else
                        memset(&queue[*queueLen], 0, sizeof(float) * getLen);

                    track->GetEnvelopeValues(mEnvValues.get(),
                                             getLen,
                                             (*pos - (getLen - 1)).as_double() / trackRate);
                    *pos -= getLen;
                } else {
                    auto results = cache.Get(floatSample, *pos, getLen, mMayThrow);
                    if (results)
                        memcpy(&queue[*queueLen], results, sizeof(float) * getLen);
                    else
                        memset(&queue[*queueLen], 0, sizeof(float) * getLen);

                    track->GetEnvelopeValues(mEnvValues.get(),
                                             getLen,
                                             (*pos).as_double() / trackRate);

                    *pos += getLen;
                }

                for (decltype(getLen) i = 0; i < getLen; i++) {
                    queue[(*queueLen) + i] *= mEnvValues[i];
                }

                if (backwards)
                    ReverseSamples((samplePtr) &queue[0], floatSample,
                                   *queueLen, getLen);

                *queueLen += getLen;
            }
        }

        auto thisProcessLen = mProcessLen;
        bool last = (*queueLen < (int) mProcessLen);
        if (last) {
            thisProcessLen = *queueLen;
        }

        double factor = initialWarp;

        auto results = pResample->Process(factor,
                                          &queue[*queueStart],
                                          thisProcessLen,
                                          last,
                                          &mFloatBuffer[out],
                                          mMaxOut - out);

        const auto input_used = results.first;
        *queueStart += input_used;
        *queueLen -= input_used;
        out += results.second;
        t += (input_used / trackRate) * (backwards ? -1 : 1);

        if (last) {
            break;
        }
    }

    for (size_t c = 0; c < mNumChannels; c++) {
        if (mApplyTrackGains) {
            mGains[c] = track->GetChannelGain(c);
        } else {
            mGains[c] = 1.0;
        }
    }

    MixBuffers(mNumChannels,
               channelFlags,
               mGains.get(),
               (samplePtr) mFloatBuffer.get(),
               mTemp.get(),
               out,
               mInterleaved);

    return out;
}

size_t Mixer::MixSameRate(int *channelFlags, WaveTrackCache &cache,
                          sampleCount *pos) {
    const WaveTrack *const track = cache.GetTrack();
    const double t = (*pos).as_double() / track->GetRate();
    const double trackEndTime = track->GetEndTime();
    const double trackStartTime = track->GetStartTime();
    const bool backwards = (mT1 < mT0);
    const double tEnd = backwards
                        ? std::max(trackStartTime, mT1)
                        : std::min(trackEndTime, mT1);

    //don't process if we're at the end of the selection or track.
    if ((backwards ? t <= tEnd : t >= tEnd))
        return 0;
    //if we're about to approach the end of the track or selection, figure out how much we need to grab
    auto slen = limitSampleBufferSize(
            mMaxOut,
            // PRL: maybe t and tEnd should be given as sampleCount instead to
            // avoid trouble subtracting one large value from another for a small
            // difference
            sampleCount{(backwards ? t - tEnd : tEnd - t) * track->GetRate() + 0.5}
    );

    if (backwards) {
        auto results = cache.Get(floatSample, *pos - (slen - 1), slen, mMayThrow);
        if (results)
            memcpy(mFloatBuffer.get(), results, sizeof(float) * slen);
        else
            memset(mFloatBuffer.get(), 0, sizeof(float) * slen);
        track->GetEnvelopeValues(mEnvValues.get(), slen, t - (slen - 1) / mRate);
        for (decltype(slen) i = 0; i < slen; i++)
            mFloatBuffer[i] *= mEnvValues[i]; // Track gain control will go here?
        ReverseSamples((samplePtr) mFloatBuffer.get(), floatSample, 0, slen);

        *pos -= slen;
    } else {
        auto results = cache.Get(floatSample, *pos, slen, mMayThrow);
        if (results)
            memcpy(mFloatBuffer.get(), results, sizeof(float) * slen);
        else
            memset(mFloatBuffer.get(), 0, sizeof(float) * slen);
        track->GetEnvelopeValues(mEnvValues.get(), slen, t);
        for (decltype(slen) i = 0; i < slen; i++)
            mFloatBuffer[i] *= mEnvValues[i]; // Track gain control will go here?

        *pos += slen;
    }

    for (size_t c = 0; c < mNumChannels; c++)
        if (mApplyTrackGains)
            mGains[c] = track->GetChannelGain(c);
        else
            mGains[c] = 1.0;

    MixBuffers(mNumChannels, channelFlags, mGains.get(),
               (samplePtr) mFloatBuffer.get(), mTemp.get(), slen, mInterleaved);

    return slen;
}

samplePtr Mixer::GetBuffer()
{
   return mBuffer[0].ptr();
}
