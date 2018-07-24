/**********************************************************************

  Audacity: A Digital Audio Editor

  ExportPCM.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_EXPORTPCM__
#define __AUDACITY_EXPORTPCM__

#include "ImportPlugin.h"
#include "Export.h"
#include "Mix.h"

class ExportPCM final : ExportPlugin {
public:

    ExportPCM();

    // Required

    ProgressResult Export(
            WaveTrackConstArray& tracks,
            const std::string &fName,
            MixerSpec *mixerSpec = nullptr,
            int subformat = 0) override;

};


#endif
