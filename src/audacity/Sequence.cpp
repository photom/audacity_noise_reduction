/**********************************************************************

  Audacity: A Digital Audio Editor

  Sequence.cpp

  Dominic Mazzoni

*******************************************************************//**
\file Sequence.cpp
\brief Implements classes Sequence and SeqBlock.

*//****************************************************************//**

\class Sequence
\brief A WaveTrack contains WaveClip(s).
   A WaveClip contains a Sequence. A Sequence is primarily an
   interface to an array of SeqBlock instances, corresponding to
   the audio BlockFiles on disk.
   Contrast with RingBuffer.

*//****************************************************************//**

\class SeqBlock
\brief Data structure containing pointer to a BlockFile and
   a start time. Element of a BlockArray.

*//*******************************************************************/


#include "Audacity.h"
#include "Sequence.h"
#include "SampleFormat.h"
#include "InconsistencyException.h"
#include "Utils.h"
#include "BlockFile.h"
#include "SilentBlockFile.h"

#include <algorithm>
#include <float.h>
#include <math.h>
#include <cstring>
#include <iostream>

size_t Sequence::sMaxDiskBlockSize = 1048576;

namespace {
    inline bool Overflows(double numSamples) {
        return numSamples > 9223372036854775807L;
    }
}

// Sequence methods
Sequence::Sequence(const std::shared_ptr<DirManager> &projDirManager, sampleFormat format)
        : mSampleFormat(format), mMinSamples(Sequence::sMaxDiskBlockSize / SAMPLE_SIZE(mSampleFormat) / 2),
          mMaxSamples(mMinSamples * 2),
          mDirManager(projDirManager) {
}

// essentially a copy constructor - but you must pass in the
// current project's DirManager, because we might be copying
// from one project to another
Sequence::Sequence(const Sequence &orig, const std::shared_ptr<DirManager> &projDirManager)
        : mSampleFormat(orig.mSampleFormat), mMinSamples(orig.mMinSamples), mMaxSamples(orig.mMaxSamples),
          mDirManager(projDirManager) {
    Paste(0, &orig);
}

Sequence::~Sequence() {
}

bool Sequence::Get(samplePtr buffer, sampleFormat format,
                   sampleCount start, size_t len, bool mayThrow) const {
    if (start == mNumSamples) {
        return len == 0;
    }

    if (start < 0 || start + len > mNumSamples) {
        if (mayThrow)
            THROW_INCONSISTENCY_EXCEPTION;
        ClearSamples(buffer, floatSample, 0, len);
        return false;
    }
    int b = FindBlock(start);

    return Get(b, buffer, format, start, len, mayThrow);
}

bool Sequence::Get(int b, samplePtr buffer, sampleFormat format,
                   sampleCount start, size_t len, bool mayThrow) const {
    bool result = true;
    while (len) {
        const SeqBlock &block = mBlock[b];
        // start is in block
        const auto bstart = (start - block.start).as_size_t();
        // bstart is not more than block length
        const auto blen = std::min(len, block.f->GetLength() - bstart);

        if (!Read(buffer, format, block, bstart, blen, mayThrow))
            result = false;

        len -= blen;
        buffer += (blen * SAMPLE_SIZE(format));
        b++;
        start += blen;
    }
    return result;
}

namespace {
    void ensureSampleBufferSize(SampleBuffer &buffer, sampleFormat format,
                                size_t &size, size_t required,
                                SampleBuffer *pSecondBuffer = nullptr) {
        // This should normally do nothing, but it is a defense against corrupt
        // projects than might have inconsistent block files bigger than the
        // expected maximum size.
        if (size < required) {
            // reallocate
            buffer.Allocate(required, format);
            if (pSecondBuffer && pSecondBuffer->ptr())
                pSecondBuffer->Allocate(required, format);
            if (!buffer.ptr() || (pSecondBuffer && !pSecondBuffer->ptr())) {
                // malloc failed
                // Perhaps required is a really crazy value,
                // and perhaps we should throw an AudacityException, but that is
                // a second-order concern
                THROW_INCONSISTENCY_EXCEPTION;
            }
            size = required;
        }
    }
}

// Pass NULL to set silence
void Sequence::SetSamples(samplePtr buffer, sampleFormat format,
                          sampleCount start, sampleCount len)
