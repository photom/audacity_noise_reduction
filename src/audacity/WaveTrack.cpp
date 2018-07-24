/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrack.cpp

  Dominic Mazzoni

*******************************************************************//**

\class WaveTrack
\brief A Track that contains audio waveform data.

*//****************************************************************//**

\class WaveTrack::Location
\brief Used only by WaveTrack, a special way to hold location that
can accommodate merged regions.

*//****************************************************************//**

\class TrackFactory
\brief Used to create a WaveTrack, or a LabelTrack..  Implementation
of the functions of this class are dispersed through the different
Track classes.

*//*******************************************************************/

#include <float.h>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <cstring>

#include "MemoryX.h"
#include "WaveTrack.h"
#include "Sequence.h"
#include "InconsistencyException.h"
#include "Track.h"
#include "TimeWarper.h"


WaveTrack::Holder TrackFactory::NewWaveTrack(sampleFormat format, double rate) {
    return std::unique_ptr<WaveTrack>
            {safenew WaveTrack(mDirManager, format, rate)};
}

WaveTrack::WaveTrack(const std::shared_ptr<DirManager> &projDirManager, sampleFormat format, double rate) :
        mDirManager(projDirManager) {
    mFormat = format;
    mRate = (int) rate;
    mGain = 1.0;
    mPan = 0.0;
    mChannel = MonoChannel;
}

WaveTrack::WaveTrack(const WaveTrack &orig) {
    Init(orig);

    for (const auto &clip : orig.mClips)
        mClips.push_back
                (std::make_unique<WaveClip>(*clip, mDirManager, true));
}

// Copy the track metadata but not the contents.
void WaveTrack::Init(const WaveTrack &orig) {
    mFormat = orig.mFormat;
    mRate = orig.mRate;
    mGain = orig.mGain;
    mPan = orig.mPan;
}

WaveTrack::~WaveTrack() {
}

double WaveTrack::GetRate() const {
    return mRate;
}

void WaveTrack::SetRate(double newRate) {
    assert(newRate > 0);
    newRate = std::max(1.0, newRate);
    mRate = (int) newRate;
}

float WaveTrack::GetGain() const {
    return mGain;
}

void WaveTrack::SetGain(float newGain) {
    mGain = newGain;
}

float WaveTrack::GetPan() const {
    return mPan;
}

void WaveTrack::SetPan(float newPan) {
    if (newPan > 1.0)
        mPan = 1.0;
    else if (newPan < -1.0)
        mPan = -1.0;
    else
        mPan = newPan;
}

sampleCount WaveTrack::TimeToLongSamples(double t0) const {
    return sampleCount(floor(t0 * mRate + 0.5));
}

double WaveTrack::GetStartTime() const {
    bool found = false;
    double best = 0.0;

    if (mClips.empty())
        return 0;

    for (const auto &clip : mClips)
        if (!found) {
            found = true;
            best = clip->GetStartTime();
        } else if (clip->GetStartTime() < best)
            best = clip->GetStartTime();

    return best;
}

double WaveTrack::GetEndTime() const {
    bool found = false;
    double best = 0.0;

    if (mClips.empty())
        return 0;

    for (const auto &clip : mClips)
        if (!found) {
            found = true;
            best = clip->GetEndTime();
        } else if (clip->GetEndTime() > best)
            best = clip->GetEndTime();

    return best;
}

bool WaveTrack::Get(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len, fillFormat fill,
                    bool mayThrow, sampleCount *pNumCopied) const {
    // Simple optimization: When this buffer is completely contained within one clip,
    // don't clear anything (because we won't have to). Otherwise, just clear
    // everything to be on the safe side.
    bool doClear = true;
    bool result = true;
    sampleCount samplesCopied = 0;
    for (const auto &clip: mClips) {
        if (start >= clip->GetStartSample() && start + len <= clip->GetEndSample()) {
            doClear = false;
            break;
        }
    }
    if (doClear) {
        // Usually we fill in empty space with zero
        if (fill == fillZero)
            ClearSamples(buffer, format, 0, len);
            // but we don't have to.
        else if (fill == fillTwo) {
            assert(format == floatSample);
            float *pBuffer = (float *) buffer;
            for (size_t i = 0; i < len; i++)
                pBuffer[i] = 2.0f;
        } else {
            std::cerr << "Invalid fill format" << std::endl;
        }
    }

    for (const auto &clip: mClips) {
        auto clipStart = clip->GetStartSample();
        auto clipEnd = clip->GetEndSample();

        if (clipEnd > start && clipStart < start + len) {
            // Clip sample region and Get/Put sample region overlap
            auto samplesToCopy =
                    std::min(start + len - clipStart, clip->GetNumSamples());
            auto startDelta = clipStart - start;
            decltype(startDelta) inclipDelta = 0;
            if (startDelta < 0) {
                inclipDelta = -startDelta; // make positive value
                samplesToCopy -= inclipDelta;
                // samplesToCopy is now either len or
                //    (clipEnd - clipStart) - (start - clipStart)
                //    == clipEnd - start > 0
                // samplesToCopy is not more than len
                //
                startDelta = 0;
                // startDelta is zero
            } else {
                // startDelta is nonnegative and less than than len
                // samplesToCopy is positive and not more than len
            }

            if (!clip->GetSamples(
                    (samplePtr) (((char *) buffer) +
                                 startDelta.as_size_t() *
                                 SAMPLE_SIZE(format)),
                    format, inclipDelta, samplesToCopy.as_size_t(), mayThrow))
                result = false;
            else
                samplesCopied += samplesToCopy;
        }
    }
    if (pNumCopied)
        *pNumCopied = samplesCopied;
    return result;
}

