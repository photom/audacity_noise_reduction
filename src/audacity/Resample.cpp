/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2012 Audacity Team.
   License: GPL v2.  See License.txt.

   Resample.cpp
   Dominic Mazzoni, Rob Sykes, Vaughan Johnson

******************************************************************//**

\class Resample
\brief Interface to libsoxr.

   This class abstracts the interface to different resampling libraries:

      libsoxr, written by Rob Sykes. LGPL.

   Since Audacity always does resampling on mono streams that are
   contiguous in memory, this class doesn't support multiple channels
   or some of the other optional features of some of these resamplers.

*//*******************************************************************/

#include "Resample.h"
#include <soxr.h>


/**************************************************************************//**

\brief IdentInterfaceSymbol pairs a persistent string identifier used internally
with an optional, different string as msgid for lookup in a translation catalog.
\details  If there is need to change a msgid in a later version of the
program, change the constructor call to supply a second argument but leave the
first the same, so that compatibility of older configuration files containing
that internal string is not broken.
********************************************************************************/
class IdentInterfaceSymbol {
public:
    IdentInterfaceSymbol() = default;

    // Allows implicit construction from a msgid re-used as an internal string
    IdentInterfaceSymbol(const std::string &msgid)
            : mInternal{msgid}, mMsgid{msgid} {}

    // Allows implicit construction from a msgid re-used as an internal string
    IdentInterfaceSymbol(const char *msgid)
            : mInternal{msgid}, mMsgid{msgid} {}

    // Two-argument version distinguishes internal from translatable string
    // such as when the first squeezes spaces out
    IdentInterfaceSymbol(const std::string &internal, const std::string &msgid)
            : mInternal{internal}
            // Do not permit non-empty msgid with empty internal
            , mMsgid{internal.empty() ? std::string{} : msgid} {}

    const std::string &Internal() const { return mInternal; }

    const std::string &Msgid() const { return mMsgid; }

    bool empty() const { return mInternal.empty(); }

    friend inline bool operator==(
            const IdentInterfaceSymbol &a, const IdentInterfaceSymbol &b) { return a.mInternal == b.mInternal; }

    friend inline bool operator!=(
            const IdentInterfaceSymbol &a, const IdentInterfaceSymbol &b) { return !(a == b); }

private:
    std::string mInternal;
    std::string mMsgid;
};

// Packages a table of user-visible choices each with an internal code string,
// a preference key path,
// and a default choice
class EnumSetting {
public:
    EnumSetting(
            const std::string &key,
            const IdentInterfaceSymbol symbols[], size_t nSymbols,
            size_t defaultSymbol
    )
            : mKey{key}, mSymbols{symbols}, mnSymbols{nSymbols}, mDefaultSymbol{defaultSymbol} {
        assert(defaultSymbol < nSymbols);
    }

    const std::string &Key() const { return mKey; }

    const IdentInterfaceSymbol &Default() const { return mSymbols[mDefaultSymbol]; }

    const IdentInterfaceSymbol *begin() const { return mSymbols; }

    const IdentInterfaceSymbol *end() const { return mSymbols + mnSymbols; }

    std::string Read() const;


protected:
    size_t Find(const std::string &value) const;

    const std::string mKey;

    const IdentInterfaceSymbol *mSymbols;
    const size_t mnSymbols;

    // stores an internal value
    mutable bool mMigrated{false};

    const size_t mDefaultSymbol;
};


// Extends EnumSetting with a corresponding table of integer codes
// (generally not equal to their table positions),
// and optionally an old preference key path that stored integer codes, to be
// migrated into one that stores internal string values instead
class EncodedEnumSetting : public EnumSetting {
public:
    EncodedEnumSetting(
            const std::string &key,
            const IdentInterfaceSymbol symbols[], size_t nSymbols,
            size_t defaultSymbol,

            const int intValues[] = nullptr, // must have same size as symbols
            const std::string &oldKey = std::string("")
    )
            : EnumSetting{key, symbols, nSymbols, defaultSymbol}, mIntValues{intValues}, mOldKey{oldKey} {
        assert(mIntValues);
    }