// STRONG-GUARANTEE
{
    const auto size = mBlock.size();

    if (start < 0 || start + len > mNumSamples)
        THROW_INCONSISTENCY_EXCEPTION;

    size_t tempSize = mMaxSamples;
    // to do:  allocate this only on demand
    SampleBuffer scratch(tempSize, mSampleFormat);

    SampleBuffer temp;
    if (buffer && format != mSampleFormat) {
        temp.Allocate(tempSize, mSampleFormat);
    }

    int b = FindBlock(start);
    BlockArray newBlock;
    std::copy(mBlock.begin(), mBlock.begin() + b, std::back_inserter(newBlock));

    while (len > 0
           // Redundant termination condition,
           // but it guards against infinite loop in case of inconsistencies
           // (too-small files, not yet seen?)
           // that cause the loop to make no progress because blen == 0
           && b < (int) size
            ) {
        newBlock.push_back(mBlock[b]);
        SeqBlock &block = newBlock.back();
        // start is within block
        const auto bstart = (start - block.start).as_size_t();
        const auto fileLength = block.f->GetLength();

        // the std::min is a guard against inconsistent Sequence
        const auto blen =
                limitSampleBufferSize(fileLength - std::min(bstart, fileLength),
                                      len);
        assert(blen == 0 || bstart + blen <= fileLength);

#if 0
        // PRL:  This inconsistency (too-big file) has been seen in "the wild"
        // in 2.2.0.  It is the least problematic kind of inconsistency.
        // We will tolerate it for 2.2.1.
        // Not known whether it is only in projects saved in earlier versions.
        // After 2.2.1, we should detect and correct it at file loading time.
        if (fileLength > mMaxSamples) {
           THROW_INCONSISTENCY_EXCEPTION;
        }
#endif

        ensureSampleBufferSize(scratch, mSampleFormat, tempSize, fileLength,
                               &temp);

        samplePtr useBuffer = buffer;
        if (buffer && format != mSampleFormat) {
            // To do: remove the extra movement.
            // Note: we ensured temp can hold fileLength.  blen is not more
            CopySamples(buffer, format, temp.ptr(), mSampleFormat, blen);
            useBuffer = temp.ptr();
        }

        // We don't ever write to an existing block; to support Undo,
        // we copy the old block entirely into memory, dereference it,
        // make the change, and then write the NEW block to disk.

        if (bstart > 0 || blen < fileLength) {
            // First or last block is only partially overwritten
            Read(scratch.ptr(), mSampleFormat, block, 0, fileLength, true);

            if (useBuffer) {
                auto sampleSize = SAMPLE_SIZE(mSampleFormat);
                memcpy(scratch.ptr() +
                       bstart * sampleSize, useBuffer, blen * sampleSize);
            } else
                ClearSamples(scratch.ptr(), mSampleFormat, bstart, blen);

            block.f = mDirManager->NewSimpleBlockFile(
                    scratch.ptr(), fileLength, mSampleFormat);
        } else {
            // Avoid reading the disk when the replacement is total
            if (useBuffer)
                block.f = mDirManager->NewSimpleBlockFile(
                        useBuffer, fileLength, mSampleFormat);
            else
                block.f = make_blockfile<SilentBlockFile>(fileLength);
        }

        // blen might be zero for inconsistent Sequence...
        if (buffer)
            buffer += (blen * SAMPLE_SIZE(format));

        len -= blen;
        start += blen;

        // ... but this, at least, always guarantees some loop progress:
        b++;
    }

    std::copy(mBlock.begin() + b, mBlock.end(), std::back_inserter(newBlock));

}

size_t Sequence::GetMaxBlockSize() const {
    return mMaxSamples;
}

size_t Sequence::GetBestBlockSize(sampleCount start) const {
    // This method returns a nice number of samples you should try to grab in
    // one big chunk in order to land on a block boundary, based on the starting
    // sample.  The value returned will always be nonzero and will be no larger
    // than the value of GetMaxBlockSize()

    if (start < 0 || start >= mNumSamples)
        return mMaxSamples;

    int b = FindBlock(start);
    int numBlocks = mBlock.size();

    const SeqBlock &block = mBlock[b];
    // start is in block:
    auto result = (block.start + block.f->GetLength() - start).as_size_t();

    decltype(result) length;
    while (result < mMinSamples && b + 1 < numBlocks &&
           ((length = mBlock[b + 1].f->GetLength()) + result) <= mMaxSamples) {
        b++;
        result += length;
    }

    assert(result > 0 && result <= mMaxSamples);

    return result;
}

void Sequence::Append(samplePtr buffer, sampleFormat format,
                      size_t len)