size_t WaveTrack::GetBestBlockSize(sampleCount s) const {
    auto bestBlockSize = GetMaxBlockSize();

    for (const auto &clip : mClips) {
        auto startSample = (sampleCount) floor(clip->GetStartTime() * mRate + 0.5);
        auto endSample = startSample + clip->GetNumSamples();
        if (s >= startSample && s < endSample) {
            bestBlockSize = clip->GetSequence()->GetBestBlockSize(s - startSample);
            break;
        }
    }

    return bestBlockSize;
}

size_t WaveTrack::GetMaxBlockSize() const {
    decltype(GetMaxBlockSize()) maxblocksize = 0;
    for (const auto &clip : mClips) {
        maxblocksize = std::max(maxblocksize, clip->GetSequence()->GetMaxBlockSize());
    }

    if (maxblocksize == 0) {
        // We really need the maximum block size, so create a
        // temporary sequence to get it.
        maxblocksize = Sequence{mDirManager, mFormat}.GetMaxBlockSize();
    }

    assert(maxblocksize > 0);

    return maxblocksize;
}

double WaveTrack::LongSamplesToTime(sampleCount pos) const {
    return pos.as_double() / mRate;
}

void WaveTrack::Flush()
// NOFAIL-GUARANTEE that the rightmost clip will be in a flushed state.
// PARTIAL-GUARANTEE in case of exceptions:
// Some initial portion (maybe none) of the append buffer of the rightmost
// clip gets appended; no previously saved contents are lost.
{
    // After appending, presumably.  Do this to the clip that gets appended.
    RightmostOrNewClip()->Flush();
}

WaveClip *WaveTrack::RightmostOrNewClip()
// NOFAIL-GUARANTEE
{
    if (mClips.empty()) {
        WaveClip *clip = CreateClip();
        clip->SetOffset(mOffset);
        return clip;
    } else {
        auto it = mClips.begin();
        WaveClip *rightmost = (*it++).get();
        double maxOffset = rightmost->GetOffset();
        for (auto end = mClips.end(); it != end; ++it) {
            WaveClip *clip = it->get();
            double offset = clip->GetOffset();
            if (maxOffset < offset)
                maxOffset = offset, rightmost = clip;
        }
        return rightmost;
    }
}

WaveClip *WaveTrack::CreateClip() {
    mClips.emplace_back(std::make_unique<WaveClip>(mDirManager, mFormat, mRate));
    return mClips.back().get();
}

WaveClip *WaveTrack::NewestOrNewClip() {
    if (mClips.empty()) {
        WaveClip *clip = CreateClip();
        clip->SetOffset(mOffset);
        return clip;
    } else
        return mClips.back().get();
}

void WaveTrack::Append(samplePtr buffer, sampleFormat format,
                       size_t len, unsigned int stride /* = 1 */)
// PARTIAL-GUARANTEE in case of exceptions:
// Some prefix (maybe none) of the buffer is appended, and no content already
// flushed to disk is lost.
{
    RightmostOrNewClip()->Append(buffer, format, len, stride);
}

namespace {
WaveClipHolders::const_iterator
FindClip(const WaveClipHolders &list, const WaveClip *clip, int *distance = nullptr) {
    if (distance)
        *distance = 0;
    auto it = list.begin();
    for (const auto end = list.end(); it != end; ++it) {
        if (it->get() == clip)
            break;
        if (distance)
            ++*distance;
    }
    return it;
}

WaveClipHolders::iterator
FindClip(WaveClipHolders &list, const WaveClip *clip, int *distance = nullptr) {
    if (distance)
        *distance = 0;
    auto it = list.begin();
    for (const auto end = list.end(); it != end; ++it) {
        if (it->get() == clip)
            break;
        if (distance)
            ++*distance;
    }
    return it;
}
}

void WaveTrack::HandleClear(double t0, double t1,
                            bool addCutLines, bool split)
