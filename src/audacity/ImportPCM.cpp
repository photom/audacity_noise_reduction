/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportPCM.cpp

  Dominic Mazzoni
  Leland Lucius

*//****************************************************************//**

\class PCMImportFileHandle
\brief An ImportFileHandle for PCM data

*//****************************************************************//**

\class PCMImportPlugin
\brief An ImportPlugin for PCM data

*//*******************************************************************/

#include <cstring>
#include "ImportPCM.h"

#include "sndfile.h"
#include "FileFormats.h"
#include "ImportPlugin.h"


// static
std::unique_ptr<ImportFileHandle> PCMImportFileHandle::Open(const std::string &filename) {
    SF_INFO info;
    SFFile file;

    memset(&info, 0, sizeof(info));

    if (FILE *f = fopen(filename.c_str(), "r")) {
        int fd = fileno(f);
        // Even though there is an sf_open() that takes a filename, use the one that
        // takes a file descriptor since wxWidgets can open a file with a Unicode name and
        // libsndfile can't (under Windows).
        file.reset(SFCall<SNDFILE *>(sf_open_fd, fd, SFM_READ, &info, true));
    }


    if (!file) {
        // TODO: Handle error
        //char str[1000];
        //sf_error_str((SNDFILE *)NULL, str, 1000);

        return nullptr;
    } else if (file &&
               (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG) {
        // mchinen 15.1.2012 - disallowing libsndfile to handle
        // ogg files because seeking is broken at this date (very slow,
        // seeks from beginning of file each seek).
        // This was said by Erik (libsndfile maintainer).
        // Note that this won't apply to our local libsndfile, so only
        // linux builds that use --with-libsndfile=system are affected,
        // as our local libsndfile doesn't do OGG.
        // In particular ubuntu 10.10 and 11.04 are known to be affected
        // When the bug is fixed, we can check version to avoid only
        // the broken builds.

        return nullptr;
    }

    // Success, so now transfer the duty to close the file from "file".
    return std::make_unique<PCMImportFileHandle>(filename, std::move(file), info);
}


PCMImportFileHandle::PCMImportFileHandle(std::string name,
                                         SFFile &&file, SF_INFO info)
        : ImportFileHandle(name),
          mFile(std::move(file)),
          mInfo(info) {
    assert(info.channels >= 0);

    //
    // Figure out the format to use.
    //
    // In general, go with floatSample.  However, if
    // the file is higher-quality, go with a format which preserves
    // the quality of the original file.
    //

    mFormat = floatSample;

    if (mFormat != floatSample &&
        sf_subtype_more_than_16_bits(mInfo.format))
        mFormat = floatSample;
}

std::string PCMImportFileHandle::GetFileDescription() {
    return SFCall<std::string>(sf_header_name, mInfo.format);
}

auto PCMImportFileHandle::GetFileUncompressedBytes() -> ByteCount {
    return mInfo.frames * mInfo.channels * SAMPLE_SIZE(mFormat);
}

ProgressResult PCMImportFileHandle::Import(TrackFactory *trackFactory,
                                           TrackHolders &outTracks) {
    outTracks.clear();

    assert(mFile.get());

    // Fall back to "copy" if it doesn't match anything else, since it is safer
    bool doEdit = false;

    TrackHolders channels(mInfo.channels);

    auto iter = channels.begin();
    for (int c = 0; c < mInfo.channels; ++iter, ++c) {
        *iter = trackFactory->NewWaveTrack(mFormat, mInfo.samplerate);

        if (mInfo.channels > 1)
            switch (c) {
                case 0:
                    iter->get()->SetChannel(WaveTrack::LeftChannel);
                    break;
                case 1:
                    iter->get()->SetChannel(WaveTrack::RightChannel);
                    break;
                default:
                    iter->get()->SetChannel(WaveTrack::MonoChannel);
            }
    }

    auto fileTotalFrames =
            (sampleCount) mInfo.frames; // convert from sf_count_t
    auto maxBlockSize = channels.begin()->get()->GetMaxBlockSize();

    // Otherwise, we're in the "copy" mode, where we read in the actual
    // samples from the file and store our own local copy of the
    // samples in the tracks.

    // PRL:  guard against excessive memory buffer allocation in case of many channels
    using type = decltype(maxBlockSize);
    if (mInfo.channels < 1)
        return ProgressResult::Failed;
    auto maxBlock = std::min(maxBlockSize,
                             std::numeric_limits<type>::max() /
                             (mInfo.channels * SAMPLE_SIZE(mFormat))
    );
    if (maxBlock < 1)
        return ProgressResult::Failed;

    SampleBuffer srcbuffer, buffer;
    assert(mInfo.channels >= 0);
    while (nullptr == srcbuffer.Allocate(maxBlock * mInfo.channels, mFormat).ptr() ||
           nullptr == buffer.Allocate(maxBlock, mFormat).ptr()) {
        maxBlock /= 2;
        if (maxBlock < 1)
            return ProgressResult::Failed;
    }

    decltype(fileTotalFrames) framescompleted = 0;

    long block;
    do {
        block = maxBlock;

        if (mFormat == int16Sample)
            block = SFCall<sf_count_t>(sf_readf_short, mFile.get(), (short *) srcbuffer.ptr(), block);
            //import 24 bit int as float and have the append function convert it.  This is how PCMAliasBlockFile works too.
        else
            block = SFCall<sf_count_t>(sf_readf_float, mFile.get(), (float *) srcbuffer.ptr(), block);

        if (block < 0 || block > (long) maxBlock) {
            assert(false);
            block = maxBlock;
        }

        if (block) {
            auto iter = channels.begin();
            for (int c = 0; c < mInfo.channels; ++iter, ++c) {
                if (mFormat == int16Sample) {
                    for (int j = 0; j < block; j++)
                        ((short *) buffer.ptr())[j] =
                                ((short *) srcbuffer.ptr())[mInfo.channels * j + c];
                } else {
                    for (int j = 0; j < block; j++)
                        ((float *) buffer.ptr())[j] =
                                ((float *) srcbuffer.ptr())[mInfo.channels * j + c];
                }

                iter->get()->Append(buffer.ptr(), (mFormat == int16Sample) ? int16Sample : floatSample, block);
            }
            framescompleted += block;
        }

    } while (block > 0);

    for (const auto &channel : channels) {
        channel->Flush();
    }
    outTracks.swap(channels);

    return ProgressResult::Success;
}

PCMImportFileHandle::~PCMImportFileHandle() {
}