// STRONG-GUARANTEE
{
    if (len == 0)
        return;

    // Quick check to make sure that it doesn't overflow
    if (Overflows(mNumSamples.as_double() + ((double) len)))
        THROW_INCONSISTENCY_EXCEPTION;

    BlockArray newBlock;
    sampleCount newNumSamples = mNumSamples;

    // If the last block is not full, we need to add samples to it
    int numBlocks = mBlock.size();
    SeqBlock *pLastBlock;
    decltype(pLastBlock->f->GetLength()) length;
    size_t bufferSize = mMaxSamples;
    SampleBuffer buffer2(bufferSize, mSampleFormat);
    bool replaceLast = false;
    if (numBlocks > 0 &&
        (length =
                 (pLastBlock = &mBlock.back())->f->GetLength()) < mMinSamples) {
        // Enlarge a sub-minimum block at the end
        const SeqBlock &lastBlock = *pLastBlock;
        const auto addLen = std::min(mMaxSamples - length, len);

        Read(buffer2.ptr(), mSampleFormat, lastBlock, 0, length, true);

        CopySamples(buffer,
                    format,
                    buffer2.ptr() + length * SAMPLE_SIZE(mSampleFormat),
                    mSampleFormat,
                    addLen);

        const auto newLastBlockLen = length + addLen;

        SeqBlock newLastBlock(
                mDirManager->NewSimpleBlockFile(
                        buffer2.ptr(), newLastBlockLen, mSampleFormat),
                lastBlock.start
        );

        newBlock.push_back(newLastBlock);

        len -= addLen;
        newNumSamples += addLen;
        buffer += addLen * SAMPLE_SIZE(format);

        replaceLast = true;
    }
    // Append the rest as NEW blocks
    while (len) {
        const auto idealSamples = GetIdealBlockSize();
        const auto addedLen = std::min(idealSamples, len);
        BlockFilePtr pFile;
        if (format == mSampleFormat) {
            pFile = mDirManager->NewSimpleBlockFile(
                    buffer, addedLen, mSampleFormat);
        } else {
            CopySamples(buffer, format, buffer2.ptr(), mSampleFormat, addedLen);
            pFile = mDirManager->NewSimpleBlockFile(
                    buffer2.ptr(), addedLen, mSampleFormat);
        }

        newBlock.push_back(SeqBlock(pFile, newNumSamples));

        buffer += addedLen * SAMPLE_SIZE(format);
        newNumSamples += addedLen;
        len -= addedLen;
    }

    AppendBlocksIfConsistent(newBlock, replaceLast,
                             newNumSamples, "Append");

// JKC: During generate we use Append again and again.
// If generating a long sequence this test would give O(n^2)
// performance - not good!
#ifdef VERY_SLOW_CHECKING
    ConsistencyCheck(wxT("Append"));
#endif
}

sampleFormat Sequence::GetSampleFormat() const {
    return mSampleFormat;
}

size_t Sequence::GetIdealAppendLen() const {
    int numBlocks = mBlock.size();
    const auto max = GetMaxBlockSize();

    if (numBlocks == 0)
        return max;

    const auto lastBlockLen = mBlock.back().f->GetLength();
    if (lastBlockLen >= max)
        return max;
    else
        return max - lastBlockLen;
}

