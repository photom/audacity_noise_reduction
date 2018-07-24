#directory src
project(AudacityNoiseReduction)

include_directories(${CMAKE_SOURCE_DIR}/src/audacity)

set(LIB_SOURCE
        Audacity.h
        BlockFile.cpp
        BlockFile.h
        DirManager.cpp
        DirManager.h
        Dither.cpp
        Dither.h
        Envelope.cpp
        Envelope.h
        Export.h
        Export.cpp
        ExportPCM.cpp
        ExportPCM.h
        FileException.cpp
        FileException.h
        FileFormats.cpp
        FileFormats.h
        float_cast.h
        ImportPCM.cpp
        ImportPCM.h
        ImportPlugin.h
        InconsistencyException.cpp
        InconsistencyException.h
        MemoryX.h
        Mix.h
        Mix.cpp
        NoiseReduction.h
        NoiseReduction.cpp
        ODTaskThread.cpp
        ODTaskThread.h
        RealFFTf.cpp
        RealFFTf.h
        Resample.cpp
        Resample.h
        SampleFormat.cpp
        SampleFormat.h
        Sequence.cpp
        Sequence.h
        SilentBlockFile.cpp
        SilentBlockFile.h
        SimpleBlockFile.cpp
        SimpleBlockFile.h
        TimeWarper.cpp
        TimeWarper.h
        Track.cpp
        Track.h
        Types.h
        Utils.h
        WaveClip.cpp
        WaveClip.h
        WaveTrack.cpp
        WaveTrack.h
        wxFileName.cpp
        wxFileName.h
        wxFileNameWrapper.h
        wxTokenzr.cpp
        wxTokenzr.h)

add_library(audacity-noisered SHARED ${LIB_SOURCE})

set_target_properties(audacity-noisered PROPERTIES LINKER_LANGUAGE CXX)