// STRONG-GUARANTEE
{
    if (t1 < t0)
        THROW_INCONSISTENCY_EXCEPTION;

    bool editClipCanMove = false;

    WaveClipPointers clipsToDelete;
    WaveClipHolders clipsToAdd;

    // We only add cut lines when deleting in the middle of a single clip
    // The cut line code is not really prepared to handle other situations
    if (addCutLines) {
        for (const auto &clip : mClips) {
            if (!clip->BeforeClip(t1) && !clip->AfterClip(t0) &&
                (clip->BeforeClip(t0) || clip->AfterClip(t1))) {
                addCutLines = false;
                break;
            }
        }
    }

    for (const auto &clip : mClips) {
        if (clip->BeforeClip(t0) && clip->AfterClip(t1)) {
            // Whole clip must be deleted - remember this
            clipsToDelete.push_back(clip.get());
        } else if (!clip->BeforeClip(t1) && !clip->AfterClip(t0)) {
            // Clip data is affected by command
            if (addCutLines) {
                // Don't modify this clip in place, because we want a strong
                // guarantee, and might modify another clip
                clipsToDelete.push_back(clip.get());
                auto newClip = std::make_unique<WaveClip>(*clip, mDirManager, true);
                newClip->ClearAndAddCutLine(t0, t1);
                clipsToAdd.push_back(std::move(newClip));
            } else {
                if (split) {
                    // Three cases:

                    if (clip->BeforeClip(t0)) {
                        // Delete from the left edge

                        // Don't modify this clip in place, because we want a strong
                        // guarantee, and might modify another clip
                        clipsToDelete.push_back(clip.get());
                        auto newClip = std::make_unique<WaveClip>(*clip, mDirManager, true);
                        newClip->Clear(clip->GetStartTime(), t1);
                        newClip->Offset(t1 - clip->GetStartTime());

                        clipsToAdd.push_back(std::move(newClip));
                    } else if (clip->AfterClip(t1)) {
                        // Delete to right edge

                        // Don't modify this clip in place, because we want a strong
                        // guarantee, and might modify another clip
                        clipsToDelete.push_back(clip.get());
                        auto newClip = std::make_unique<WaveClip>(*clip, mDirManager, true);
                        newClip->Clear(t0, clip->GetEndTime());

                        clipsToAdd.push_back(std::move(newClip));
                    } else {
                        // Delete in the middle of the clip...we actually create two
                        // NEW clips out of the left and right halves...

                        // left
                        clipsToAdd.push_back
                                (std::make_unique<WaveClip>(*clip, mDirManager, true));
                        clipsToAdd.back()->Clear(t0, clip->GetEndTime());

                        // right
                        clipsToAdd.push_back
                                (std::make_unique<WaveClip>(*clip, mDirManager, true));
                        WaveClip *const right = clipsToAdd.back().get();
                        right->Clear(clip->GetStartTime(), t1);
                        right->Offset(t1 - clip->GetStartTime());

                        clipsToDelete.push_back(clip.get());
                    }
                } else {
                    // (We are not doing a split cut)

                    // Don't modify this clip in place, because we want a strong
                    // guarantee, and might modify another clip
                    clipsToDelete.push_back(clip.get());
                    auto newClip = std::make_unique<WaveClip>(*clip, mDirManager, true);

                    // clip->Clear keeps points < t0 and >= t1 via Envelope::CollapseRegion
                    newClip->Clear(t0, t1);

                    clipsToAdd.push_back(std::move(newClip));
                }
            }
        }
    }

    // Only now, change the contents of this track
    // use NOFAIL-GUARANTEE for the rest

    for (const auto &clip : mClips) {
        if (clip->BeforeClip(t1)) {
            // Clip is "behind" the region -- offset it unless we're splitting
            // or we're using the "don't move other clips" mode
            if (!split && editClipCanMove)
                clip->Offset(-(t1 - t0));
        }
    }

    for (const auto &clip: clipsToDelete) {
        auto myIt = FindClip(mClips, clip);
        assert (myIt != mClips.end());
        mClips.erase(myIt); // deletes the clip!
    }

    for (auto &clip: clipsToAdd)
        mClips.push_back(std::move(clip)); // transfer ownership
}

int WaveTrack::GetNumClips() const {
    return mClips.size();
}