void Sequence::Delete(sampleCount start, sampleCount len)
// STRONG-GUARANTEE
{
    if (len == 0)
        return;

    if (len < 0 || start < 0 || start + len > mNumSamples)
        THROW_INCONSISTENCY_EXCEPTION;

    //TODO: add a ref-deref mechanism to SeqBlock/BlockArray so we don't have to make this a critical section.
    //On-demand threads iterate over the mBlocks and the GUI thread deletes them, so for now put a mutex here over
    //both functions,
    // DeleteUpdateMutexLocker locker(*this);

    const unsigned int numBlocks = mBlock.size();

    const unsigned int b0 = FindBlock(start);
    unsigned int b1 = FindBlock(start + len - 1);

    auto sampleSize = SAMPLE_SIZE(mSampleFormat);

    SeqBlock *pBlock;
    decltype(pBlock->f->GetLength()) length;

    // One buffer for reuse in various branches here
    SampleBuffer scratch;
    // The maximum size that should ever be needed
    auto scratchSize = mMaxSamples + mMinSamples;

    // Special case: if the samples to DELETE are all within a single
    // block and the resulting length is not too small, perform the
    // deletion within this block:
    if (b0 == b1 &&
        (length = (pBlock = &mBlock[b0])->f->GetLength()) - len >= mMinSamples) {
        SeqBlock &b = *pBlock;
        // start is within block
        auto pos = (start - b.start).as_size_t();

        // Guard against failure of this anyway below with limitSampleBufferSize
        assert(len < length);

        // len must be less than length
        // because start + len - 1 is also in the block...
        auto newLen = (length - limitSampleBufferSize(length, len));

        scratch.Allocate(scratchSize, mSampleFormat);
        ensureSampleBufferSize(scratch, mSampleFormat, scratchSize, newLen);

        Read(scratch.ptr(), mSampleFormat, b, 0, pos, true);
        Read(scratch.ptr() + (pos * sampleSize), mSampleFormat,
             b,
                // ... and therefore pos + len
                // is not more than the length of the block
             (pos + len).as_size_t(), newLen - pos, true);

        auto newFile =
                mDirManager->NewSimpleBlockFile(scratch.ptr(), newLen, mSampleFormat);

        // Don't make a duplicate array.  We can still give STRONG-GUARANTEE
        // if we modify only one block in place.

        // use NOFAIL-GUARANTEE in remaining steps

        b.f = newFile;

        for (unsigned int j = b0 + 1; j < numBlocks; j++)
            mBlock[j].start -= len;

        mNumSamples -= len;

        // This consistency check won't throw, it asserts.
        // Proof that we kept consistency is not hard.
        ConsistencyCheck("Delete - branch one", false);
        return;
    }

    // Create a NEW array of blocks
    BlockArray newBlock;
    newBlock.reserve(numBlocks - (b1 - b0) + 2);

    // Copy the blocks before the deletion point over to
    // the NEW array
    newBlock.insert(newBlock.end(), mBlock.begin(), mBlock.begin() + b0);
    unsigned int i;

    // First grab the samples in block b0 before the deletion point
    // into preBuffer.  If this is enough samples for its own block,
    // or if this would be the first block in the array, write it out.
    // Otherwise combine it with the previous block (splitting them
    // 50/50 if necessary).
    const SeqBlock &preBlock = mBlock[b0];
    // start is within preBlock
    auto preBufferLen = (start - preBlock.start).as_size_t();
    if (preBufferLen) {
        if (preBufferLen >= mMinSamples || b0 == 0) {
            if (!scratch.ptr())
                scratch.Allocate(scratchSize, mSampleFormat);
            ensureSampleBufferSize(scratch, mSampleFormat, scratchSize, preBufferLen);
            Read(scratch.ptr(), mSampleFormat, preBlock, 0, preBufferLen, true);
            auto pFile =
                    mDirManager->NewSimpleBlockFile(scratch.ptr(), preBufferLen, mSampleFormat);

            newBlock.push_back(SeqBlock(pFile, preBlock.start));
        } else {
            const SeqBlock &prepreBlock = mBlock[b0 - 1];
            const auto prepreLen = prepreBlock.f->GetLength();
            const auto sum = prepreLen + preBufferLen;

            if (!scratch.ptr())
                scratch.Allocate(scratchSize, mSampleFormat);
            ensureSampleBufferSize(scratch, mSampleFormat, scratchSize,
                                   sum);

            Read(scratch.ptr(), mSampleFormat, prepreBlock, 0, prepreLen, true);
            Read(scratch.ptr() + prepreLen * sampleSize, mSampleFormat,
                 preBlock, 0, preBufferLen, true);

            newBlock.pop_back();
            Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                     newBlock, prepreBlock.start, scratch.ptr(), sum);
        }
    } else {
        // The sample where we begin deletion happens to fall
        // right on the beginning of a block.
    }

    // Now, symmetrically, grab the samples in block b1 after the
    // deletion point into postBuffer.  If this is enough samples
    // for its own block, or if this would be the last block in
    // the array, write it out.  Otherwise combine it with the
    // subsequent block (splitting them 50/50 if necessary).
    const SeqBlock &postBlock = mBlock[b1];
    // start + len - 1 lies within postBlock
    const auto postBufferLen = (
            (postBlock.start + postBlock.f->GetLength()) - (start + len)
    ).as_size_t();
    if (postBufferLen) {
        if (postBufferLen >= mMinSamples || b1 == numBlocks - 1) {
            if (!scratch.ptr())
                // Last use of scratch, can ask for smaller
                scratch.Allocate(postBufferLen, mSampleFormat);
            // start + len - 1 lies within postBlock
            auto pos = (start + len - postBlock.start).as_size_t();
            Read(scratch.ptr(), mSampleFormat, postBlock, pos, postBufferLen, true);
            auto file =
                    mDirManager->NewSimpleBlockFile(scratch.ptr(), postBufferLen, mSampleFormat);

            newBlock.push_back(SeqBlock(file, start));
        } else {
            SeqBlock &postpostBlock = mBlock[b1 + 1];
            const auto postpostLen = postpostBlock.f->GetLength();
            const auto sum = postpostLen + postBufferLen;

            if (!scratch.ptr())
                // Last use of scratch, can ask for smaller
                scratch.Allocate(sum, mSampleFormat);
            // start + len - 1 lies within postBlock
            auto pos = (start + len - postBlock.start).as_size_t();
            Read(scratch.ptr(), mSampleFormat, postBlock, pos, postBufferLen, true);
            Read(scratch.ptr() + (postBufferLen * sampleSize), mSampleFormat,
                 postpostBlock, 0, postpostLen, true);

            Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                     newBlock, start, scratch.ptr(), sum);
            b1++;
        }
    } else {
        // The sample where we begin deletion happens to fall
        // right on the end of a block.
    }

    // Copy the remaining blocks over from the old array
    for (i = b1 + 1; i < numBlocks; i++)
        newBlock.push_back(mBlock[i].Plus(-len));

    CommitChangesIfConsistent
            (newBlock, mNumSamples - len, "Delete - branch two");
}

