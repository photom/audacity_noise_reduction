/**********************************************************************

  Audacity: A Digital Audio Editor

  Track.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_TRACK__
#define __AUDACITY_TRACK__


#include "DirManager.h"
#include "WaveTrack.h"

class Track {

};


class TrackFactory
{
 private:
   TrackFactory(const std::shared_ptr<DirManager> &&dirManager):
      mDirManager(dirManager)
   {
   }

   const std::shared_ptr<DirManager> mDirManager;
   friend class AudacityProject;
   friend class BenchmarkDialog;

 public:
   // These methods are defined in WaveTrack.cpp, NoteTrack.cpp,
   // LabelTrack.cpp, and TimeTrack.cpp respectively
   std::unique_ptr<WaveTrack> DuplicateWaveTrack(const WaveTrack &orig);
   std::unique_ptr<WaveTrack> NewWaveTrack(sampleFormat format = (sampleFormat)0,
                           double rate = 0);
#if defined(USE_MIDI)
   std::unique_ptr<NoteTrack> NewNoteTrack();
#endif
};

#endif