void WaveTrack::Paste(double t0, const WaveTrack *src)
// WEAK-GUARANTEE
{
    bool editClipCanMove = false;

    if (src == nullptr)
        // THROW_INCONSISTENCY_EXCEPTION; // ?
        return;

    if (src->GetKind() != WaveTrack::Wave)
        // THROW_INCONSISTENCY_EXCEPTION; // ?
        return;

    const auto *other = static_cast<const WaveTrack *>(src);

    //
    // Pasting is a bit complicated, because with the existence of multiclip mode,
    // we must guess the behaviour the user wants.
    //
    // Currently, two modes are implemented:
    //
    // - If a single clip should be pasted, and it should be pasted inside another
    //   clip, no NEW clips are generated. The audio is simply inserted.
    //   This resembles the old (pre-multiclip support) behaviour. However, if
    //   the clip is pasted outside of any clip, a NEW clip is generated. This is
    //   the only behaviour which is different to what was done before, but it
    //   shouldn't confuse users too much.
    //
    // - If multiple clips should be pasted, or a single clip that does not fill
    // the duration of the pasted track, these are always pasted as single
    // clips, and the current clip is splitted, when necessary. This may seem
    // strange at first, but it probably is better than trying to auto-merge
    // anything. The user can still merge the clips by hand (which should be a
    // simple command reachable by a hotkey or single mouse click).
    //

    if (other->GetNumClips() == 0)
        return;

    //wxPrintf("paste: we have at least one clip\n");

    bool singleClipMode = (other->GetNumClips() == 1 &&
                           other->GetStartTime() == 0.0);

    const double insertDuration = other->GetEndTime();
    if (insertDuration != 0 && insertDuration < 1.0 / mRate)
        // PRL:  I added this check to avoid violations of preconditions in other WaveClip and Sequence
        // methods, but allow the value 0 so I don't subvert the purpose of commit
        // 739422ba70ceb4be0bb1829b6feb0c5401de641e which causes append-recording always to make
        // a new clip.
        return;

    if (singleClipMode) {
        // Single clip mode
        // wxPrintf("paste: checking for single clip mode!\n");

        WaveClip *insideClip = nullptr;

        for (const auto &clip : mClips) {
            // If clips are immovable we also allow prepending to clips
            if (clip->WithinClip(t0) ||
                TimeToLongSamples(t0) == clip->GetStartSample()) {
                insideClip = clip.get();
                break;
            }
        }

        if (insideClip) {
            // We did not move other clips out of the way already, so
            // check if we can paste without having to move other clips
            for (const auto &clip : mClips) {
                if (clip->GetStartTime() > insideClip->GetStartTime() &&
                    insideClip->GetEndTime() + insertDuration >
                    clip->GetStartTime())
                    // STRONG-GUARANTEE in case of this path
                    // not that it matters.
                    THROW_INCONSISTENCY_EXCEPTION;
            }
            insideClip->Paste(t0, other->GetClipByIndex(0));
            return;
        }

        // Just fall through and exhibit NEW behaviour
    }

    // Insert NEW clips
    //wxPrintf("paste: multi clip mode!\n");

    if (!editClipCanMove && !IsEmpty(t0, t0 + insertDuration - 1.0 / mRate))
        // STRONG-GUARANTEE in case of this path
        // not that it matters.
        THROW_INCONSISTENCY_EXCEPTION;

    for (const auto &clip : other->mClips) {
        // AWD Oct. 2009: Don't actually paste in placeholder clips
        if (!clip->GetIsPlaceholder()) {
            auto newClip =
                    std::make_unique<WaveClip>(*clip, mDirManager, true);
            newClip->Resample(mRate);
            newClip->Offset(t0);
            newClip->MarkChanged();
            mClips.push_back(std::move(newClip)); // transfer ownership
        }
    }
}

const WaveClip *WaveTrack::GetClipByIndex(int index) const {
    return const_cast<WaveTrack &>(*this).GetClipByIndex(index);
}

WaveClip *WaveTrack::GetClipByIndex(int index) {
    if (index < (int) mClips.size())
        return mClips[index].get();
    else
        return nullptr;
}

bool WaveTrack::IsEmpty(double t0, double t1) const {
    if (t0 > t1)
        return true;

    //wxPrintf("Searching for overlap in %.6f...%.6f\n", t0, t1);
    for (const auto &clip : mClips) {
        if (!clip->BeforeClip(t1) && !clip->AfterClip(t0)) {
            //wxPrintf("Overlapping clip: %.6f...%.6f\n",
            //       clip->GetStartTime(),
            //       clip->GetEndTime());
            // We found a clip that overlaps this region
            return false;
        }
    }
    //wxPrintf("No overlap found\n");

    // Otherwise, no clips overlap this region
    return true;
}

void WaveTrack::Set(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len)
// WEAK-GUARANTEE
{
    for (const auto &clip: mClips) {
        auto clipStart = clip->GetStartSample();
        auto clipEnd = clip->GetEndSample();

        if (clipEnd > start && clipStart < start + len) {
            // Clip sample region and Get/Put sample region overlap
            auto samplesToCopy =
                    std::min(start + len - clipStart, clip->GetNumSamples());
            auto startDelta = clipStart - start;
            decltype(startDelta) inclipDelta = 0;
            if (startDelta < 0) {
                inclipDelta = -startDelta; // make positive value
                samplesToCopy -= inclipDelta;
                // samplesToCopy is now either len or
                //    (clipEnd - clipStart) - (start - clipStart)
                //    == clipEnd - start > 0
                // samplesToCopy is not more than len
                //
                startDelta = 0;
                // startDelta is zero
            } else {
                // startDelta is nonnegative and less than than len
                // samplesToCopy is positive and not more than len
            }

            clip->SetSamples(
                    (samplePtr) (((char *) buffer) +
                                 startDelta.as_size_t() *
                                 SAMPLE_SIZE(format)),
                    format, inclipDelta, samplesToCopy.as_size_t());
            clip->MarkChanged();
        }
    }
}


