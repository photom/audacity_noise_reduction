/**********************************************************************

  Audacity: A Digital Audio Editor

  ImportPlugin.h

  Joshua Haberman
  Leland Lucius

*******************************************************************//**

\file ImportPlugin.h
\brief
  The interface that all file import "plugins" (if you want to call
  them that) must implement.  Defines ImportFileHandle, ImportPlugin,
  UnusableImportPlugin, ImportPluginList and UnusableImportPluginList.

  Since this is part of libaudacity, it must not use any GUI parts
  of wxWidgets.

*//****************************************************************//**

\class ImportFileHandle
\brief Base class for FlacImportFileHandle, LOFImportFileHandle,
MP3ImportFileHandle, OggImportFileHandle and PCMImportFileHandle.
Gives API for sound file import.

*//****************************************************************//**

\class ImportPlugin
\brief Base class for FlacImportPlugin, LOFImportPlugin,
MP3ImportPlugin, OggImportPlugin and PCMImportPlugin.
Gives API for sound file import.

*//****************************************************************//**

\class UnusableImportPlugin
\brief Used in place of a real plug in for plug ins that have not
been compiled or are not available in this version of Audacity.  Has
enough information to identify the file extensions that would be used,
but little else.

*//****************************************************************//**

\class ImportPluginList
\brief An ImportPlugin list.

*//****************************************************************//**

\class UnusableImportPluginList
\brief An UnusableImportPlugin list.

*//*******************************************************************/

#ifndef __AUDACITY_IMPORTER__
#define __AUDACITY_IMPORTER__


#include "wxFileName.h"
#include "NoiseReduction.h"

using TrackHolders = std::vector<std::unique_ptr<WaveTrack>>;

enum class ProgressResult : unsigned {
    Cancelled = 0, //<! User says that whatever is happening is undesirable and shouldn't have happened at all
    Success,       //<! User says nothing, everything works fine, continue doing whatever we're doing
    Failed,        //<! Something has gone wrong, we should stop and cancel everything we did
    Stopped        //<! Nothing is wrong, but user says we should stop now and leave things as they are now
};

class ImportFileHandle /* not final */
{
public:
    ImportFileHandle(const std::string &filename)
            : mFilename(filename) {
    }

    virtual ~ImportFileHandle() {
    }

    // This is similar to GetImporterDescription, but if possible the
    // importer will return a more specific description of the
    // specific file that is open.
    virtual std::string GetFileDescription() = 0;

    // Return an estimate of how many bytes the file will occupy once
    // imported.  In principle this may exceed main memory, so don't use
    // size_t.
    using ByteCount = unsigned long long;

    virtual ByteCount GetFileUncompressedBytes() = 0;

    // do the actual import, creating whatever tracks are necessary with
    // the TrackFactory and calling the progress callback every iteration
    // through the importing loop
    // The given Tags structure may also be modified.
    // In case of errors or exceptions, it is not necessary to leave outTracks
    // or tags unmodified.
    virtual ProgressResult Import(TrackFactory *trackFactory, TrackHolders &outTracks) = 0;

    // Return number of elements in stream list
    virtual int32_t GetStreamCount() = 0;

    // Return stream descriptions list
    virtual const std::vector<std::string> &GetStreamInfo() = 0;

    // Set stream "import/don't import" flag
    virtual void SetStreamUsage(int32_t StreamID, bool Use) = 0;

protected:
    std::string mFilename;
};

#endif
