/**********************************************************************

  Audacity: A Digital Audio Editor

  Sequence.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_SEQUENCE__
#define __AUDACITY_SEQUENCE__

#include "MemoryX.h"
#include "Types.h"
#include "DirManager.h"
#include <vector>


class BlockFile;

class DirManager;

using BlockFilePtr = std::shared_ptr<BlockFile>;


// This is an internal data structure!  For advanced use only.
class SeqBlock {
public:
    BlockFilePtr f;
    ///the sample in the global wavetrack that this block starts at.
    sampleCount start;

    SeqBlock()
            : f{}, start(0) {}

    SeqBlock(const BlockFilePtr &f_, sampleCount start_)
            : f(f_), start(start_) {}

    // Construct a SeqBlock with changed start, same file
    SeqBlock Plus(sampleCount delta) const {
        return SeqBlock(f, start + delta);
    }
};

class BlockArray : public std::vector<SeqBlock> {
};

using BlockPtrArray = std::vector<SeqBlock *>; // non-owning pointers

class Sequence {
public:
    //
    // Constructor / Destructor / Duplicator
    //

    Sequence(const std::shared_ptr<DirManager> &projDirManager, sampleFormat format);

    // The copy constructor and duplicate operators take a
    // DirManager as a parameter, because you might be copying
    // from one project to another...
    Sequence(const Sequence &orig, const std::shared_ptr<DirManager> &projDirManager);

    Sequence &operator=(const Sequence &) = delete;

    ~Sequence();

   // This returns a possibly large or negative value
   sampleCount GetBlockStart(sampleCount position) const;

    //
    // Editing
    //

    //
    // This should only be used if you really, really know what
    // you're doing!
    //

    BlockArray &GetBlockArray() { return mBlock; }

    // These return a nonnegative number of samples meant to size a memory buffer
    size_t GetBestBlockSize(sampleCount start) const;

    size_t GetMaxBlockSize() const;

    sampleCount GetNumSamples() const { return mNumSamples; }

    bool Get(samplePtr buffer, sampleFormat format,
             sampleCount start, size_t len, bool mayThrow) const;

    // Note that len is not size_t, because nullptr may be passed for buffer, in
    // which case, silence is inserted, possibly a large amount.
    void SetSamples(samplePtr buffer, sampleFormat format,
                    sampleCount start, sampleCount len);

    void Append(samplePtr buffer, sampleFormat format, size_t len);

    size_t GetIdealAppendLen() const;
    //
    // Manipulating Sample Format
    //

    sampleFormat GetSampleFormat() const;

    void Delete(sampleCount start, sampleCount len);

    void Paste(sampleCount s0, const Sequence *src);

    // Return non-null, or else throw!
    std::unique_ptr<Sequence> Copy(sampleCount s0, sampleCount s1) const;

    const std::shared_ptr<DirManager> &GetDirManager() { return mDirManager; }

    int FindBlock(sampleCount pos) const;

    size_t GetIdealBlockSize() const;

    void AppendBlocksIfConsistent
            (BlockArray &additionalBlocks, bool replaceLast,
             sampleCount numSamples, const char *whereStr);

    // This function throws if the track is messed up
    // because of inconsistent block starts & lengths
    void ConsistencyCheck(const char *whereStr, bool mayThrow = true) const;

    // Accumulate NEW block files onto the end of a block array.
    // Does not change this sequence.  The intent is to use
    // CommitChangesIfConsistent later.
    static void Blockify
            (DirManager &dirManager, size_t maxSamples, sampleFormat format,
             BlockArray &list, sampleCount start, samplePtr buffer, size_t len);

    static void AppendBlock
            (DirManager &dirManager,
             BlockArray &blocks, sampleCount &numSamples, const SeqBlock &b);


    // Return true iff there is a change
    bool ConvertToSampleFormat(sampleFormat format);

private:

    bool mErrorOpening{false};

    static bool Read(samplePtr buffer, sampleFormat format,
                     const SeqBlock &b,
                     size_t blockRelativeStart, size_t len, bool mayThrow);

    bool Get(int b, samplePtr buffer, sampleFormat format,
             sampleCount start, size_t len, bool mayThrow) const;

    static void ConsistencyCheck
            (const BlockArray &block, size_t maxSamples, size_t from,
             sampleCount numSamples, const char *whereStr,
             bool mayThrow = true);

    // The next two are used in methods that give a strong guarantee.
    // They either throw because final consistency check fails, or swap the
    // changed contents into place.

    void CommitChangesIfConsistent
            (BlockArray &newBlock, sampleCount numSamples, const char *whereStr);

    //
    // Private static variables
    //

    static size_t sMaxDiskBlockSize;

    //
    // Private variables
    //

    BlockArray mBlock;
    sampleFormat mSampleFormat;

    // Not size_t!  May need to be large:
    sampleCount mNumSamples{0};

    size_t mMinSamples; // min samples per block
    size_t mMaxSamples; // max samples per block

    std::shared_ptr<DirManager> mDirManager;

};


#endif