void Sequence::Paste(sampleCount s, const Sequence *src)
// STRONG-GUARANTEE
{
    if ((s < 0) || (s > mNumSamples)) {
        std::cerr <<
                  string_format("Sequence::Paste: sampleCount s %lf is < 0 or > mNumSamples %lf).",
                          // PRL:  Why bother with Internat when the above is just wxT?
                                s.as_double(), mNumSamples.as_double()) << std::endl;
        THROW_INCONSISTENCY_EXCEPTION;
    }

    // Quick check to make sure that it doesn't overflow
    if (Overflows((mNumSamples.as_double()) + (src->mNumSamples.as_double()))) {
        std::cerr <<
                  string_format("Sequence::Paste: mNumSamples %lf + src->mNumSamples %lf would overflow.",
                          // PRL:  Why bother with Internat when the above is just wxT?
                                mNumSamples.as_double(), src->mNumSamples.as_double()) << std::endl;
        THROW_INCONSISTENCY_EXCEPTION;
    }

    if (src->mSampleFormat != mSampleFormat) {
        std::cerr << string_format(
                "Sequence::Paste: Sample format to be pasted, %s, does not match destination format, %s.",
                GetSampleFormatStr(src->mSampleFormat), GetSampleFormatStr(src->mSampleFormat)) << std::endl;
        THROW_INCONSISTENCY_EXCEPTION;
    }

    const BlockArray &srcBlock = src->mBlock;
    auto addedLen = src->mNumSamples;
    const unsigned int srcNumBlocks = srcBlock.size();
    auto sampleSize = SAMPLE_SIZE(mSampleFormat);

    if (addedLen == 0 || srcNumBlocks == 0)
        return;

    const size_t numBlocks = mBlock.size();

    if (numBlocks == 0 ||
        (s == mNumSamples && mBlock.back().f->GetLength() >= mMinSamples)) {
        // Special case: this track is currently empty, or it's safe to append
        // onto the end because the current last block is longer than the
        // minimum size

        // Build and swap a copy so there is a strong exception safety guarantee
        BlockArray newBlock{mBlock};
        sampleCount samples = mNumSamples;
        for (unsigned int i = 0; i < srcNumBlocks; i++)
            // AppendBlock may throw for limited disk space, if pasting from
            // one project into another.
            AppendBlock(*mDirManager, newBlock, samples, srcBlock[i]);
        // Increase ref count or duplicate file

        CommitChangesIfConsistent
                (newBlock, samples, "Paste branch one");
        return;
    }

    const int b = (s == mNumSamples) ? mBlock.size() - 1 : FindBlock(s);
    assert((b >= 0) && (b < (int) numBlocks));
    SeqBlock *const pBlock = &mBlock[b];
    const auto length = pBlock->f->GetLength();
    const auto largerBlockLen = addedLen + length;
    // PRL: when insertion point is the first sample of a block,
    // and the following test fails, perhaps we could test
    // whether coalescence with the previous block is possible.
    if (largerBlockLen <= mMaxSamples) {
        // Special case: we can fit all of the NEW samples inside of
        // one block!

        SeqBlock &block = *pBlock;
        // largerBlockLen is not more than mMaxSamples...
        SampleBuffer buffer(largerBlockLen.as_size_t(), mSampleFormat);

        // ...and addedLen is not more than largerBlockLen
        auto sAddedLen = addedLen.as_size_t();
        // s lies within block:
        auto splitPoint = (s - block.start).as_size_t();
        Read(buffer.ptr(), mSampleFormat, block, 0, splitPoint, true);
        src->Get(0, buffer.ptr() + splitPoint * sampleSize,
                 mSampleFormat, 0, sAddedLen, true);
        Read(buffer.ptr() + (splitPoint + sAddedLen) * sampleSize,
             mSampleFormat, block,
             splitPoint, length - splitPoint, true);

        auto file =
                mDirManager->NewSimpleBlockFile(
                        // largerBlockLen is not more than mMaxSamples...
                        buffer.ptr(), largerBlockLen.as_size_t(), mSampleFormat);

        // Don't make a duplicate array.  We can still give STRONG-GUARANTEE
        // if we modify only one block in place.

        // use NOFAIL-GUARANTEE in remaining steps
        block.f = file;

        for (unsigned int i = b + 1; i < numBlocks; i++)
            mBlock[i].start += addedLen;

        mNumSamples += addedLen;

        // This consistency check won't throw, it asserts.
        // Proof that we kept consistency is not hard.
        ConsistencyCheck("Paste branch two", false);
        return;
    }

    // Case three: if we are inserting four or fewer blocks,
    // it's simplest to just lump all the data together
    // into one big block along with the split block,
    // then resplit it all
    BlockArray newBlock;
    newBlock.reserve(numBlocks + srcNumBlocks + 2);
    newBlock.insert(newBlock.end(), mBlock.begin(), mBlock.begin() + b);

    SeqBlock &splitBlock = mBlock[b];
    auto splitLen = splitBlock.f->GetLength();
    // s lies within splitBlock
    auto splitPoint = (s - splitBlock.start).as_size_t();

    unsigned int i;
    if (srcNumBlocks <= 4) {

        // addedLen is at most four times maximum block size
        auto sAddedLen = addedLen.as_size_t();
        const auto sum = splitLen + sAddedLen;

        SampleBuffer sumBuffer(sum, mSampleFormat);
        Read(sumBuffer.ptr(), mSampleFormat, splitBlock, 0, splitPoint, true);
        src->Get(0, sumBuffer.ptr() + splitPoint * sampleSize,
                 mSampleFormat,
                 0, sAddedLen, true);
        Read(sumBuffer.ptr() + (splitPoint + sAddedLen) * sampleSize, mSampleFormat,
             splitBlock, splitPoint,
             splitLen - splitPoint, true);

        Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                 newBlock, splitBlock.start, sumBuffer.ptr(), sum);
    } else {

        // The final case is that we're inserting at least five blocks.
        // We divide these into three groups: the first two get merged
        // with the first half of the split block, the middle ones get
        // copied in as is, and the last two get merged with the last
        // half of the split block.

        const auto srcFirstTwoLen =
                srcBlock[0].f->GetLength() + srcBlock[1].f->GetLength();
        const auto leftLen = splitPoint + srcFirstTwoLen;

        const SeqBlock &penultimate = srcBlock[srcNumBlocks - 2];
        const auto srcLastTwoLen =
                penultimate.f->GetLength() +
                srcBlock[srcNumBlocks - 1].f->GetLength();
        const auto rightSplit = splitBlock.f->GetLength() - splitPoint;
        const auto rightLen = rightSplit + srcLastTwoLen;

        SampleBuffer sampleBuffer(std::max(leftLen, rightLen), mSampleFormat);

        Read(sampleBuffer.ptr(), mSampleFormat, splitBlock, 0, splitPoint, true);
        src->Get(0, sampleBuffer.ptr() + splitPoint * sampleSize,
                 mSampleFormat, 0, srcFirstTwoLen, true);

        Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                 newBlock, splitBlock.start, sampleBuffer.ptr(), leftLen);

        for (i = 2; i < srcNumBlocks - 2; i++) {
            const SeqBlock &block = srcBlock[i];
            auto file = mDirManager->CopyBlockFile(block.f);
            // We can assume file is not null
            newBlock.push_back(SeqBlock(file, block.start + s));
        }

        auto lastStart = penultimate.start;
        src->Get(srcNumBlocks - 2, sampleBuffer.ptr(), mSampleFormat,
                 lastStart, srcLastTwoLen, true);
        Read(sampleBuffer.ptr() + srcLastTwoLen * sampleSize, mSampleFormat,
             splitBlock, splitPoint, rightSplit, true);

        Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                 newBlock, s + lastStart, sampleBuffer.ptr(), rightLen);
    }

    // Copy remaining blocks to NEW block array and
    // swap the NEW block array in for the old
    for (i = b + 1; i < numBlocks; i++)
        newBlock.push_back(mBlock[i].Plus(addedLen));

    CommitChangesIfConsistent
            (newBlock, mNumSamples + addedLen, "Paste branch three");
}

