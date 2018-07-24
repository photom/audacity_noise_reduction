/**********************************************************************

  Audacity: A Digital Audio Editor

  SimpleBlockFile.h

  Joshua Haberman

**********************************************************************/

#ifndef __AUDACITY_SIMPLE_BLOCKFILE__
#define __AUDACITY_SIMPLE_BLOCKFILE__

#include <iostream>
#include "BlockFile.h"
#include "DirManager.h"

struct SimpleBlockFileCache {
    bool active;
    bool needWrite;
    sampleFormat format;
    ArrayOf<char> sampleData, summaryData;

    SimpleBlockFileCache() {}
};

// The AU formats we care about
enum {
    AU_SAMPLE_FORMAT_16 = 3,
    AU_SAMPLE_FORMAT_24 = 4,
    AU_SAMPLE_FORMAT_FLOAT = 6,
};

typedef struct {
    uint32_t magic;      // magic number
    uint32_t dataOffset; // byte offset to start of audio data
    uint32_t dataSize;   // data length, in bytes (optional)
    uint32_t encoding;   // data encoding enumeration
    uint32_t sampleRate; // samples per second
    uint32_t channels;   // number of interleaved channels
} auHeader;

class SimpleBlockFile : public BlockFile {
public:
    /// Create a disk file and write summary and sample data to it
    SimpleBlockFile(wxFileNameWrapper &&baseFileName,
                    samplePtr sampleData, size_t sampleLen,
                    sampleFormat format,
                    bool allowDeferredWrite = false,
                    bool bypassCache = false);

    /// Create the memory structure to refer to the given block file
    SimpleBlockFile(wxFileNameWrapper &&existingFile, size_t len,
                    float min, float max, float rms);

    virtual ~SimpleBlockFile();

    static bool GetCache();

    SimpleBlockFileCache mCache;

    bool WriteSimpleBlockFile(samplePtr sampleData, size_t sampleLen,
                              sampleFormat format, void *summaryData);

    /// Read the data section of the disk file
    size_t ReadData(samplePtr data, sampleFormat format,
                    size_t start, size_t len, bool mayThrow) const override;

    /// Read the summary section of the disk file
    bool ReadSummary(ArrayOf<char> &data) override {
        std::cerr << "SimpleBlockFile::ReadSummary unimplemented." << std::endl;
        return true;
    }

   /// Create a NEW block file identical to this one
   BlockFilePtr Copy(wxFileNameWrapper &&newFileName) override {
       std::cerr << "SimpleBlockFile::Copy unimplemented." << std::endl;
       return nullptr;
   }

   DiskByteCount GetSpaceUsage() const override {
       std::cerr << "SimpleBlockFile::GetSpaceUsage unimplemented." << std::endl;
       return 0;
   }
   void Recover() override {
       std::cerr << "SimpleBlockFile::Recover unimplemented." << std::endl;
    }

private:
    mutable sampleFormat mFormat; // may be found lazily
};


#endif
