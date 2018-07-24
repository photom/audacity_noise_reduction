/**********************************************************************

  Audacity: A Digital Audio Editor

  SimpleBlockFile.cpp

  Joshua Haberman
  Markus Meyer

*******************************************************************//**

\file SimpleBlockFile.cpp
\brief Implements SimpleBlockFile and auHeader.

*//****************************************************************//**

\class SimpleBlockFile
\brief A BlockFile that reads and writes uncompressed data using
libsndfile

A block file that writes the audio data to an .au file and reads
it back using libsndfile.

There are two ways to construct a simple block file.  One is to
supply data and have the constructor write the file.  The other
is for when the file already exists and we simply want to create
the data structure to refer to it.

The block file can be cached in two ways. Caching is enabled if the
preference "/Directories/CacheBlockFiles" is set, otherwise disabled. The
default is to disable caching.

* Read-caching: If caching is enabled, all block files will always be
  read-cached. Block files on disk will be read as soon as they are created
  and held in memory. New block files will be written to disk, but held in
  memory, so they are never read from disk in the current session.

* Write-caching: If caching is enabled and the parameter allowDeferredWrite
  is enabled at the block file constructor, NEW block files are held in memory
  and written to disk only when WriteCacheToDisk() is called. This is used
  during recording to prevent disk access. After recording, WriteCacheToDisk()
  will be called on all block files and they will be written to disk. During
  normal editing, no write cache is active, that is, any block files will be
  written to disk instantly.

  Even with write cache, auto recovery during normal editing will work as
  expected. However, auto recovery during recording will not work (not even
  manual auto recovery, because the files are never written physically to
  disk).

*//****************************************************************//**

\class auHeader
\brief The auHeader is a structure used by SimpleBlockFile for .au file
format.  There probably is an 'official' header file we should include
to get its definition, rather than rolling our own.

*//*******************************************************************/


#include <cstring>
#include <fstream>
#include "SimpleBlockFile.h"
#include "FileException.h"
#include "SampleFormat.h"

/// Constructs a SimpleBlockFile based on sample data and writes
/// it to disk.
///
/// @param baseFileName The filename to use, but without an extension.
///                     This constructor will add the appropriate
///                     extension (.au in this case).
/// @param sampleData   The sample data to be written to this block.
/// @param sampleLen    The number of samples to be written to this block.
/// @param format       The format of the given samples.
/// @param allowDeferredWrite    Allow deferred write-caching
SimpleBlockFile::SimpleBlockFile(wxFileNameWrapper &&baseFileName,
                                 samplePtr sampleData, size_t sampleLen,
                                 sampleFormat format,
                                 bool allowDeferredWrite /* = false */,
                                 bool bypassCache /* = false */) :
        BlockFile{
                (baseFileName.SetExt("au"), std::move(baseFileName)),
                sampleLen
        } {
    mFormat = format;

    mCache.active = false;

    bool useCache = GetCache() && (!bypassCache);

    if (!(allowDeferredWrite && useCache) && !bypassCache) {
        bool bSuccess = WriteSimpleBlockFile(sampleData, sampleLen, format, NULL);
        if (!bSuccess)
            throw FileException{
                    FileException::Cause::Write, GetFileName().name};
    }

    if (useCache) {
        //wxLogDebug("SimpleBlockFile::SimpleBlockFile(): Caching block file data.");
        mCache.active = true;
        mCache.needWrite = true;
        mCache.format = format;
        const auto sampleDataSize = sampleLen * SAMPLE_SIZE(format);
        mCache.sampleData.reinit(sampleDataSize);
        memcpy(mCache.sampleData.get(), sampleData, sampleDataSize);
        ArrayOf<char> cleanup;
        void *summaryData = BlockFile::CalcSummary(sampleData, sampleLen,
                                                   format, cleanup);
        mCache.summaryData.reinit(mSummaryInfo.totalSummaryBytes);
        memcpy(mCache.summaryData.get(), summaryData,
               mSummaryInfo.totalSummaryBytes);
    }
}

/// Construct a SimpleBlockFile memory structure that will point to an
/// existing block file.  This file must exist and be a valid block file.
///
/// @param existingFile The disk file this SimpleBlockFile should use.
SimpleBlockFile::SimpleBlockFile(wxFileNameWrapper &&existingFile, size_t len,
                                 float min, float max, float rms) :
        BlockFile{std::move(existingFile), len} {
    // Set an invalid format to force GetSpaceUsage() to read it from the file.
    mFormat = (sampleFormat) 0;

    mMin = min;
    mMax = max;
    mRMS = rms;

    mCache.active = false;
}

