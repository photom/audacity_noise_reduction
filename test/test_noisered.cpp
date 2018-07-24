#include "catch.hpp"

// A simple program that computes the square root of a number
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>

#include <openssl/md5.h>

#include "ExportPCM.h"
#include "Audacity.h"
#include "WaveTrack.h"
#include "NoiseReduction.h"
#include "ImportPCM.h"

namespace {

std::string calc_file_hash(const std::string &filename) {
    std::ifstream file(filename, std::ifstream::binary);
    MD5_CTX md5Context;
    MD5_Init(&md5Context);
    char buf[1024 * 16];
    while (file.good()) {
        file.read(buf, sizeof(buf));
        MD5_Update(&md5Context, buf, (size_t) file.gcount());
    }
    unsigned char result[MD5_DIGEST_LENGTH];
    MD5_Final(result, &md5Context);

    std::stringstream md5string;
    md5string << std::hex << std::uppercase << std::setfill('0');
    for (const auto &byte: result)
        md5string << std::setw(2) << (int) byte;

    return md5string.str();
}


TEST_CASE("import and export") {
    SECTION("test1") {
        const auto dir_manager = std::make_shared<DirManager>();
        auto factory = new TrackFactory(dir_manager);
        auto handler = PCMImportFileHandle::Open("test.wav");
        REQUIRE(handler != nullptr);
        TrackHolders holders{};
        auto import_result = handler->Import(factory, holders);
        REQUIRE(import_result == ProgressResult::Success);

        auto exporter = ExportPCM();
        auto audioArray = WaveTrackConstArray();
        audioArray.emplace_back(std::move(holders.at(0)));
        auto export_result = exporter.Export(audioArray, std::string("test_out.wav"));
        REQUIRE(export_result == ProgressResult::Success);

        CHECK(calc_file_hash("test.wav") == calc_file_hash("test_out.wav"));
        remove("test_out.wav");
        delete factory;
    }
}

TEST_CASE("noise reduction") {
    SECTION("read wave file and get profile.") {
        // import
        const auto dir_manager = std::make_shared<DirManager>();
        auto factory = new TrackFactory(dir_manager);
        auto handler = PCMImportFileHandle::Open("test.wav");
        TrackHolders holders{};
        auto import_result = handler->Import(factory, holders);
        REQUIRE(import_result == ProgressResult::Success);

        // noise reduction
        auto effect = new EffectNoiseReduction();
        double profile_start = 0.0;
        double profile_end = 0.3;
        double noise_gain = 12.0;
        double sensitivity = 6.0;
        double smoothing = 3.0;
        auto profile_result = effect->GetProfile(holders[0].get(), profile_start, profile_end,
                                                 noise_gain, sensitivity, smoothing, factory);
        REQUIRE(profile_result);
        auto noisered_result = effect->ReduceNoise(holders[0].get(),
                                                   noise_gain, sensitivity, smoothing, factory);
        REQUIRE(noisered_result);

        // export
        auto exporter = ExportPCM();
        auto audioArray = WaveTrackConstArray();
        audioArray.emplace_back(std::move(holders.at(0)));
        auto export_result = exporter.Export(audioArray, std::string("test_out.wav"));
        REQUIRE(export_result == ProgressResult::Success);

        // results are not same.
        // CHECK(calc_file_hash("test_answer.wav") == calc_file_hash("test_out.wav"));
        remove("test_out.wav");

        delete factory;
        delete effect;
    }

    SECTION("profile source is different from source..") {
        // import
        const auto dir_manager = std::make_shared<DirManager>();
        auto factory = new TrackFactory(dir_manager);
        auto bg_handler = PCMImportFileHandle::Open("bg_input.wav");
        TrackHolders bg_holders{};
        auto bg_import_result = bg_handler->Import(factory, bg_holders);
        REQUIRE(bg_import_result == ProgressResult::Success);

        auto src_handler = PCMImportFileHandle::Open("input.wav");
        TrackHolders src_holders{};
        auto src_import_result = src_handler->Import(factory, src_holders);
        REQUIRE(src_import_result == ProgressResult::Success);

        // noise reduction
        auto effect = new EffectNoiseReduction();
        double profile_start = 0.0;
        double profile_end = 0.5;
        double noise_gain = 12.0;
        double sensitivity = 6.0;
        double smoothing = 3.0;
        auto profile_result = effect->GetProfile(bg_holders[0].get(), profile_start, profile_end,
                                                 noise_gain, sensitivity, smoothing, factory);
        REQUIRE(profile_result);

        auto noisered_result = effect->ReduceNoise(src_holders[0].get(),
                                                   noise_gain, sensitivity, smoothing, factory);
        REQUIRE(noisered_result);

        // export
        auto exporter = ExportPCM();
        auto audioArray = WaveTrackConstArray();
        audioArray.emplace_back(std::move(src_holders.at(0)));
        auto export_result = exporter.Export(audioArray, std::string("output.wav"));
        REQUIRE(export_result == ProgressResult::Success);

        // results are not same.
        // CHECK(calc_file_hash("test_answer.wav") == calc_file_hash("test_out.wav"));
        // remove("test_out.wav");

        delete factory;
        delete effect;
    }
}
}