std::unique_ptr<Sequence> Sequence::Copy(sampleCount s0, sampleCount s1) const {
    auto dest = std::make_unique<Sequence>(mDirManager, mSampleFormat);
    if (s0 >= s1 || s0 >= mNumSamples || s1 < 0) {
        return dest;
    }

    int numBlocks = mBlock.size();

    int b0 = FindBlock(s0);
    const int b1 = FindBlock(s1 - 1);
    assert(b0 >= 0);
    assert(b0 < numBlocks);
    assert(b1 < numBlocks);
    assert(b0 <= b1);

    dest->mBlock.reserve(b1 - b0 + 1);

    auto bufferSize = mMaxSamples;
    SampleBuffer buffer(bufferSize, mSampleFormat);

    int blocklen;

    // Do the first block

    const SeqBlock &block0 = mBlock[b0];
    if (s0 != block0.start) {
        const auto &file = block0.f;
        // Nonnegative result is length of block0 or less:
        blocklen =
                (std::min(s1, block0.start + file->GetLength()) - s0).as_size_t();
        assert(file->IsAlias() || (blocklen <= (int) mMaxSamples)); // Vaughan, 2012-02-29
        ensureSampleBufferSize(buffer, mSampleFormat, bufferSize, blocklen);
        Get(b0, buffer.ptr(), mSampleFormat, s0, blocklen, true);

        dest->Append(buffer.ptr(), mSampleFormat, blocklen);
    } else
        --b0;

    // If there are blocks in the middle, copy the blockfiles directly
    for (int bb = b0 + 1; bb < b1; ++bb)
        AppendBlock(*dest->mDirManager, dest->mBlock, dest->mNumSamples, mBlock[bb]);
    // Increase ref count or duplicate file

    // Do the last block
    if (b1 > b0) {
        const SeqBlock &block = mBlock[b1];
        const auto &file = block.f;
        // s1 is within block:
        blocklen = (s1 - block.start).as_size_t();
        assert(file->IsAlias() || (blocklen <= (int) mMaxSamples)); // Vaughan, 2012-02-29
        if (blocklen < (int) file->GetLength()) {
            ensureSampleBufferSize(buffer, mSampleFormat, bufferSize, blocklen);
            Get(b1, buffer.ptr(), mSampleFormat, block.start, blocklen, true);
            dest->Append(buffer.ptr(), mSampleFormat, blocklen);
        } else
            // Special case, copy exactly
            AppendBlock(*dest->mDirManager, dest->mBlock, dest->mNumSamples, block);
        // Increase ref count or duplicate file
    }

    dest->ConsistencyCheck("Sequence::Copy()");

    return dest;
}

