/**********************************************************************

  Audacity: A Digital Audio Editor

  NoiseReduction.h

  Dominic Mazzoni
  Vaughan Johnson (Preview)
  Paul Licameli

**********************************************************************/

#ifndef __AUDACITY_EFFECT_NOISE_REDUCTION__
#define __AUDACITY_EFFECT_NOISE_REDUCTION__

#include "MemoryX.h"
#include "WaveTrack.h"

class TrackFactory {
public:
    explicit
    TrackFactory(const std::shared_ptr<DirManager> &dirManager)
                 : mDirManager(dirManager) {
    }

    const std::shared_ptr<DirManager> mDirManager;

    friend class AudacityProject;

    friend class BenchmarkDialog;

public:
    // These methods are defined in WaveTrack.cpp, NoteTrack.cpp,
    // LabelTrack.cpp, and TimeTrack.cpp respectively
    std::unique_ptr<WaveTrack> DuplicateWaveTrack(const WaveTrack &orig);

    std::unique_ptr<WaveTrack> NewWaveTrack(sampleFormat format = (sampleFormat) 0,
                                            double rate = 0);

#if defined(USE_MIDI)
    std::unique_ptr<NoteTrack> NewNoteTrack();
#endif
};

class EffectNoiseReduction final {
public:

    EffectNoiseReduction();

    virtual ~EffectNoiseReduction();

    // Effect implementation

//   using Effect::TrackProgress;


    bool Init();
    bool Process();
    bool Process(WaveTrack *waveTrack);
    bool GetProfile(WaveTrack *track, double t0, double t1, double noiseGain, double sensitivity, double freqSmoothingBands,TrackFactory *factory);
    bool ReduceNoise(WaveTrack *track, double noiseGain, double sensitivity, double freqSmoothingBands, TrackFactory *factory);
    class Settings;

    class Statistics;

// profile extracting region
    double mT0;
    double mT1;

private:
    class Worker;

    friend class Dialog;

    TrackFactory *mFactory;
    std::unique_ptr<Settings> mSettings;
    std::unique_ptr<Statistics> mStatistics;
};

#endif
