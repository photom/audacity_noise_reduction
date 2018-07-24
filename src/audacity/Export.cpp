/**********************************************************************

  Audacity: A Digital Audio Editor

  Export.cpp

  Dominic Mazzoni

*******************************************************************//**

\class Export
\brief Main class to control the export function.

*//****************************************************************//**

\class ExportType
\brief Container for information about supported export types.

*//****************************************************************//**

\class ExportMixerDialog
\brief Dialog for advanced mixing.

*//****************************************************************//**

\class ExportMixerPanel
\brief Panel that displays mixing for advanced mixing option.

*//********************************************************************/


#include "Export.h"


ExportPlugin::ExportPlugin() {
}

ExportPlugin::~ExportPlugin() {
}

/** \brief Add a NEW entry to the list of formats this plug-in can export
 *
 * To configure the format use SetFormat, SetCanMetaData etc with the index of
 * the format.
 * @return The number of formats currently set up. This is one more than the
 * index of the newly added format.
 */
int ExportPlugin::AddFormat() {
    FormatInfo nf;
    mFormatInfos.push_back(nf);
    return mFormatInfos.size();
}

/**
 * @param index The plugin to set the format for (range 0 to one less than the
 * count of formats)
 */
void ExportPlugin::SetFormat(const std::string &format, int index) {
    mFormatInfos[index].mFormat = format;
}

void ExportPlugin::SetCanMetaData(bool canmetadata, int index) {
    mFormatInfos[index].mCanMetaData = canmetadata;
}

void ExportPlugin::AddExtension(const std::string &extension, int index) {
    mFormatInfos[index].mExtensions.emplace_back(extension);
}

void ExportPlugin::SetMaxChannels(unsigned maxchannels, unsigned index) {
    mFormatInfos[index].mMaxChannels = maxchannels;
}

void ExportPlugin::SetExtensions(const std::vector<std::string> & extensions, int index)
{
   mFormatInfos[index].mExtensions = extensions;
}

//Create a mixer by computing the time warp factor
std::unique_ptr<Mixer> ExportPlugin::CreateMixer(const WaveTrackConstArray &inputTracks,
         double startTime, double stopTime,
         unsigned numOutChannels, size_t outBufferSize, bool outInterleaved,
         double outRate, sampleFormat outFormat,
         bool highQuality, MixerSpec *mixerSpec)
{
   // MB: the stop time should not be warped, this was a bug.
   return std::make_unique<Mixer>(inputTracks,
                  // Throw, to stop exporting, if read fails:
                  true,
                  startTime, stopTime,
                  numOutChannels, outBufferSize, outInterleaved,
                  outRate, outFormat,
                  highQuality, mixerSpec);
}
