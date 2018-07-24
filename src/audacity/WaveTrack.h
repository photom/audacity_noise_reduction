/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveTrack.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_WAVETRACK__
#define __AUDACITY_WAVETRACK__

#include <cassert>

#include "Types.h"
#include "WaveClip.h"
#include "DirManager.h"
#include "TimeWarper.h"

class WaveTrack {

public:

    //
    // Constructor / Destructor / Duplicator
    //
    // Private since only factories are allowed to construct WaveTracks
    //

    WaveTrack(const std::shared_ptr<DirManager> &projDirManager,
              sampleFormat format = (sampleFormat) 0,
              double rate = 0);

    WaveTrack(const WaveTrack &orig);

    void Init(const WaveTrack &orig);

public:

    enum {
        LeftChannel = 0,
        RightChannel = 1,
        MonoChannel = 2
    };

    enum TrackKindEnum {
        None,
        Wave,
#if defined(USE_MIDI)
        Note,
#endif
        Label,
        Time,
        All
    };

    // Get number of clips in this WaveTrack
    int GetNumClips() const;


    int GetKind() const { return Wave; }

    virtual ~WaveTrack();

    using Holder = std::unique_ptr<WaveTrack>;

    //
    // WaveTrack parameters
    //

    double GetRate() const;

    void SetRate(double newRate);

    // Multiplicative factor.  Only converted to dB for display.
    float GetGain() const;

    void SetGain(float newGain);

    // -1.0 (left) -> 1.0 (right)
    float GetPan() const;

    void SetPan(float newPan);

    /** @brief Get the time at which the first clip in the track starts
     *
     * @return time in seconds, or zero if there are no clips in the track
     */
    double GetStartTime() const;

    /** @brief Get the time at which the last clip in the track ends, plus
     * recorded stuff
     *
     * @return time in seconds, or zero if there are no clips in the track.
     */
    double GetEndTime() const;

    /** @brief Convert correctly between an (absolute) time in seconds and a number of samples.
     *
     * This method will not give the correct results if used on a relative time (difference of two
     * times). Each absolute time must be converted and the numbers of samples differenced:
     *    sampleCount start = track->TimeToLongSamples(t0);
     *    sampleCount end = track->TimeToLongSamples(t1);
     *    sampleCount len = (sampleCount)(end - start);
     * NOT the likes of:
     *    sampleCount len = track->TimeToLongSamples(t1 - t0);
     * See also WaveTrack::TimeToLongSamples().
     * @param t0 The time (floating point seconds) to convert
     * @return The number of samples from the start of the track which lie before the given time.
     */
    sampleCount TimeToLongSamples(double t0) const;

    /** @brief Convert correctly between an number of samples and an (absolute) time in seconds.
     *
     * @param pos The time number of samples from the start of the track to convert.
     * @return The time in seconds.
     */
    double LongSamplesToTime(sampleCount pos) const;

    // Get access to the (visible) clips in the tracks, in unspecified order
    // (not necessarioy sequenced in time).
    WaveClipHolders &GetClips() { return mClips; }

    const WaveClipConstHolders &GetClips() const { return reinterpret_cast< const WaveClipConstHolders & >( mClips ); }

    ///
    /// MM: Now that each wave track can contain multiple clips, we don't
    /// have a continous space of samples anymore, but we simulate it,
    /// because there are alot of places (e.g. effects) using this interface.
    /// This interface makes much sense for modifying samples, but note that
    /// it is not time-accurate, because the "offset" is a double value and
    /// therefore can lie inbetween samples. But as long as you use the
    /// same value for "start" in both calls to "Set" and "Get" it is
    /// guaranteed that the same samples are affected.
    ///
    bool Get(samplePtr buffer, sampleFormat format,
             sampleCount start, size_t len,
             fillFormat fill = fillZero, bool mayThrow = true, sampleCount *pNumCopied = nullptr) const;

    // These return a nonnegative number of samples meant to size a memory buffer
    size_t GetBestBlockSize(sampleCount t) const;

    size_t GetMaxBlockSize() const;

    // Create NEW clip and add it to this track. Returns a pointer
    // to the newly created clip.
    WaveClip *CreateClip();

    /** @brief Get access to the most recently added clip, or create a clip,
    *  if there is not already one.  THIS IS NOT NECESSARILY RIGHTMOST.
    *
    *  @return a pointer to the most recently added WaveClip
    */
    WaveClip *NewestOrNewClip();

    void Paste(double t0, const WaveTrack *src);