int Sequence::FindBlock(sampleCount pos) const {
    assert(pos >= 0 && pos < mNumSamples);

    if (pos == 0)
        return 0;

    int numBlocks = mBlock.size();

    size_t lo = 0, hi = numBlocks, guess;
    sampleCount loSamples = 0, hiSamples = mNumSamples;

    while (true) {
        //this is not a binary search, but a
        //dictionary search where we guess something smarter than the binary division
        //of the unsearched area, since samples are usually proportional to block file number.
        const double frac = (pos - loSamples).as_double() /
                            (hiSamples - loSamples).as_double();
        guess = std::min(hi - 1, lo + size_t(frac * (hi - lo)));
        const SeqBlock &block = mBlock[guess];

        assert(block.f->GetLength() > 0);
        assert(lo <= guess && guess < hi && lo < hi);

        if (pos < block.start) {
            assert(lo != guess);
            hi = guess;
            hiSamples = block.start;
        } else {
            const sampleCount nextStart = block.start + block.f->GetLength();
            if (pos < nextStart)
                break;
            else {
                assert(guess < hi - 1);
                lo = guess + 1;
                loSamples = nextStart;
            }
        }
    }

    const int rval = guess;
    assert(rval >= 0 && rval < numBlocks &&
           pos >= mBlock[rval].start &&
           pos < mBlock[rval].start + mBlock[rval].f->GetLength());

    return rval;
}

sampleCount Sequence::GetBlockStart(sampleCount position) const
{
   int b = FindBlock(position);
   return mBlock[b].start;
}

//static
bool Sequence::Read(samplePtr buffer, sampleFormat format,
                    const SeqBlock &b, size_t blockRelativeStart, size_t len,
                    bool mayThrow) {
    const auto &f = b.f;

    assert(blockRelativeStart + len <= f->GetLength());

    // Either throws, or of !mayThrow, tells how many were really read
    auto result = f->ReadData(buffer, format, blockRelativeStart, len, mayThrow);

    if (result != len) {
        std::string msg = string_format("Expected to read %ld samples, got %ld samples.", len, result);
        std::cerr << msg << std::endl;
        return false;
    }

    return true;
}

size_t Sequence::GetIdealBlockSize() const {
    return mMaxSamples;
}

void Sequence::AppendBlocksIfConsistent
        (BlockArray &additionalBlocks, bool replaceLast,
         sampleCount numSamples, const char *whereStr) {
    // Any additional blocks are meant to be appended,
    // replacing the final block if there was one.

    if (additionalBlocks.empty())
        return;

    bool tmpValid = false;
    SeqBlock tmp;

    if (replaceLast && !mBlock.empty()) {
        tmp = mBlock.back(), tmpValid = true;
        mBlock.pop_back();
    }

    auto prevSize = mBlock.size();

    bool consistent = false;
    auto cleanup = finally([&] {
        if (!consistent) {
            mBlock.resize(prevSize);
            if (tmpValid)
                mBlock.push_back(tmp);
        }
    });

    std::copy(additionalBlocks.begin(), additionalBlocks.end(),
              std::back_inserter(mBlock));

    // Check consistency only of the blocks that were added,
    // avoiding quadratic time for repeated checking of repeating appends
    ConsistencyCheck(mBlock, mMaxSamples, prevSize, numSamples, whereStr); // may throw

    // now commit
    // use NOFAIL-GUARANTEE

    mNumSamples = numSamples;
    consistent = true;
}

void Sequence::ConsistencyCheck(const char *whereStr, bool mayThrow) const {
    ConsistencyCheck(mBlock, mMaxSamples, 0, mNumSamples, whereStr, mayThrow);
}

void Sequence::ConsistencyCheck
        (const BlockArray &mBlock, size_t maxSamples, size_t from,
         sampleCount mNumSamples, const char *whereStr,
         bool mayThrow) {
    bool bError = false;
    // Construction of the exception at the appropriate line of the function
    // gives a little more discrimination
    InconsistencyException ex;

    unsigned int numBlocks = mBlock.size();

    unsigned int i;
    sampleCount pos = from < numBlocks ? mBlock[from].start : mNumSamples;
    if (from == 0 && pos != 0)
        ex = CONSTRUCT_INCONSISTENCY_EXCEPTION, bError = true;

    for (i = from; !bError && i < numBlocks; i++) {
        const SeqBlock &seqBlock = mBlock[i];
        if (pos != seqBlock.start)
            ex = CONSTRUCT_INCONSISTENCY_EXCEPTION, bError = true;

        if (seqBlock.f) {
            const auto length = seqBlock.f->GetLength();
            if (length > maxSamples)
                ex = CONSTRUCT_INCONSISTENCY_EXCEPTION, bError = true;
            pos += length;
        } else
            ex = CONSTRUCT_INCONSISTENCY_EXCEPTION, bError = true;
    }
    if (!bError && pos != mNumSamples)
        ex = CONSTRUCT_INCONSISTENCY_EXCEPTION, bError = true;

    if (bError) {
        std::string msg = string_format("*** Consistency check failed at %d after %s. ***", ex.GetLine(), whereStr);
        std::cerr << msg << std::endl;
        // wxString str;
        // DebugPrintf(mBlock, mNumSamples, &str);
        // std::cerr << str;
        std::cerr << "*** Please report this error to https://forum.audacityteam.org/. ***\n\n"
                     "Recommended course of action:\n"
                     "Undo the failed operation(s), then export or save your work and quit." << std::endl;

        //if (mayThrow)
        //throw ex;
        //else
        assert(false);
    }
}

