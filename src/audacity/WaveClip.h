/**********************************************************************

  Audacity: A Digital Audio Editor

  WaveClip.h

  ?? Dominic Mazzoni
  ?? Markus Meyer

*******************************************************************/

#ifndef __AUDACITY_WAVECLIP__
#define __AUDACITY_WAVECLIP__

#include <memory>
#include <vector>

#include <Audacity.h>
#include "Types.h"
#include "SampleFormat.h"
#include "Envelope.h"
#include "DirManager.h"

class WaveClip;

class Sequence;

// Array of pointers that assume ownership
using WaveClipHolder = std::shared_ptr<WaveClip>;
using WaveClipHolders = std::vector<WaveClipHolder>;
using WaveClipConstHolders = std::vector<std::shared_ptr<const WaveClip> >;

// Temporary arrays of mere pointers
using WaveClipPointers = std::vector<WaveClip *>;
using WaveClipConstPointers = std::vector<const WaveClip *>;


class WaveClip {
private:
    // It is an error to copy a WaveClip without specifying the DirManager.

    WaveClip(const WaveClip &) = delete;

    WaveClip &operator=(const WaveClip &) = delete;

public:
    // typical constructor
    WaveClip(const std::shared_ptr<DirManager> &projDirManager,
             sampleFormat format,
             int rate);

    // essentially a copy constructor - but you must pass in the
    // current project's DirManager, because we might be copying
    // from one project to another
    WaveClip(const WaveClip &orig,
             const std::shared_ptr<DirManager> &projDirManager,
             bool copyCutlines);

    // Copy only a range from the given WaveClip
    WaveClip(const WaveClip &orig,
             const std::shared_ptr<DirManager> &projDirManager,
             bool copyCutlines,
             double t0, double t1);

    virtual ~WaveClip();

    // Always gives non-negative answer, not more than sample sequence length
    // even if t0 really falls outside that range
    void TimeToSamplesClip(double t0, sampleCount *s0) const;

    sampleCount GetStartSample() const;

    sampleCount GetEndSample() const;

    sampleCount GetNumSamples() const;

    double GetStartTime() const;

    double GetEndTime() const;

    int GetRate() const { return mRate; }

    // Set rate without resampling. This will change the length of the clip
    void SetRate(int rate);

    bool GetSamples(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len, bool mayThrow = true) const;

    void SetSamples(samplePtr buffer, sampleFormat format,
                    sampleCount start, size_t len);

    /** WaveTrack calls this whenever data in the wave clip changes. It is
     * called automatically when WaveClip has a chance to know that something
     * has changed, like when member functions SetSamples() etc. are called. */
    void MarkChanged() // NOFAIL-GUARANTEE
    { mDirty++; }

    // Get low-level access to the sequence. Whenever possible, don't use this,
    // but use more high-level functions inside WaveClip (or add them if you
    // think they are useful for general use)
    Sequence *GetSequence() { return mSequence.get(); }

    /// Flush must be called after last Append
    void Flush();

    void SetOffset(double offset);

    double GetOffset() const { return mOffset; }

    void Offset(double delta) // NOFAIL-GUARANTEE
    { SetOffset(GetOffset() + delta); }

    /// You must call Flush after the last Append
    void Append(samplePtr buffer, sampleFormat format,
                size_t len, unsigned int stride = 1);

    /** Whenever you do an operation to the sequence that will change the number
     * of samples (that is, the length of the clip), you will want to call this
     * function to tell the envelope about it. */
    void UpdateEnvelopeTrackLen();

    bool BeforeClip(double t) const;

    bool AfterClip(double t) const;

    /// Clear, and add cut line that starts at t0 and contains everything until t1.
    void ClearAndAddCutLine(double t0, double t1);

    Envelope *GetEnvelope() { return mEnvelope.get(); }

    const Envelope *GetEnvelope() const { return mEnvelope.get(); }

    /// This name is consistent with WaveTrack::Clear. It performs a "Cut"
    /// operation (but without putting the cutted audio to the clipboard)
    void Clear(double t0, double t1);

    // One and only one of the following is true for a given t (unless the clip
    // has zero length -- then BeforeClip() and AfterClip() can both be true).
    // Within() is true if the time is substantially within the clip
    bool WithinClip(double t) const;

    /// Paste data from other clip, resampling it if not equal rate
    void Paste(double t0, const WaveClip *other);

    // Resample clip. This also will set the rate, but without changing
    // the length of the clip
    void Resample(int rate);

    void ConvertToSampleFormat(sampleFormat format);

    /// Offset cutlines right to time 't0' by time amount 'len'
    void OffsetCutLines(double t0, double len);

    // AWD, Oct 2009: for pasting whitespace at the end of selection
    bool GetIsPlaceholder() const { return mIsPlaceholder; }

    void SetIsPlaceholder(bool val) { mIsPlaceholder = val; }

    /// Get access to cut lines list
    WaveClipHolders &GetCutLines() { return mCutLines; }

    const WaveClipConstHolders &
    GetCutLines() const { return reinterpret_cast< const WaveClipConstHolders & >( mCutLines ); }

    size_t NumCutLines() const { return mCutLines.size(); }

protected:
    double mOffset{0};
    int mRate;
    int mDirty{0};
    std::unique_ptr<Sequence> mSequence;
    std::unique_ptr<Envelope> mEnvelope;

    SampleBuffer mAppendBuffer{};
    size_t mAppendBufferLen{0};

    // Cut Lines are nothing more than ordinary wave clips, with the
    // offset relative to the start of the clip.
    WaveClipHolders mCutLines{};

    // AWD, Oct. 2009: for whitespace-at-end-of-selection pasting
    bool mIsPlaceholder{false};
};


#endif