    // Read and write the encoded values
    virtual int ReadInt() const;

protected:

private:
    const int *mIntValues;
    const std::string mOldKey;
};

int EncodedEnumSetting::ReadInt() const
{
   if (!mIntValues)
      return 0;

   auto index = Find( Read() );
   assert( index < mnSymbols );
   return mIntValues[ index ];
}
std::string EnumSetting::Read() const
{
   const auto &defaultValue = Default().Internal();
   std::string value;

   // Remap to default if the string is not known -- this avoids surprises
   // in case we try to interpret config files from future versions
   auto index = Find( value );
   if ( index >= mnSymbols )
      value = defaultValue;
   return value;
}

size_t EnumSetting::Find( const std::string &value ) const
{
   return size_t(
      std::find( begin(), end(), IdentInterfaceSymbol{ value, {} } )
         - mSymbols );
}

Resample::Resample(const bool useBestMethod, const double dMinFactor, const double dMaxFactor) {
    this->SetMethod(useBestMethod);
    soxr_quality_spec_t q_spec;
    if (dMinFactor == dMaxFactor) {
        mbWantConstRateResampling = true; // constant rate resampling
        q_spec = soxr_quality_spec("\0\1\4\6"[mMethod], 0);
    } else {
        mbWantConstRateResampling = false; // variable rate resampling
        q_spec = soxr_quality_spec(SOXR_HQ, SOXR_VR);
    }
    mHandle.reset(soxr_create(1, dMinFactor, 1, 0, 0, &q_spec, 0));
}

Resample::~Resample() {
}

//////////
static const IdentInterfaceSymbol methodNames[] = {
        {"LowQuality",    "Low Quality (Fastest)"},
        {"MediumQuality", "Medium Quality"},
        {"HighQuality",   "High Quality"},
        {"BestQuality",   "Best Quality (Slowest)"}
};

static const size_t numMethods = sizeof(methodNames)/sizeof(methodNames[0]);

static const std::string fastMethodKey =
        "/Quality/LibsoxrSampleRateConverterChoice";

static const std::string bestMethodKey =
        "/Quality/LibsoxrHQSampleRateConverterChoice";

static const std::string oldFastMethodKey =
        "/Quality/LibsoxrSampleRateConverter";

static const std::string oldBestMethodKey =
        "/Quality/LibsoxrHQSampleRateConverter";

static const size_t fastMethodDefault = 1; // Medium Quality
static const size_t bestMethodDefault = 3; // Best Quality

static const int intChoicesMethod[] = {
        0, 1, 2, 3
};

static_assert((sizeof(intChoicesMethod)/sizeof(intChoicesMethod[0])) == numMethods, "size mismatch");

EncodedEnumSetting Resample::FastMethodSetting{
        fastMethodKey,
        methodNames, numMethods,
        fastMethodDefault,

        intChoicesMethod,
        oldFastMethodKey
};

EncodedEnumSetting Resample::BestMethodSetting
        {
                bestMethodKey,
                methodNames, numMethods,
                bestMethodDefault,

                intChoicesMethod,
                oldBestMethodKey
        };

//////////
std::pair<size_t, size_t>
Resample::Process(double factor,
                  float *inBuffer,
                  size_t inBufferLen,
                  bool lastFlag,
                  float *outBuffer,
                  size_t outBufferLen) {
    size_t idone, odone;
    if (mbWantConstRateResampling) {
        soxr_process(mHandle.get(),
                     inBuffer, (lastFlag ? ~inBufferLen : inBufferLen), &idone,
                     outBuffer, outBufferLen, &odone);
    } else {
        soxr_set_io_ratio(mHandle.get(), 1 / factor, 0);

        inBufferLen = lastFlag ? ~inBufferLen : inBufferLen;
        soxr_process(mHandle.get(),
                     inBuffer, inBufferLen, &idone,
                     outBuffer, outBufferLen, &odone);
    }
    return {idone, odone};
}

void Resample::SetMethod(const bool useBestMethod) {
    if (useBestMethod)
        mMethod = BestMethodSetting.ReadInt();
    else
        mMethod = FastMethodSetting.ReadInt();
}