sampleCount WaveTrack::GetBlockStart(sampleCount s) const {
    for (const auto &clip : mClips) {
        const auto startSample = (sampleCount) floor(0.5 + clip->GetStartTime() * mRate);
        const auto endSample = startSample + clip->GetNumSamples();
        if (s >= startSample && s < endSample)
            return startSample + clip->GetSequence()->GetBlockStart(s - startSample);
    }

    return -1;
}

void WaveTrack::GetEnvelopeValues(double *buffer, size_t bufferLen,
                                  double t0) const {
    // The output buffer corresponds to an unbroken span of time which the callers expect
    // to be fully valid.  As clips are processed below, the output buffer is updated with
    // envelope values from any portion of a clip, start, end, middle, or none at all.
    // Since this does not guarantee that the entire buffer is filled with values we need
    // to initialize the entire buffer to a default value.
    //
    // This does mean that, in the cases where a usable clip is located, the buffer value will
    // be set twice.  Unfortunately, there is no easy way around this since the clips are not
    // stored in increasing time order.  If they were, we could just track the time as the
    // buffer is filled.
    for (decltype(bufferLen) i = 0; i < bufferLen; i++) {
        buffer[i] = 1.0;
    }

    double startTime = t0;
    auto tstep = 1.0 / mRate;
    double endTime = t0 + tstep * bufferLen;
    for (const auto &clip: mClips) {
        // IF clip intersects startTime..endTime THEN...
        auto dClipStartTime = clip->GetStartTime();
        auto dClipEndTime = clip->GetEndTime();
        if ((dClipStartTime < endTime) && (dClipEndTime > startTime)) {
            auto rbuf = buffer;
            auto rlen = bufferLen;
            auto rt0 = t0;

            if (rt0 < dClipStartTime) {
                // This is not more than the number of samples in
                // (endTime - startTime) which is bufferLen:
                auto nDiff = (sampleCount) floor((dClipStartTime - rt0) * mRate + 0.5);
                auto snDiff = nDiff.as_size_t();
                rbuf += snDiff;
                assert(snDiff <= rlen);
                rlen -= snDiff;
                rt0 = dClipStartTime;
            }

            if (rt0 + rlen * tstep > dClipEndTime) {
                auto nClipLen = clip->GetEndSample() - clip->GetStartSample();

                if (nClipLen <=
                    0) // Testing for bug 641, this problem is consistently '== 0', but doesn't hurt to check <.
                    return;

                // This check prevents problem cited in http://bugzilla.audacityteam.org/show_bug.cgi?id=528#c11,
                // Gale's cross_fade_out project, which was already corrupted by bug 528.
                // This conditional prevents the previous write past the buffer end, in clip->GetEnvelope() call.
                // Never increase rlen here.
                // PRL bug 827:  rewrote it again
                rlen = limitSampleBufferSize(rlen, nClipLen);
                rlen = std::min(rlen, size_t(floor(0.5 + (dClipEndTime - rt0) / tstep)));
            }
            // Samples are obtained for the purpose of rendering a wave track,
            // so quantize time
            clip->GetEnvelope()->GetValues(rbuf, rlen, rt0, tstep);
        }
    }
}

float WaveTrack::GetChannelGain(int channel) const {
    float left = 1.0;
    float right = 1.0;

    if (mPan < 0)
        right = (mPan + 1.0);
    else if (mPan > 0)
        left = 1.0 - mPan;

    if ((channel % 2) == 0)
        return left * mGain;
    else
        return right * mGain;
}

WaveTrackCache::~WaveTrackCache() {
}

void WaveTrackCache::SetTrack(const std::shared_ptr<const WaveTrack> &pTrack) {
    if (mPTrack != pTrack) {
        if (pTrack) {
            mBufferSize = pTrack->GetMaxBlockSize();
            if (!mPTrack ||
                mPTrack->GetMaxBlockSize() != mBufferSize) {
                Free();
                mBuffers[0].data = Floats{mBufferSize};
                mBuffers[1].data = Floats{mBufferSize};
            }
        } else
            Free();
        mPTrack = pTrack;
        mNValidBuffers = 0;
    }
}