void Sequence::Blockify
        (DirManager &mDirManager, size_t mMaxSamples, sampleFormat mSampleFormat,
         BlockArray &list, sampleCount start, samplePtr buffer, size_t len) {
    if (len <= 0)
        return;
    auto num = (len + (mMaxSamples - 1)) / mMaxSamples;
    list.reserve(list.size() + num);

    for (decltype(num) i = 0; i < num; i++) {
        SeqBlock b;

        const auto offset = i * len / num;
        b.start = start + offset;
        int newLen = ((i + 1) * len / num) - offset;
        samplePtr bufStart = buffer + (offset * SAMPLE_SIZE(mSampleFormat));

        b.f = mDirManager.NewSimpleBlockFile(bufStart, newLen, mSampleFormat);

        list.push_back(b);
    }
}

void Sequence::CommitChangesIfConsistent
        (BlockArray &newBlock, sampleCount numSamples, const char *whereStr) {
    ConsistencyCheck(newBlock, mMaxSamples, 0, numSamples, whereStr); // may throw

    // now commit
    // use NOFAIL-GUARANTEE

    mBlock.swap(newBlock);
    mNumSamples = numSamples;
}

bool Sequence::ConvertToSampleFormat(sampleFormat format)
// STRONG-GUARANTEE
{
    if (format == mSampleFormat)
        // no change
        return false;

    if (mBlock.size() == 0) {
        mSampleFormat = format;
        return true;
    }

    const sampleFormat oldFormat = mSampleFormat;
    mSampleFormat = format;

    const auto oldMinSamples = mMinSamples, oldMaxSamples = mMaxSamples;
    // These are the same calculations as in the constructor.
    mMinSamples = sMaxDiskBlockSize / SAMPLE_SIZE(mSampleFormat) / 2;
    mMaxSamples = mMinSamples * 2;

    bool bSuccess = false;
    auto cleanup = finally([&] {
        if (!bSuccess) {
            // Conversion failed. Revert these member vars.
            mSampleFormat = oldFormat;
            mMaxSamples = oldMaxSamples;
            mMinSamples = oldMinSamples;
        }
    });

    BlockArray newBlockArray;
    // Use the ratio of old to NEW mMaxSamples to make a reasonable guess
    // at allocation.
    newBlockArray.reserve
            (1 + mBlock.size() * ((float) oldMaxSamples / (float) mMaxSamples));

    {
        size_t oldSize = oldMaxSamples;
        SampleBuffer bufferOld(oldSize, oldFormat);
        size_t newSize = oldMaxSamples;
        SampleBuffer bufferNew(newSize, format);

        for (size_t i = 0, nn = mBlock.size(); i < nn; i++) {
            SeqBlock &oldSeqBlock = mBlock[i];
            const auto &oldBlockFile = oldSeqBlock.f;
            const auto len = oldBlockFile->GetLength();
            ensureSampleBufferSize(bufferOld, oldFormat, oldSize, len);
            Read(bufferOld.ptr(), oldFormat, oldSeqBlock, 0, len, true);

            ensureSampleBufferSize(bufferNew, format, newSize, len);
            CopySamples(bufferOld.ptr(), oldFormat, bufferNew.ptr(), format, len);

            // Note this fix for http://bugzilla.audacityteam.org/show_bug.cgi?id=451,
            // using Blockify, allows (len < mMinSamples).
            // This will happen consistently when going from more bytes per sample to fewer...
            // This will create a block that's smaller than mMinSamples, which
            // shouldn't be allowed, but we agreed it's okay for now.
            //vvv ANSWER-ME: Does this cause any bugs, or failures on write, elsewhere?
            //    If so, need to special-case (len < mMinSamples) and start combining data
            //    from the old blocks... Oh no!

            // Using Blockify will handle the cases where len > the NEW mMaxSamples. Previous code did not.
            const auto blockstart = oldSeqBlock.start;
            Blockify(*mDirManager, mMaxSamples, mSampleFormat,
                     newBlockArray, blockstart, bufferNew.ptr(), len);
        }
    }

    // Invalidate all the old, non-aliased block files.
    // Aliased files will be converted at save, per comment above.

    // Commit the changes to block file array
    CommitChangesIfConsistent
            (newBlockArray, mNumSamples, "Sequence::ConvertToSampleFormat()");

    // Commit the other changes
    bSuccess = true;

    return true;
}

void Sequence::AppendBlock
        (DirManager &mDirManager,
         BlockArray &mBlock, sampleCount &mNumSamples, const SeqBlock &b) {
    // Quick check to make sure that it doesn't overflow
    if (Overflows((mNumSamples.as_double()) + ((double) b.f->GetLength())))
        THROW_INCONSISTENCY_EXCEPTION;

    SeqBlock newBlock(
            mDirManager.CopyBlockFile(b.f), // Bump ref count if not locked, else copy
            mNumSamples
    );
    // We can assume newBlock.f is not null

    mBlock.push_back(newBlock);
    mNumSamples += newBlock.f->GetLength();

    // Don't do a consistency check here because this
    // function gets called in an inner loop.
}
