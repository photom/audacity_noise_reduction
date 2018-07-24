/**********************************************************************

  Audacity: A Digital Audio Editor

  BlockFile.cpp

  Joshua Haberman
  Dominic Mazzoni

*******************************************************************//*!

\class BlockFile
\brief A BlockFile is a chunk of immutable audio data.

A BlockFile represents a chunk of audio data.  These chunks are
assembled into sequences by the class Sequence.  These classes
are at the heart of how Audacity stores audio data.

BlockFile is an abstract base class that can be implemented in
many different ways.  However it does have a fairly large amount
of shared code that deals with the physical file and manipulating
the summary data.

BlockFile should be thought of as an immutable class.  After it
is constructed, it is essentially never changed (though there are
a few exceptions).  Most notably, the audio data and summary data
are never altered once it is constructed.  This is important to
some of the derived classes that are actually aliases to audio
data stored in existing files.

BlockFiles are managed by std::shared_ptr, so they are reference-counted.

*//****************************************************************//**

\class SummaryInfo
\brief Works with BlockFile to hold info about max and min and RMS
over multiple samples, which in turn allows rapid drawing when zoomed
out.

*//*******************************************************************/

#include <float.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <zconf.h>

#include "sndfile.h"

#include "Audacity.h"
#include "BlockFile.h"
#include "MemoryX.h"
#include "SampleFormat.h"
#include "FileException.h"
#include "FileFormats.h"
// msmeyer: Define this to add debug output via wxPrintf()
//#define DEBUG_BLOCKFILE

#ifdef DEBUG_BLOCKFILE
#define BLOCKFILE_DEBUG_OUTPUT(op, i) \
   wxPrintf(wxT("[BlockFile %x %s] %s: %i\n"), (unsigned)this, \
            mFileName.GetFullName(), wxT(op), i);
#else
#define BLOCKFILE_DEBUG_OUTPUT(op, i)
#endif

static const int headerTagLen = 20;
static char headerTag[headerTagLen + 1] = "AudacityBlockFile112";

SummaryInfo::SummaryInfo(size_t samples) {
    format = floatSample;

    fields = 3; /* min, max, rms */

    bytesPerFrame = sizeof(float) * fields;

    frames64K = (samples + 65535) / 65536;
    frames256 = frames64K * 256;

    offset64K = headerTagLen;
    offset256 = offset64K + (frames64K * bytesPerFrame);
    totalSummaryBytes = offset256 + (frames256 * bytesPerFrame);
}

ArrayOf<char> BlockFile::fullSummary;

/// Initializes the base BlockFile data.  The block is initially
/// unlocked and its reference count is 1.
///
/// @param fileName The name of the disk file associated with this
///                 BlockFile.  Not all BlockFiles will store their
///                 sample data here (for example, AliasBlockFiles
///                 read their data from elsewhere), but all BlockFiles
///                 will store at least the summary data here.
///
/// @param samples  The number of samples this BlockFile contains.
BlockFile::BlockFile(wxFileNameWrapper &&fileName, size_t samples) :
        mLockCount(0),
        mFileName(std::move(fileName)),
        mLen(samples),
        mSummaryInfo(SummaryInfo(samples)) {
    mSilentLog = false;
}

BlockFile::~BlockFile()
{
   if (!IsLocked() && mFileName.HasName())
      // PRL: what should be done if this fails?
      unlink(mFileName.GetFullPath().c_str());

   ++gBlockFileDestructionCount;
}

// static
unsigned long BlockFile::gBlockFileDestructionCount{0};

/// Returns true if the block is locked.
bool BlockFile::IsLocked() {
    return mLockCount > 0;
}

/// Get a buffer containing a summary block describing this sample
/// data.  This must be called by derived classes when they
/// are constructed, to allow them to construct their summary data,
/// after which they should write that data to their disk file.
///
/// This method also has the side effect of setting the mMin, mMax,
/// and mRMS members of this class.
///
/// You must not DELETE the returned buffer; it is static to this
/// method.
///
/// @param buffer A buffer containing the sample data to be analyzed
/// @param len    The length of the sample data
/// @param format The format of the sample data.
void *BlockFile::CalcSummary(samplePtr buffer, size_t len,
                             sampleFormat format, ArrayOf<char> &cleanup) {
    // Caller has nothing to de7allocate
    cleanup.reset();

    fullSummary.reinit(mSummaryInfo.totalSummaryBytes);

    memcpy(fullSummary.get(), headerTag, headerTagLen);

    float *summary64K = (float *) (fullSummary.get() + mSummaryInfo.offset64K);
    float *summary256 = (float *) (fullSummary.get() + mSummaryInfo.offset256);

    float *fbuffer = new float[len];
    memset(fbuffer, 0, len);
    CopySamples(buffer, format,
                (samplePtr) &fbuffer[0], floatSample, len);

    CalcSummaryFromBuffer(&fbuffer[0], len, summary256, summary64K);

    return fullSummary.get();
}