void WaveTrackCache::Free() {
    mBuffers[0].Free();
    mBuffers[1].Free();
    mOverlapBuffer.Free();
    mNValidBuffers = 0;
}

constSamplePtr WaveTrackCache::Get(sampleFormat format,
                                   sampleCount start, size_t len, bool mayThrow) {
    if (format == floatSample && len > 0) {
        const auto end = start + len;

        bool fillFirst = (mNValidBuffers < 1);
        bool fillSecond = (mNValidBuffers < 2);

        // Discard cached results that we no longer need
        if (mNValidBuffers > 0 &&
            (end <= mBuffers[0].start ||
             start >= mBuffers[mNValidBuffers - 1].end())) {
            // Complete miss
            fillFirst = true;
            fillSecond = true;
        } else if (mNValidBuffers == 2 &&
                   start >= mBuffers[1].start &&
                   end > mBuffers[1].end()) {
            // Request starts in the second buffer and extends past it.
            // Discard the first buffer.
            // (But don't deallocate the buffer space.)
            mBuffers[0].swap(mBuffers[1]);
            fillSecond = true;
            mNValidBuffers = 1;
        } else if (mNValidBuffers > 0 &&
                   start < mBuffers[0].start &&
                   0 <= mPTrack->GetBlockStart(start)) {
            // Request is not a total miss but starts before the cache,
            // and there is a clip to fetch from.
            // Not the access pattern for drawing spectrogram or playback,
            // but maybe scrubbing causes this.
            // Move the first buffer into second place, and later
            // refill the first.
            // (This case might be useful when marching backwards through
            // the track, as with scrubbing.)
            mBuffers[0].swap(mBuffers[1]);
            fillFirst = true;
            fillSecond = false;
            // Cache is not in a consistent state yet
            mNValidBuffers = 0;
        }

        // Refill buffers as needed
        if (fillFirst) {
            const auto start0 = mPTrack->GetBlockStart(start);
            if (start0 >= 0) {
                const auto len0 = mPTrack->GetBestBlockSize(start0);
                assert(len0 <= mBufferSize);
                if (!mPTrack->Get(
                        samplePtr(mBuffers[0].data.get()), floatSample, start0, len0,
                        fillZero, mayThrow))
                    return 0;
                mBuffers[0].start = start0;
                mBuffers[0].len = len0;
                if (!fillSecond &&
                    mBuffers[0].end() != mBuffers[1].start)
                    fillSecond = true;
                // Keep the partially updated state consistent:
                mNValidBuffers = fillSecond ? 1 : 2;
            } else {
                // Request may fall between the clips of a track.
                // Invalidate all.  WaveTrack::Get() will return zeroes.
                mNValidBuffers = 0;
                fillSecond = false;
            }
        }
        assert(!fillSecond || mNValidBuffers > 0);
        if (fillSecond) {
            mNValidBuffers = 1;
            const auto end0 = mBuffers[0].end();
            if (end > end0) {
                const auto start1 = mPTrack->GetBlockStart(end0);
                if (start1 == end0) {
                    const auto len1 = mPTrack->GetBestBlockSize(start1);
                    assert(len1 <= mBufferSize);
                    if (!mPTrack->Get(samplePtr(mBuffers[1].data.get()), floatSample, start1, len1, fillZero, mayThrow))
                        return 0;
                    mBuffers[1].start = start1;
                    mBuffers[1].len = len1;
                    mNValidBuffers = 2;
                }
            }
        }
        assert(mNValidBuffers < 2 || mBuffers[0].end() == mBuffers[1].start);

        samplePtr buffer = 0;
        auto remaining = len;

        // Possibly get an initial portion that is uncached

        // This may be negative
        const auto initLen =
                mNValidBuffers < 1 ? sampleCount(len)
                                   : std::min(sampleCount(len), mBuffers[0].start - start);

        if (initLen > 0) {
            // This might be fetching zeroes between clips
            mOverlapBuffer.Resize(len, format);
            // initLen is not more than len:
            auto sinitLen = initLen.as_size_t();
            if (!mPTrack->Get(mOverlapBuffer.ptr(), format, start, sinitLen,
                              fillZero, mayThrow))
                return 0;
            assert(sinitLen <= remaining);
            remaining -= sinitLen;
            start += initLen;
            buffer = mOverlapBuffer.ptr() + sinitLen * SAMPLE_SIZE(format);
        }

        // Now satisfy the request from the buffers
        for (int ii = 0; ii < mNValidBuffers && remaining > 0; ++ii) {
            const auto starti = start - mBuffers[ii].start;
            // Treatment of initLen above establishes this loop invariant,
            // and statements below preserve it:
            assert(starti >= 0);

            // This may be negative
            const auto leni =
                    std::min(sampleCount(remaining), mBuffers[ii].len - starti);
            if (initLen <= 0 && leni == len) {
                // All is contiguous already.  We can completely avoid copying
                // leni is nonnegative, therefore start falls within mBuffers[ii],
                // so starti is bounded between 0 and buffer length
                return samplePtr(mBuffers[ii].data.get() + starti.as_size_t());
            } else if (leni > 0) {
                // leni is nonnegative, therefore start falls within mBuffers[ii]
                // But we can't satisfy all from one buffer, so copy
                if (buffer == 0) {
                    mOverlapBuffer.Resize(len, format);
                    buffer = mOverlapBuffer.ptr();
                }
                // leni is positive and not more than remaining
                const size_t size = sizeof(float) * leni.as_size_t();
                // starti is less than mBuffers[ii].len and nonnegative
                memcpy(buffer, mBuffers[ii].data.get() + starti.as_size_t(), size);
                assert(leni <= remaining);
                remaining -= leni.as_size_t();
                start += leni;
                buffer += size;
            }
        }

        if (remaining > 0) {
            // Very big request!
            // Fall back to direct fetch
            if (buffer == 0) {
                mOverlapBuffer.Resize(len, format);
                buffer = mOverlapBuffer.ptr();
            }
            if (!mPTrack->Get(buffer, format, start, remaining, fillZero, mayThrow))
                return 0;
        }

        return mOverlapBuffer.ptr();
    }

    // Cache works only for float format.
    mOverlapBuffer.Resize(len, format);
    if (mPTrack->Get(mOverlapBuffer.ptr(), format, start, len, fillZero, mayThrow))
        return mOverlapBuffer.ptr();
    else
        return 0;
}


