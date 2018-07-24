/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportPCM.cpp

  Dominic Mazzoni

**********************************************************************/

#include <iostream>
#include "ExportPCM.h"

#include "sndfile.h"
#include "Utils.h"
#include "FileFormats.h"
#include "Mix.h"

struct {
    int format;
    const char *name;
    const char *desc;
}
static const kFormats[] =
        {
#if defined(__WXMAC__)
                { SF_FORMAT_AIFF | SF_FORMAT_PCM_16,   wxT("AIFF"),   XO("AIFF (Apple) signed 16-bit PCM")    },
#endif
                {SF_FORMAT_WAV | SF_FORMAT_PCM_16, "WAV", "WAV (Microsoft) signed 16-bit PCM"},
                {SF_FORMAT_WAV | SF_FORMAT_PCM_24, "WAV24", "WAV (Microsoft) signed 24-bit PCM"},
                {SF_FORMAT_WAV | SF_FORMAT_FLOAT, "WAVFLT", "WAV (Microsoft) 32-bit float PCM"},
// { SF_FORMAT_WAV | SF_FORMAT_GSM610,    wxT("GSM610"), XO("GSM 6.10 WAV (mobile)")             },
        };

ExportPCM::ExportPCM() {

    SF_INFO si;

    si.samplerate = 0;
    si.channels = 0;

    int format; // the index of the format we are setting up at the moment

    // Add the "special" formats first
    for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); i++) {
        format = AddFormat() - 1;

        si.format = kFormats[i].format;
        for (si.channels = 1; sf_format_check(&si); si.channels++);
        std::string ext = sf_header_extension(si.format);

        SetFormat(kFormats[i].name, format);
        SetCanMetaData(true, format);
        AddExtension(ext, format);
        SetMaxChannels(si.channels - 1, format);
    }

    // Then add the generic libsndfile formats
    format = AddFormat() - 1;  // store the index = 1 less than the count
    SetFormat("LIBSNDFILE", format);
    SetCanMetaData(true, format);
    std::vector<std::string> allext = sf_get_all_extensions();
    std::string wavext = sf_header_extension(SF_FORMAT_WAV);   // get WAV ext.
#if defined(wxMSW)
    // On Windows make sure WAV is at the beginning of the list of all possible
    // extensions for this format
    allext.Remove(wavext);
    allext.Insert(wavext, 0);
#endif
    SetExtensions(allext, format);
    SetMaxChannels(255, format);
}

/**
 *
 * @param subformat Control whether we are doing a "preset" export to a popular
 * file type, or giving the user full control over libsndfile.
 */
ProgressResult ExportPCM::Export(
        WaveTrackConstArray& waveTracks,
        const std::string &fName,
        MixerSpec *mixerSpec,
        int subformat) {
    assert(waveTracks.size() == 1);
    double rate = waveTracks.at(0)->GetRate();
    double t0 = waveTracks.at(0)->GetStartTime();
    double t1 = waveTracks.at(0)->GetEndTime();
    unsigned numChannels = waveTracks.at(0)->GetChannel() == WaveTrack::MonoChannel ? 1 : 2;

    int sf_format;
    if (subformat < 0 || static_cast<unsigned int>(subformat) >= (sizeof(kFormats) / sizeof(kFormats[0]))) {
        sf_format = SF_FORMAT_WAV;
    } else {
        sf_format = kFormats[subformat].format;
    }

    auto updateResult = ProgressResult::Success;
    {
        SFFile sf; // wraps f

        std::string formatStr;
        SF_INFO info;
        //int          err;

        //This whole operation should not occur while a file is being loaded on OD,
        //(we are worried about reading from a file being written to,) so we block.
        //Furthermore, we need to do this because libsndfile is not threadsafe.
        formatStr = SFCall<std::string>(sf_header_name, sf_format & SF_FORMAT_TYPEMASK);

        // Use libsndfile to export file

        info.samplerate = (unsigned int) (rate + 0.5);
        info.frames = (unsigned int) ((t1 - t0) * rate + 0.5);
        info.channels = numChannels;
        info.format = sf_format;
        info.sections = 1;
        info.seekable = 0;

        // If we can't export exactly the format they requested,
        // try the default format for that header type...
        if (!sf_format_check(&info))
            info.format = (info.format & SF_FORMAT_TYPEMASK);
        if (!sf_format_check(&info)) {
            std::cerr << "Cannot export audio in this format." << std::endl;
            return ProgressResult::Cancelled;
        }

        if (FILE* f = fopen(fName.c_str(), "wb")) {
            int fd = fileno(f);
            // Even though there is an sf_open() that takes a filename, use the one that
            // takes a file descriptor since wxWidgets can open a file with a Unicode name and
            // libsndfile can't (under Windows).
            sf.reset(SFCall<SNDFILE *>(sf_open_fd, fd, SFM_WRITE, &info, false));
            //add clipping for integer formats.  We allow floats to clip.
            sf_command(sf.get(), SFC_SET_CLIPPING, nullptr, sf_subtype_is_integer(sf_format) ? SF_TRUE : SF_FALSE);
        }

        if (!sf) {
            std::cerr << string_format("Cannot export audio to %s", fName) << std::endl;
            return ProgressResult::Cancelled;
        }

        sampleFormat format;
        if (sf_subtype_more_than_16_bits(info.format))
            format = floatSample;
        else
            format = int16Sample;

        size_t maxBlockLen = 44100 * 5;

        {
            assert(info.channels >= 0);
            auto mixer = CreateMixer(waveTracks,
                                     t0, t1,
                                     info.channels, maxBlockLen, true,
                                     rate, format, true, mixerSpec);


            while (updateResult == ProgressResult::Success) {
                sf_count_t samplesWritten;
                size_t numSamples = mixer->Process(maxBlockLen);

                if (numSamples == 0)
                    break;

                samplePtr mixed = mixer->GetBuffer();

                if (format == int16Sample)
                    samplesWritten = SFCall<sf_count_t>(sf_writef_short, sf.get(), (short *) mixed, numSamples);
                else
                    samplesWritten = SFCall<sf_count_t>(sf_writef_float, sf.get(), (float *) mixed, numSamples);

                if (static_cast<size_t>(samplesWritten) != numSamples) {
                    char buffer2[1000];
                    sf_error_str(sf.get(), buffer2, 1000);
                    std::cerr << string_format(
                            /* i18n-hint: %s will be the error message from libsndfile, which
                             * is usually something unhelpful (and untranslated) like "system
                             * error" */
                            "Error while writing %s file (disk full?).\nLibsndfile says \"%s\"",
                            formatStr,
                            buffer2);
                    updateResult = ProgressResult::Cancelled;
                    break;
                }

            }
        }

        // Install the WAV metata in a "LIST" chunk at the end of the file
        if (updateResult == ProgressResult::Success ||
            updateResult == ProgressResult::Stopped) {

            if (0 != sf.close()) {
                // TODO: more precise message
                std::cerr << "Unable to export" << std::endl;
                return ProgressResult::Cancelled;
            }
        }
    }

    return updateResult;
}