void BlockFile::CalcSummaryFromBuffer(const float *fbuffer, size_t len,
                                      float *summary256, float *summary64K) {
    decltype(len) sumLen;

    float min, max;
    float sumsq;
    double totalSquares = 0.0;
    double fraction{0.0};

    // Recalc 256 summaries
    sumLen = (len + 255) / 256;
    int summaries = 256;

    for (decltype(sumLen) i = 0; i < sumLen; i++) {
        min = fbuffer[i * 256];
        max = fbuffer[i * 256];
        sumsq = ((float) min) * ((float) min);
        decltype(len) jcount = 256;
        if (jcount > len - i * 256) {
            jcount = len - i * 256;
            fraction = 1.0 - (jcount / 256.0);
        }
        for (decltype(jcount) j = 1; j < jcount; j++) {
            float f1 = fbuffer[i * 256 + j];
            sumsq += ((float) f1) * ((float) f1);
            if (f1 < min)
                min = f1;
            else if (f1 > max)
                max = f1;
        }

        totalSquares += sumsq;
        float rms = (float) sqrt(sumsq / jcount);

        summary256[i * 3] = min;
        summary256[i * 3 + 1] = max;
        summary256[i * 3 + 2] = rms;  // The rms is correct, but this may be for less than 256 samples in last loop.
    }
    for (auto i = sumLen; i < mSummaryInfo.frames256; i++) {
        // filling in the remaining bits with non-harming/contributing values
        // rms values are not "non-harming", so keep  count of them:
        summaries--;
        summary256[i * 3] = FLT_MAX;  // min
        summary256[i * 3 + 1] = -FLT_MAX;   // max
        summary256[i * 3 + 2] = 0.0f; // rms
    }

    // Calculate now while we can do it accurately
    mRMS = sqrt(totalSquares / len);

    // Recalc 64K summaries
    sumLen = (len + 65535) / 65536;

    for (decltype(sumLen) i = 0; i < sumLen; i++) {
        min = summary256[3 * i * 256];
        max = summary256[3 * i * 256 + 1];
        sumsq = (float) summary256[3 * i * 256 + 2];
        sumsq *= sumsq;
        for (decltype(len) j = 1; j <
                                  256; j++) {   // we can overflow the useful summary256 values here, but have put non-harmful values in them
            if (summary256[3 * (i * 256 + j)] < min)
                min = summary256[3 * (i * 256 + j)];
            if (summary256[3 * (i * 256 + j) + 1] > max)
                max = summary256[3 * (i * 256 + j) + 1];
            float r1 = summary256[3 * (i * 256 + j) + 2];
            sumsq += r1 * r1;
        }

        double denom = (i < sumLen - 1) ? 256.0 : summaries - fraction;
        float rms = (float) sqrt(sumsq / denom);

        summary64K[i * 3] = min;
        summary64K[i * 3 + 1] = max;
        summary64K[i * 3 + 2] = rms;
    }
    for (auto i = sumLen; i < mSummaryInfo.frames64K; i++) {
        std::cerr << "Out of data for mSummaryInfo" << std::endl;   // Do we ever get here?
        summary64K[i * 3] = 0.0f;  // probably should be FLT_MAX, need a test case
        summary64K[i * 3 + 1] = 0.0f; // probably should be -FLT_MAX, need a test case
        summary64K[i * 3 + 2] = 0.0f; // just padding
    }

    // Recalc block-level summary (mRMS already calculated)
    min = summary64K[0];
    max = summary64K[1];

    for (decltype(sumLen) i = 1; i < sumLen; i++) {
        if (summary64K[3 * i] < min)
            min = summary64K[3 * i];
        if (summary64K[3 * i + 1] > max)
            max = summary64K[3 * i + 1];
    }

    mMin = min;
    mMax = max;
}

/// Returns the file name of the disk file associated with this
/// BlockFile.  Not all BlockFiles store their sample data here,
/// but most BlockFiles have at least their summary data here.
/// (some, i.e. SilentBlockFiles, do not correspond to a file on
///  disk and have empty file names)
auto BlockFile::GetFileName() const -> GetFileNameResult {
    return {mFileName};
}

///sets the file name the summary info will be saved in.  threadsafe.
void BlockFile::SetFileName(wxFileNameWrapper &&name) {
    mFileName = std::move(name);
}