namespace {
template<typename Cont1, typename Cont2>
Cont1 FillSortedClipArray(const Cont2 &mClips) {
    Cont1 clips;
    for (const auto &clip : mClips)
        clips.push_back(clip.get());
    std::sort(clips.begin(), clips.end(),
              [](const WaveClip *a, const WaveClip *b) { return a->GetStartTime() < b->GetStartTime(); });
    return clips;
}
}

WaveClipPointers WaveTrack::SortedClipArray() {
    return FillSortedClipArray<WaveClipPointers>(mClips);
}

WaveClipConstPointers WaveTrack::SortedClipArray() const {
    return FillSortedClipArray<WaveClipConstPointers>(mClips);
}

int WaveTrack::GetClipIndex(const WaveClip *clip) const {
    int result;
    FindClip(mClips, clip, &result);
    return result;
}

void WaveTrack::MergeClips(int clipidx1, int clipidx2)
// STRONG-GUARANTEE
{
    WaveClip *clip1 = GetClipByIndex(clipidx1);
    WaveClip *clip2 = GetClipByIndex(clipidx2);

    if (!clip1 || !clip2) // Could happen if one track of a linked pair had a split and the other didn't.
        return; // Don't throw, just do nothing.

    // Append data from second clip to first clip
    // use STRONG-GUARANTEE
    clip1->Paste(clip1->GetEndTime(), clip2);

    // use NOFAIL-GUARANTEE for the rest
    // Delete second clip
    auto it = FindClip(mClips, clip2);
    mClips.erase(it);
}

void WaveTrack::SplitAt(double t)
// WEAK-GUARANTEE
{
    for (const auto &c : mClips) {
        if (c->WithinClip(t)) {
            t = LongSamplesToTime(TimeToLongSamples(t)); // put t on a sample
            auto newClip = std::make_unique<WaveClip>(*c, mDirManager, true);
            c->Clear(t, c->GetEndTime());
            newClip->Clear(c->GetStartTime(), t);

            //offset the NEW clip by the splitpoint (noting that it is already offset to c->GetStartTime())
            sampleCount here = llrint(floor(((t - c->GetStartTime()) * mRate) + 0.5));
            newClip->Offset(here.as_double() / (double) mRate);
            // This could invalidate the iterators for the loop!  But we return
            // at once so it's okay
            mClips.push_back(std::move(newClip)); // transfer ownership
            return;
        }
    }
}

