/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportPCM.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_IMPORT_PCM__
#define __AUDACITY_IMPORT_PCM__

#include "sndfile.h"
#include "FileFormats.h"
#include "ImportPlugin.h"


class PCMImportFileHandle final : public ImportFileHandle {
public:
    static std::unique_ptr<ImportFileHandle> Open(const std::string &filename);

    PCMImportFileHandle(std::string name, SFFile &&file, SF_INFO info);

    ~PCMImportFileHandle();

    std::string GetFileDescription() override;

    ByteCount GetFileUncompressedBytes() override;

    ProgressResult Import(TrackFactory *trackFactory, TrackHolders &outTracks) override;

    int32_t GetStreamCount() override { return 1; }

    const std::vector<std::string> &GetStreamInfo() override {
        static std::vector<std::string> empty;
        return empty;
    }

    void SetStreamUsage(int32_t StreamID, bool Use) override {}

private:
    SFFile mFile;
    const SF_INFO mInfo;
    sampleFormat mFormat;
};


#endif