inline bool exists_test(const std::string &name) {
    if (FILE *file = fopen(name.c_str(), "r")) {
        fclose(file);
        return true;
    } else {
        return false;
    }
}

size_t BlockFile::CommonReadData(
        bool mayThrow,
        const wxFileName &fileName, bool &mSilentLog,
        const AliasBlockFile *pAliasFile, sampleCount origin, unsigned channel,
        samplePtr data, sampleFormat format, size_t start, size_t len,
        const sampleFormat *pLegacyFormat, size_t legacyLen) {
    // Third party library has its own type alias, check it before
    // adding origin + size_t
    static_assert(sizeof(sampleCount::type) <= sizeof(sf_count_t),
                  "Type sf_count_t is too narrow to hold a sampleCount");

    SF_INFO info;
    memset(&info, 0, sizeof(info));

    if (pLegacyFormat) {
        switch (*pLegacyFormat) {
            case int16Sample:
                info.format =
                        SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_CPU;
                break;
            default:
            case floatSample:
                info.format =
                        SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_CPU;
                break;
            case int24Sample:
                info.format = SF_FORMAT_RAW | SF_FORMAT_PCM_32 | SF_ENDIAN_CPU;
                break;
        }
        info.samplerate = 44100; // Doesn't matter
        info.channels = 1;
        info.frames = legacyLen + origin.as_long_long();
    }

    SFFile sf;

    {

        const auto fullPath = fileName.GetFullPath();
        if (FILE *file = fopen(fullPath.c_str(), "r")) {
            int fd = fileno(file);
            // Even though there is an sf_open() that takes a filename, use the one that
            // takes a file descriptor since wxWidgets can open a file with a Unicode name and
            // libsndfile can't (under Windows).
            sf.reset(SFCall<SNDFILE *>(sf_open_fd, fd, SFM_READ, &info, false));
        }

        if (!sf) {

            memset(data, 0, SAMPLE_SIZE(format) * len);

        }
    }
    mSilentLog = !sf;

    size_t framesRead = 0;
    if (sf) {
        auto seek_result = SFCall<sf_count_t>(
                sf_seek, sf.get(), (origin + start).as_long_long(), SEEK_SET);

        if (seek_result < 0)
            // error
            ;
        else {
            auto channels = info.channels;
            assert(channels >= 1);
            assert((int) channel < channels);

            if (channels == 1 &&
                format == int16Sample &&
                sf_subtype_is_integer(info.format)) {
                // If both the src and dest formats are integer formats,
                // read integers directly from the file, comversions not needed
                framesRead = SFCall<sf_count_t>(
                        sf_readf_short, sf.get(), (short *) data, len);
            } else if (channels == 1 &&
                       format == int24Sample &&
                       sf_subtype_is_integer(info.format)) {
                framesRead = SFCall<sf_count_t>(
                        sf_readf_int, sf.get(), (int *) data, len);

                // libsndfile gave us the 3 byte sample in the 3 most
                // significant bytes -- we want it in the 3 least
                // significant bytes.
                int *intPtr = (int *) data;
                for (size_t i = 0; i < framesRead; i++)
                    intPtr[i] = intPtr[i] >> 8;
            } else if (format == int16Sample &&
                       !sf_subtype_more_than_16_bits(info.format)) {
                // Special case: if the file is in 16-bit (or less) format,
                // and the calling method wants 16-bit data, go ahead and
                // read 16-bit data directly.  This is a pretty common
                // case, as most audio files are 16-bit.
                SampleBuffer buffer(len * channels, int16Sample);
                framesRead = SFCall<sf_count_t>(
                        sf_readf_short, sf.get(), (short *) buffer.ptr(), len);
                for (size_t i = 0; i < framesRead; i++)
                    ((short *) data)[i] =
                            ((short *) buffer.ptr())[(channels * i) + channel];
            } else {
                // Otherwise, let libsndfile handle the conversion and
                // scaling, and pass us normalized data as floats.  We can
                // then convert to whatever format we want.
                SampleBuffer buffer(len * channels, floatSample);
                framesRead = SFCall<sf_count_t>(
                        sf_readf_float, sf.get(), (float *) buffer.ptr(), len);
                auto bufferPtr = (samplePtr) ((float *) buffer.ptr() + channel);
                CopySamples(bufferPtr, floatSample,
                            (samplePtr) data, format,
                            framesRead,
                            true /* high quality by default */,
                            channels /* source stride */);
            }
        }
    }

    if (framesRead < len) {
        if (mayThrow)
            throw FileException{FileException::Cause::Read, fileName};
        ClearSamples(data, format, framesRead, len - framesRead);
    }

    return framesRead;
}
