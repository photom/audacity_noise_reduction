/**********************************************************************

  Audacity: A Digital Audio Editor

  Export.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_EXPORT__
#define __AUDACITY_EXPORT__

#include <string>
#include <vector>
#include "Mix.h"
#include "ImportPlugin.h"

class FormatInfo {
public:
    FormatInfo() {}

    FormatInfo(const FormatInfo &) = default;

    FormatInfo &operator=(const FormatInfo &) = default;

    //FormatInfo( FormatInfo && ) = default;
    //FormatInfo &operator = ( FormatInfo && ) = default;
    ~FormatInfo() {}

    std::string mFormat;
    std::string mDescription;
    // std::string mExtension;
    std::vector<std::string> mExtensions;
    std::string mMask;
    unsigned mMaxChannels;
    bool mCanMetaData;
};

//----------------------------------------------------------------------------
// ExportPlugin
//----------------------------------------------------------------------------
class ExportPlugin {
public:

    ExportPlugin();

    virtual ~ExportPlugin();

    int AddFormat();

    void SetFormat(const std::string &format, int index);

    void AddExtension(const std::string &extension, int index);

    void SetCanMetaData(bool canmetadata, int index);

    void SetMaxChannels(unsigned maxchannels, unsigned index);

    void SetExtensions(const std::vector<std::string> &extensions, int index);

    /** \brief called to export audio into a file.
     *
     * @param pDialog To be initialized with pointer to a NEW ProgressDialog if
     * it was null, otherwise gives an existing dialog to be reused
    *  (working around a problem in wxWidgets for Mac; see bug 1600)
     * @param selectedOnly Set to true if all tracks should be mixed, to false
     * if only the selected tracks should be mixed and exported.
     * @param metadata A Tags object that will over-ride the one in *project and
     * be used to tag the file that is exported.
     * @param subformat Control which of the multiple formats this exporter is
     * capable of exporting should be used. Used where a single export plug-in
     * handles a number of related formats, but they have separate
     * entries in the Format drop-down list box. For example, the options to
     * export to "Other PCM", "AIFF 16 Bit" and "WAV 16 Bit" are all the same
     * libsndfile export plug-in, but with subformat set to 0, 1, and 2
     * respectively.
     * @return ProgressResult::Failed or ProgressResult::Cancelled if export
     * fails to complete for any reason, in which case this function is
     * responsible for alerting the user.  Otherwise ProgressResult::Success or
     * ProgressResult::Stopped
     */
    virtual ProgressResult Export(
            WaveTrackConstArray& tracks,
            const std::string &fName,
            MixerSpec *mixerSpec = nullptr,
            int subformat = 0) = 0;

protected:
    std::unique_ptr<Mixer> CreateMixer(const WaveTrackConstArray &inputTracks,
                                       double startTime, double stopTime,
                                       unsigned numOutChannels, size_t outBufferSize, bool outInterleaved,
                                       double outRate, sampleFormat outFormat,
                                       bool highQuality = true, MixerSpec *mixerSpec = nullptr);


private:
    std::vector<FormatInfo> mFormatInfos;
};

#endif