//
// ClearAndPaste() is a specialized version of HandleClear()
// followed by Paste() and is used mostly by effects that
// can't replace track data directly using Get()/Set().
//
// HandleClear() removes any cut/split lines lines with the
// cleared range, but, in most cases, effects want to preserve
// the existing cut/split lines, so they are saved before the
// HandleClear()/Paste() and restored after.
//
// If the pasted track overlaps two or more clips, then it will
// be pasted with visible split lines.  Normally, effects do not
// want these extra lines, so they may be merged out.
//
void WaveTrack::ClearAndPaste(double t0, // Start of time to clear
                              double t1, // End of time to clear
                              const WaveTrack *src, // What to paste
                              bool preserve, // Whether to reinsert splits/cuts
                              bool merge, // Whether to remove 'extra' splits
                              const TimeWarper *effectWarper // How does time change
)
// WEAK-GUARANTEE
// this WaveTrack remains destructible in case of AudacityException.
// But some of its cutline clips may have been destroyed.
{
    double dur = std::min(t1 - t0, src->GetEndTime());

    // If duration is 0, then it's just a plain paste
    if (dur == 0.0) {
        // use WEAK-GUARANTEE
        Paste(t0, src);
        return;
    }

    std::vector<double> splits;
    WaveClipHolders cuts;

    // If provided time warper was NULL, use a default one that does nothing
    IdentityTimeWarper localWarper;
    const TimeWarper *warper = (effectWarper ? effectWarper : &localWarper);

    // Align to a sample
    t0 = LongSamplesToTime(TimeToLongSamples(t0));
    t1 = LongSamplesToTime(TimeToLongSamples(t1));

    // Save the cut/split lines whether preserving or not since merging
    // needs to know if a clip boundary is being crossed since Paste()
    // will add split lines around the pasted clip if so.
    for (const auto &clip : mClips) {
        double st;

        // Remember clip boundaries as locations to split
        st = LongSamplesToTime(TimeToLongSamples(clip->GetStartTime()));
        if (st >= t0 && st <= t1 && !make_iterator_range(splits).contains(st)) {
            splits.push_back(st);
        }

        st = LongSamplesToTime(TimeToLongSamples(clip->GetEndTime()));
        if (st >= t0 && st <= t1 && !make_iterator_range(splits).contains(st)) {
            splits.push_back(st);
        }

        // Search for cut lines
        auto &cutlines = clip->GetCutLines();
        // May erase from cutlines, so don't use range-for
        for (auto it = cutlines.begin(); it != cutlines.end();) {
            WaveClip *cut = it->get();
            double cs = LongSamplesToTime(TimeToLongSamples(clip->GetOffset() +
                                                            cut->GetOffset()));

            // Remember cut point
            if (cs >= t0 && cs <= t1) {

                // Remember the absolute offset and add to our cuts array.
                cut->SetOffset(cs);
                cuts.push_back(std::move(*it)); // transfer ownership!
                it = cutlines.erase(it);
            } else
                ++it;
        }
    }

    const auto tolerance = 2.0 / GetRate();

    // Now, clear the selection
    HandleClear(t0, t1, false, false);
    {

        // And paste in the NEW data
        Paste(t0, src);
        {
            // First, merge the NEW clip(s) in with the existing clips
            if (merge && splits.size() > 0) {
                // Now t1 represents the absolute end of the pasted data.
                t1 = t0 + src->GetEndTime();

                // Get a sorted array of the clips
                auto clips = SortedClipArray();

                // Scan the sorted clips for the first clip whose start time
                // exceeds the pasted regions end time.
                {
                    WaveClip *prev = nullptr;
                    for (const auto clip : clips) {
                        // Merge this clip and the previous clip if the end time
                        // falls within it and this isn't the first clip in the track.
                        if (fabs(t1 - clip->GetStartTime()) < tolerance) {
                            if (prev)
                                MergeClips(GetClipIndex(prev), GetClipIndex(clip));
                            break;
                        }
                        prev = clip;
                    }
                }
            }

            // Refill the array since clips have changed.
            auto clips = SortedClipArray();

            {
                // Scan the sorted clips to look for the start of the pasted
                // region.
                WaveClip *prev = nullptr;
                for (const auto clip : clips) {
                    if (prev) {
                        // It must be that clip is what was pasted and it begins where
                        // prev ends.
                        // use WEAK-GUARANTEE
                        MergeClips(GetClipIndex(prev), GetClipIndex(clip));
                        break;
                    }
                    if (fabs(t0 - clip->GetEndTime()) < tolerance)
                        // Merge this clip and the next clip if the start time
                        // falls within it and this isn't the last clip in the track.
                        prev = clip;
                    else
                        prev = nullptr;
                }
            }
        }

        // Restore cut/split lines
        if (preserve) {

            // Restore the split lines, transforming the position appropriately
            for (const auto split: splits) {
                SplitAt(warper->Warp(split));
            }

            // Restore the saved cut lines, also transforming if time altered
            for (const auto &clip : mClips) {
                double st;
                double et;

                st = clip->GetStartTime();
                et = clip->GetEndTime();

                // Scan the cuts for any that live within this clip
                for (auto it = cuts.begin(); it != cuts.end();) {
                    WaveClip *cut = it->get();
                    double cs = cut->GetOffset();

                    // Offset the cut from the start of the clip and add it to
                    // this clips cutlines.
                    if (cs >= st && cs <= et) {
                        cut->SetOffset(warper->Warp(cs) - st);
                        clip->GetCutLines().push_back(std::move(*it)); // transfer ownership!
                        it = cuts.erase(it);
                    } else
                        ++it;
                }
            }
        }
    }
}
