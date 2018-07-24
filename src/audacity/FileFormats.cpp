/**********************************************************************

  Audacity: A Digital Audio Editor

  FileFormats.cpp

  Dominic Mazzoni

*******************************************************************//*!

\file FileFormats.cpp
\brief Works with libsndfile to provide encoding and other file
information.

*//*******************************************************************/

#include <iostream>
#include <cstring>
#include <string>
#include "FileFormats.h"
#include "Utils.h"

bool sf_subtype_more_than_16_bits(unsigned int format) {
    unsigned int subtype = format & SF_FORMAT_SUBMASK;
    return (subtype == SF_FORMAT_FLOAT ||
            subtype == SF_FORMAT_DOUBLE ||
            subtype == SF_FORMAT_PCM_24 ||
            subtype == SF_FORMAT_PCM_32);
}

bool sf_subtype_is_integer(unsigned int format) {
    unsigned int subtype = format & SF_FORMAT_SUBMASK;
    return (subtype == SF_FORMAT_PCM_16 ||
            subtype == SF_FORMAT_PCM_24 ||
            subtype == SF_FORMAT_PCM_32);
}

std::string sf_header_extension(int format) {
    SF_FORMAT_INFO format_info;

    memset(&format_info, 0, sizeof(format_info));
    format_info.format = (format & SF_FORMAT_TYPEMASK);
    sf_command(nullptr, SFC_GET_FORMAT_INFO, &format_info, sizeof(format_info));

    return std::string(format_info.extension);
}

std::string sf_header_name(int format) {
    SF_FORMAT_INFO format_info;

    memset(&format_info, 0, sizeof(format_info));
    format_info.format = (format & SF_FORMAT_TYPEMASK);
    sf_command(nullptr, SFC_GET_FORMAT_INFO, &format_info, sizeof(format_info));

    return std::string(format_info.name);
}

std::vector<std::string> sf_get_all_extensions() {
    std::vector<std::string> exts;
    SF_FORMAT_INFO format_info;
    int count, k;

    memset(&format_info, 0, sizeof(format_info));

    sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT,
               &count, sizeof(count));

    for (k = 0; k < count; k++) {
        format_info.format = k;
        sf_command(nullptr, SFC_GET_FORMAT_MAJOR,
                   &format_info, sizeof(format_info));

        exts.emplace_back(std::string(format_info.extension));
    }

    // Some other extensions that are often sound files
    // but aren't included by libsndfile

    exts.emplace_back("aif"); // AIFF file with a DOS-style extension
    exts.emplace_back("ircam");
    exts.emplace_back("snd");
    exts.emplace_back("svx");
    exts.emplace_back("svx8");
    exts.emplace_back("sv16");

    return exts;
}

ODLock libSndFileMutex;

int SFFileCloser::operator()(SNDFILE *sf) const {
    auto err = SFCall<int>(sf_close, sf);
    if (err) {
        char buffer[1000];
        sf_error_str(sf, buffer, 1000);
        /* i18n-hint: %s will be the error message from libsndfile */
        std::cerr << string_format("Error (file may not have been written): %s", buffer) << std::endl;
    }
    return err;
}