SimpleBlockFile::~SimpleBlockFile() {
}

bool SimpleBlockFile::WriteSimpleBlockFile(
        samplePtr sampleData,
        size_t sampleLen,
        sampleFormat format,
        void *summaryData) {
    std::ofstream file;
    file.open(mFileName.GetFullPath(), std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        // Can't do anything else.
        return false;
    }

    auHeader header;

    // AU files can be either big or little endian.  Which it is is
    // determined implicitly by the byte-order of the magic 0x2e736e64
    // (.snd).  We want it to be native-endian, so we write the magic
    // to memory and then let it write that to a file in native
    // endianness
    header.magic = 0x2e736e64;

    // We store the summary data at the end of the header, so the data
    // offset is the length of the summary data plus the length of the header
    header.dataOffset = sizeof(auHeader) + mSummaryInfo.totalSummaryBytes;

    // dataSize is optional, and we opt out
    header.dataSize = 0xffffffff;

    switch (format) {
        case int16Sample:
            header.encoding = AU_SAMPLE_FORMAT_16;
            break;

        case int24Sample:
            header.encoding = AU_SAMPLE_FORMAT_24;
            break;

        case floatSample:
            header.encoding = AU_SAMPLE_FORMAT_FLOAT;
            break;
    }

    // TODO: don't fabricate
    header.sampleRate = 44100;

    // BlockFiles are always mono
    header.channels = 1;

    // Write the file
    ArrayOf<char> cleanup;
    if (!summaryData)
        summaryData = /*BlockFile::*/CalcSummary(sampleData, sampleLen, format, cleanup);
    //mchinen:allowing virtual override of calc summary for ODDecodeBlockFile.
    // PRL: cleanup fixes a possible memory leak!

    size_t nBytesToWrite = sizeof(header);
    file.write ((char *)&header, (int32_t)nBytesToWrite);

    nBytesToWrite = mSummaryInfo.totalSummaryBytes;
    file.write((char *)summaryData, nBytesToWrite);

    if (format == int24Sample) {
        // we can't write the buffer directly to disk, because 24-bit samples
        // on disk need to be packed, not padded to 32 bits like they are in
        // memory
        int *int24sampleData = (int *) sampleData;

        for (size_t i = 0; i < sampleLen; i++) {
            nBytesToWrite = 3;
#if wxBYTE_ORDER == wxBIG_ENDIAN
                    file.write((char *) &int24sampleData[i] + 1, nBytesToWrite);
#else
            file.Write((char*)&int24sampleData[i], nBytesToWrite);
#endif

        }
    } else {
        // for all other sample formats we can write straight from the buffer
        // to disk
        nBytesToWrite = sampleLen * SAMPLE_SIZE(format);
        file.write(sampleData, nBytesToWrite);

    }
    file.close();

    return true;
}

bool SimpleBlockFile::GetCache() {
#ifdef DEPRECATED_AUDIO_CACHE
    // See http://bugzilla.audacityteam.org/show_bug.cgi?id=545.
    bool cacheBlockFiles = false;
    gPrefs->Read(wxT("/Directories/CacheBlockFiles"), &cacheBlockFiles);
    if (!cacheBlockFiles)
       return false;

    int lowMem = gPrefs->Read(wxT("/Directories/CacheLowMem"), 16l);
    if (lowMem < 16) {
       lowMem = 16;
    }
    lowMem <<= 20;
    return (GetFreeMemory() > lowMem);
#else
    return false;
#endif
}

/// Read the data portion of the block file using libsndfile.  Convert it
/// to the given format if it is not already.
///
/// @param data   The buffer where the data will be stored
/// @param format The format the data will be stored in
/// @param start  The offset in this block file
/// @param len    The number of samples to read
size_t SimpleBlockFile::ReadData(samplePtr data, sampleFormat format,
                        size_t start, size_t len, bool mayThrow) const
{
   if (mCache.active)
   {
      //wxLogDebug("SimpleBlockFile::ReadData(): Data are already in cache.");

      auto framesRead = std::min(len, std::max(start, mLen) - start);
      CopySamples(
         (samplePtr)(mCache.sampleData.get() +
            start * SAMPLE_SIZE(mCache.format)),
         mCache.format, data, format, framesRead);

      if ( framesRead < len ) {
         if (mayThrow)
            // Not the best exception class?
            throw FileException{ FileException::Cause::Read, mFileName };
         ClearSamples(data, format, framesRead, len - framesRead);
      }

      return framesRead;
   }
   else
      return CommonReadData( mayThrow,
         mFileName, mSilentLog, nullptr, 0, 0, data, format, start, len);
}
