/**********************************************************************

  Audacity: A Digital Audio Editor

  DirManager.h

  Dominic Mazzoni

**********************************************************************/

#ifndef _DIRMANAGER_
#define _DIRMANAGER_

#include <unordered_map>

#include "MemoryX.h"
#include "wxFileNameWrapper.h"
#include "Sequence.h"
#include "BlockFile.h"



using BlockHash = std::unordered_map<std::string, std::weak_ptr<BlockFile>>;

using DirHash = std::unordered_map<int, int>;
class BlockArray;

class DirManager {
public:

    static void SetTempDir(const std::string &_temp) { globaltemp = _temp; }

    // MM: Construct DirManager
    DirManager();

    virtual ~DirManager();


    BlockFilePtr
    NewSimpleBlockFile(samplePtr sampleData,
                       size_t sampleLen,
                       sampleFormat format,
                       bool allowDeferredWrite = false);

    // Hashes for management of the sub-directory tree of _data
    struct BalanceInfo {
        DirHash dirTopPool;    // available toplevel dirs
        DirHash dirTopFull;    // full toplevel dirs
        DirHash dirMidPool;    // available two-level dirs
        DirHash dirMidFull;    // full two-level dirs
    } mBalanceInfo;

    // Accessor for the balance info, may need to do a delayed update for
    // deletion in case other threads DELETE block files
    BalanceInfo &GetBalanceInfo();

    int BalanceMidAdd(int, int);

    void BalanceFileAdd(int);

    /// Check for existing using filename using complete filename
    bool ContainsBlockFile(const std::string &filepath) const;

    bool AssignFile(wxFileNameWrapper &filename, const std::string &value, bool check);

    wxFileNameWrapper MakeBlockFilePath(const std::string &value);

    // Adds one to the reference count of the block file,
    // UNLESS it is "locked", then it makes a NEW copy of
    // the BlockFile.
    // May throw an exception in case of disk space exhaustion, otherwise
    // returns non-null.
    BlockFilePtr CopyBlockFile(const BlockFilePtr &b);

    // Get directory where data files are in. Note that projects are normally
    // not interested in this information, but it is important for the
    // auto-save functionality
    std::string GetDataFilesDir() const;

    bool CopyFile(const std::string &file1, const std::string &file2);

    void BalanceInfoDel(const std::string &);

    static void CleanTempDir();

    static void CleanDir(const std::string &path);

private:
    static std::string globaltemp;
    static int numDirManagers;

    wxFileNameWrapper MakeBlockFileName();

    std::vector<std::string> aliasList;

    BlockHash mBlockFileHash; // repository for blockfiles
    std::string projFull;
    std::string projName;
    std::string projPath;
    std::string mytemp;
    BlockArray *mLoadingTarget;
    unsigned mLoadingTargetIdx;


    size_t mMaxSamples; // max samples per block

    unsigned long mLastBlockFileDestructionCount{0};

};


#endif
