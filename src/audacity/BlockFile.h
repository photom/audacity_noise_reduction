/**********************************************************************

  Audacity: A Digital Audio Editor

  BlockFile.h

  Joshua Haberman
  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_BLOCKFILE__
#define __AUDACITY_BLOCKFILE__

#include <string>

#include "MemoryX.h"
#include "Types.h"
#include "wxFileNameWrapper.h"
#include "ODTaskThread.h"

class SummaryInfo {
public:
    explicit SummaryInfo(size_t samples);

    int fields; /* Usually 3 for Min, Max, RMS */
    sampleFormat format;
    int bytesPerFrame;
    size_t frames64K;
    int offset64K;
    size_t frames256;
    int offset256;
    size_t totalSummaryBytes;
};


class BlockFile;

class AliasBlockFile;

using BlockFilePtr = std::shared_ptr<BlockFile>;

template<typename Result, typename... Args>
inline std::shared_ptr<Result> make_blockfile(Args &&... args) {
    return std::make_shared<Result>(std::forward<Args>(args)...);
}

class BlockFile /* not final, abstract */ {
public:

    // Report disk space usage.
    using DiskByteCount = unsigned long long;
    // Constructor / Destructor

    /// Construct a BlockFile.
    BlockFile(wxFileNameWrapper &&fileName, size_t samples);

    virtual ~BlockFile();

    static unsigned long gBlockFileDestructionCount;

    // Reading

    size_t GetLength() const { return mLen; }

    /// Returns TRUE if this block references another disk file
    virtual bool IsAlias() const { return false; }

    virtual bool IsLocked();

    /// Read the summary section of the file.  Derived classes implement.
    virtual bool ReadSummary(ArrayOf<char> &data) = 0;

    /// Calculate summary data for the given sample data
    /// Overrides have differing details of memory management
    virtual void *CalcSummary(samplePtr buffer, size_t len,
                              sampleFormat format,
            // This gets filled, if the caller needs to deallocate.  Else it is null.
                              ArrayOf<char> &cleanup);

    // Common, nonvirtual calculation routine for the use of the above
    void CalcSummaryFromBuffer(const float *fbuffer, size_t len,
                               float *summary256, float *summary64K);

    /// Gets the filename of the disk file associated with this BlockFile
    /// (can be empty -- some BlockFiles, like SilentBlockFile, correspond to
    ///  no file on disk)
    /// Avoids copying wxFileName by returning a reference, but for some subclasses
    /// of BlockFile, you must exclude other threads from changing the name so long
    /// as you have only a reference.  Thus, this wrapper object that guarantees release
    /// of any lock when it goes out of scope.  Call mLocker.reset() to unlock it sooner.
    struct GetFileNameResult {
        const wxFileName &name;
        ODLocker mLocker;

        GetFileNameResult(const wxFileName &name_, ODLocker &&locker = ODLocker{})
                : name{name_}, mLocker{std::move(locker)} {}

        GetFileNameResult(const GetFileNameResult &) = delete;

        GetFileNameResult &operator=(const GetFileNameResult &) = delete;

        GetFileNameResult(GetFileNameResult &&that)
                : name{that.name}, mLocker{std::move(that.mLocker)} {}
    };

    virtual GetFileNameResult GetFileName() const;

    virtual void SetFileName(wxFileNameWrapper &&name);

    /// Retrieves audio data from this BlockFile
    /// Returns the number of samples really read, not more than len
    /// If fewer can be read than len, throws an exception if mayThrow is true,
    /// otherwise fills the remainder of data with zeroes.
    virtual size_t ReadData(samplePtr data, sampleFormat format,
                            size_t start, size_t len, bool mayThrow = true)
    const = 0;

    static size_t CommonReadData(
            bool mayThrow,
            const wxFileName &fileName, bool &mSilentLog,
            const AliasBlockFile *pAliasFile, sampleCount origin, unsigned channel,
            samplePtr data, sampleFormat format, size_t start, size_t len,
            const sampleFormat *pLegacyFormat = nullptr, size_t legacyLen = 0);

    /// Create a NEW BlockFile identical to this, using the given filename
    virtual BlockFilePtr Copy(wxFileNameWrapper &&newFileName) = 0;

    virtual DiskByteCount GetSpaceUsage() const = 0;

    /// if the on-disk state disappeared, either recover it (if it was
    //summary only), write out a placeholder of silence data (missing
    //.au) or mark the blockfile to deal some other way without spewing
    //errors.
    // May throw exceptions for i/o errors.
    virtual void Recover() = 0;

   /// Returns TRUE if this block's complete summary has been computed and is ready (for OD)
   virtual bool IsSummaryAvailable() const {return true;}

private:
    int mLockCount;

    static ArrayOf<char> fullSummary;

protected:
    wxFileNameWrapper mFileName;
    size_t mLen;
    SummaryInfo mSummaryInfo;
    float mMin, mMax, mRMS;
    mutable bool mSilentLog;
};

/// A BlockFile that refers to data in an existing file

/// An AliasBlockFile references an existing disk file for its storage
/// instead of copying the data.  It still writes a file to disk, but
/// only stores summary data in it.
///
/// This is a common base class for all alias block files.  It handles
/// reading and writing summary data, leaving very little for derived
/// classes to need to implement.
class AliasBlockFile /* not final */ : public BlockFile {
public:

    // Constructor / Destructor

    /// Constructs an AliasBlockFile
    AliasBlockFile(sampleCount aliasStart,
                   size_t aliasLen, int aliasChannel);

    AliasBlockFile(sampleCount aliasStart,
                   size_t aliasLen, int aliasChannel,
                   float min, float max, float RMS);

    virtual ~AliasBlockFile();

    /// as SilentLog (which would affect Summary data access), but
    // applying to Alias file access
    void SilenceAliasLog() const { mSilentAliasLog = true; }

    //
    bool IsAlias() const override { return true; }

protected:
    // Introduce a NEW virtual.
    /// Write the summary to disk, using the derived ReadData() to get the data
    virtual void WriteSummary();

    /// Read the summary into a buffer
    bool ReadSummary(ArrayOf<char> &data) override;

    sampleCount mAliasStart;
    const int mAliasChannel;
    mutable bool mSilentAliasLog;
};

#endif

