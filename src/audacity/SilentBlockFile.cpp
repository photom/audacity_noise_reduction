/**********************************************************************

  Audacity: A Digital Audio Editor

  SilentBlockFile.cpp

  Joshua Haberman

**********************************************************************/


#include <cstring>
#include "SilentBlockFile.h"
#include "SampleFormat.h"

SilentBlockFile::SilentBlockFile(size_t sampleLen):
BlockFile{ wxFileNameWrapper{}, sampleLen }
{
   mMin = 0.;
   mMax = 0.;
   mRMS = 0.;
}

SilentBlockFile::~SilentBlockFile()
{
}

bool SilentBlockFile::ReadSummary(ArrayOf<char> &data)
{
   data.reinit( mSummaryInfo.totalSummaryBytes );
   memset(data.get(), 0, mSummaryInfo.totalSummaryBytes);
   return true;
}

size_t SilentBlockFile::ReadData(samplePtr data, sampleFormat format,
                              size_t start, size_t len, bool) const
{
   ClearSamples(data, format, 0, len);

   return len;
}

/// Create a copy of this BlockFile
BlockFilePtr SilentBlockFile::Copy(wxFileNameWrapper &&)
{
   auto newBlockFile = make_blockfile<SilentBlockFile>(mLen);

   return newBlockFile;
}

auto SilentBlockFile::GetSpaceUsage() const -> DiskByteCount
{
   return 0;
}

