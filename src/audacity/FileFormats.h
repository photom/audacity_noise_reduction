/**********************************************************************

  Audacity: A Digital Audio Editor

  FileFormats.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_FILE_FORMATS__
#define __AUDACITY_FILE_FORMATS__

#include <vector>
#include <memory>
#include "sndfile.h"
#include "MemoryX.h"
#include "ODTaskThread.h"

//
// other utility functions
//

bool sf_subtype_more_than_16_bits(unsigned int format);

bool sf_subtype_is_integer(unsigned int format);

std::vector<std::string> sf_get_all_extensions();

/** @brief Get the most common file extension for the given format
 *
 * AND the given format with SF_FORMAT_TYPEMASK to get just the container
 * format, then retreive the most common extension using SFC_GET_FORMAT_INFO.
 * @param format the libsndfile format to get the name for (only the container
 * part is used) */
std::string sf_header_extension(int format);

/** @brief Get the string name of the specified container format
 *
 * AND format with SF_FORMAT_TYPEMASK to get only the container format and
 * then use SFC_GET_FORMAT_INFO to get the description
 * @param format the libsndfile format to get the name for (only the container
 * part is used) */
std::string sf_header_name(int format);

// This function wrapper uses a mutex to serialize calls to the SndFile library.

extern ODLock libSndFileMutex;

template<typename R, typename F, typename... Args>
inline R SFCall(F fun, Args &&... args) {
    // ODLocker locker{&libSndFileMutex};
    ODLocker locker;
    return fun(std::forward<Args>(args)...);
}


//RAII for SNDFILE*
struct SFFileCloser {
    int operator()(SNDFILE *) const;
};

struct SFFile : public std::unique_ptr<SNDFILE, ::SFFileCloser> {
    SFFile() = default;

    SFFile(SFFile &&that)
            : std::unique_ptr<SNDFILE, ::SFFileCloser>(std::move(that)) {}

    // Close explicitly, not ignoring return values.
    int close() {
        auto result = get_deleter()(get());
        release();
        return result;
    }
};

#endif