    /// Flush must be called after last Append
    void Flush();

    /** @brief Get access to the last (rightmost) clip, or create a clip,
    *  if there is not already one.
    *
    *  @return a pointer to a WaveClip at the end of the track
    */
    WaveClip *RightmostOrNewClip();

    void Append(samplePtr buffer, sampleFormat format,
                size_t len, unsigned int stride = 1);

    // May assume precondition: t0 <= t1
    void HandleClear(double t0, double t1, bool addCutLines, bool split);

    sampleFormat GetSampleFormat() const { return mFormat; }

    // Get the nth clip in this WaveTrack (will return NULL if not found).
    // Use this only in special cases (like getting the linked clip), because
    // it is much slower than GetClipIterator().
    WaveClip *GetClipByIndex(int index);

    const WaveClip *GetClipByIndex(int index) const;

    /** @brief Returns true if there are no WaveClips in the specified region
     *
     * @return true if no clips in the track overlap the specified time range,
     * false otherwise.
     */
    bool IsEmpty(double t0, double t1) const;

    void Set(samplePtr buffer, sampleFormat format,
             sampleCount start, size_t len);

    void SetChannel(int c) { mChannel = c; }

    virtual int GetChannel() const { return mChannel; };

    // This returns a possibly large or negative value
    sampleCount GetBlockStart(sampleCount t) const;

    // Fetch envelope values corresponding to uniformly separated sample times
    // starting at the given time.
    void GetEnvelopeValues(double *buffer, size_t bufferLen,
                           double t0) const;

    // Takes gain and pan into account
    float GetChannelGain(int channel) const;

    // May assume precondition: t0 <= t1
    void ClearAndPaste(double t0, double t1,
                       const WaveTrack *src,
                       bool preserve = true,
                       bool merge = true,
                       const TimeWarper *effectWarper = nullptr) /* not override */;

    // Add all wave clips to the given array 'clips' and sort the array by
    // clip start time. The array is emptied prior to adding the clips.
    WaveClipPointers SortedClipArray();

    WaveClipConstPointers SortedClipArray() const;

    // Get the linear index of a given clip (-1 if the clip is not found)
    int GetClipIndex(const WaveClip *clip) const;

    // Merge two clips, that is append data from clip2 to clip1,
    // then remove clip2 from track.
    // clipidx1 and clipidx2 are indices into the clip list.
    void MergeClips(int clipidx1, int clipidx2);

    void SplitAt(double t) /* not override */;

protected:
    //
    // Protected variables
    //

    WaveClipHolders mClips;

    sampleFormat mFormat;
    int mRate = 0;
    float mGain = 0.0;
    float mPan = 0.0;
    double mOffset = 0.0;

    int mChannel;
    mutable std::shared_ptr<DirManager> mDirManager;

};


// This is meant to be a short-lived object, during whose lifetime,
// the contents of the WaveTrack are known not to change.  It can replace
// repeated calls to WaveTrack::Get() (each of which opens and closes at least
// one block file).
class WaveTrackCache {
public:
    WaveTrackCache()
            : mBufferSize(0), mOverlapBuffer(), mNValidBuffers(0) {
    }

    explicit WaveTrackCache(const std::shared_ptr<const WaveTrack> &pTrack)
            : mBufferSize(0), mOverlapBuffer(), mNValidBuffers(0) {
        SetTrack(pTrack);
    }

    ~WaveTrackCache();

    const WaveTrack *GetTrack() const { return mPTrack.get(); }

    void SetTrack(const std::shared_ptr<const WaveTrack> &pTrack);

    // Uses fillZero always
    // Returns null on failure
    // Returned pointer may be invalidated if Get is called again
    // Do not DELETE[] the pointer
    constSamplePtr Get(
            sampleFormat format, sampleCount start, size_t len, bool mayThrow);

private:
    void Free();

    struct Buffer {
        Floats data;
        sampleCount start;
        sampleCount len;

        Buffer() : start(0), len(0) {}

        void Free() {
            data.reset();
            start = 0;
            len = 0;
        }

        sampleCount end() const { return start + len; }

        void swap(Buffer &other) {
            data.swap(other.data);
            std::swap(start, other.start);
            std::swap(len, other.len);
        }
    };

    std::shared_ptr<const WaveTrack> mPTrack;
    size_t mBufferSize;
    Buffer mBuffers[2];
    GrowableSampleBuffer mOverlapBuffer;
    int mNValidBuffers;
};

#endif // __AUDACITY_WAVETRACK__
