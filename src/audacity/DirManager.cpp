/**********************************************************************

   Audacity: A Digital Audio Editor
   Audacity(R) is copyright (c) 1999-2008 Audacity Team.
   License: GPL v2.  See License.txt.

   DirManager.cpp

   Dominic Mazzoni
   Matt Brubeck
   Michael Chinen
   James Crook
   Al Dimond
   Brian Gunlogson
   Josh Haberman
   Vaughan Johnson
   Leland Lucius
   Monty
   Markus Meyer

*******************************************************************//*!

\class DirManager
\brief Creates and manages BlockFile objects.

  This class manages the files that a project uses to store most
  of its data.  It creates NEW BlockFile objects, which can
  be used to store any type of data.  BlockFiles support all of
  the common file operations, but they also support reference
  counting, so two different parts of a project can point to
  the same block of data.

  For example, a track might contain 10 blocks of data representing
  its audio.  If you copy the last 5 blocks and paste at the
  end of the file, no NEW blocks need to be created - we just store
  pointers to NEW ones.  When part of a track is deleted, the
  affected blocks decrement their reference counts, and when they
  reach zero they are deleted.  This same mechanism is also used
  to implement Undo.

  The DirManager, besides mapping filenames to absolute paths,
  also hashes all of the block names used in a project, so that
  when reading a project from disk, multiple copies of the
  same block still get mapped to the same BlockFile object.

  The blockfile/directory scheme is rather complicated with two different schemes.
  The current scheme uses two levels of subdirectories - up to 256 'eXX' and up to
  256 'dYY' directories within each of the 'eXX' dirs, where XX and YY are hex chars.
  In each of the dXX directories there are up to 256 audio files (e.g. .au or .auf).
  They have a filename scheme of 'eXXYYZZZZ', where XX and YY refers to the
  subdirectories as above.  The 'ZZZZ' component is generated randomly for some reason.
  The XX and YY components are sequential.
  DirManager fills up the current dYY subdir until 256 are created, and moves on to the next one.

  So for example, the first blockfile created may be 'e00/d00/e0000a23b.au' and the next
  'e00/d00/e000015e8.au', and the 257th may be 'e00/d01/e0001f02a.au'.
  On close the blockfiles that are no longer referenced by the project (edited or deleted) are removed,
  along with the consequent empty directories.


*//*******************************************************************/


#include <string>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h> // stat
#include <errno.h>    // errno, ENOENT, EEXIST
#include <fstream>
#include <zconf.h>
#include <cstring>

#include "Audacity.h"
#include "DirManager.h"
#include "MemoryX.h"
#include "SimpleBlockFile.h"
#include "Utils.h"
#include "InconsistencyException.h"
#include "FileException.h"

static
int remove_directory(const char *path) {
    DIR *d = opendir(path);
    size_t path_len = strlen(path);
    int r = -1;

    if (d) {
        struct dirent *p;

        r = 0;

        while (!r && (p = readdir(d))) {
            int r2 = -1;
            char *buf;
            size_t len;

            /* Skip the names "." and ".." as we don't want to recurse on them. */
            if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
                continue;
            }

            len = path_len + strlen(p->d_name) + 2;
            buf = (char *) malloc(len);

            if (buf) {
                struct stat statbuf;

                snprintf(buf, len, "%s/%s", path, p->d_name);

                if (!stat(buf, &statbuf)) {
                    if (S_ISDIR(statbuf.st_mode)) {
                        r2 = remove_directory(buf);
                    } else {
                        r2 = unlink(buf);
                    }
                }

                free(buf);
            }

            r = r2;
        }

        closedir(d);
    }

    if (!r) {
        r = rmdir(path);
    }

    return r;
}

static bool isDirExist(const std::string &path) {

    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return false;
    }
    return (info.st_mode & S_IFDIR) != 0;
}

std::string DirManager::globaltemp("/dev/shm/audacity-noisered");
int DirManager::numDirManagers = 0;

DirManager::DirManager() {
    mLastBlockFileDestructionCount = BlockFile::gBlockFileDestructionCount;

    // Seed the random number generator.
    // this need not be strictly uniform or random, but it should give
    // unclustered numbers
    srand(time(nullptr));

    // Set up local temp subdir
    // Previously, Audacity just named project temp directories "project0",
    // "project1" and so on. But with the advent of recovery code, we need a
    // unique name even after a crash. So we create a random project index
    // and make sure it is not used already. This will not pose any performance
    // penalties as long as the number of open Audacity projects is much
    // lower than RAND_MAX.
    do {
        mytemp = globaltemp + "/" + string_format("project%d", (int) rand());
    } while (isDirExist(mytemp));

    numDirManagers++;

    projPath = "";
    projName = "";

    mLoadingTarget = nullptr;
    mLoadingTargetIdx = 0;
    mMaxSamples = ~size_t(0);

    // toplevel pool hash is fully populated to begin
    {
        // We can bypass the accessor function while initializing
        auto &balanceInfo = mBalanceInfo;
        auto &dirTopPool = balanceInfo.dirTopPool;
        for (int i = 0; i < 256; ++i)
            dirTopPool[i] = 0;
    }
}

DirManager::~DirManager() {
    numDirManagers--;
    if (numDirManagers == 0) {
        CleanTempDir();
        //::wxRmdir(temp);
    } else if (projFull.empty() && !mytemp.empty()) {
        CleanDir(mytemp);
    }
}

// static
// This is quite a dangerous function.  In the temp dir it will DELETE every directory
// recursively, that has 'project*' as the name - EVEN if it happens not to be an Audacity
// project but just something else called project.
void DirManager::CleanTempDir() {
    // with default flags (none) this does not clean the top directory, and may remove non-empty
    // directories.
    CleanDir(globaltemp);
}

// static
void DirManager::CleanDir(const std::string &path) {
    remove_directory(path.c_str());
}


BlockFilePtr DirManager::NewSimpleBlockFile(
        samplePtr sampleData, size_t sampleLen,
        sampleFormat format,
        bool allowDeferredWrite) {
    wxFileNameWrapper filePath{MakeBlockFileName()};
    const std::string fileName{filePath.GetName()};

    auto newBlockFile = std::make_shared<SimpleBlockFile>
            (std::move(filePath), sampleData, sampleLen, format, allowDeferredWrite, false);

    mBlockFileHash[fileName] = newBlockFile;

    return newBlockFile;
}

// only determines appropriate filename and subdir balance; does not
// perform maintainence
wxFileNameWrapper DirManager::MakeBlockFileName() {
    auto &balanceInfo = GetBalanceInfo();
    auto &dirMidPool = balanceInfo.dirMidPool;
    auto &dirTopPool = balanceInfo.dirTopPool;
    auto &dirTopFull = balanceInfo.dirTopFull;

    wxFileNameWrapper ret;
    std::string baseFileName;

    unsigned int filenum, midnum, topnum, midkey;

    while (1) {

        /* blockfiles are divided up into heirarchical directories.
           Each toplevel directory is represented by "e" + two unique
           hexadecimal digits, for a total possible number of 256
           toplevels.  Each toplevel contains up to 256 subdirs named
           "d" + two hex digits.  Each subdir contains 'a number' of
           files.  */

        filenum = 0;
        midnum = 0;
        topnum = 0;

        // first action: if there is no available two-level directory in
        // the available pool, try to make one

        if (dirMidPool.empty()) {

            // is there a toplevel directory with space for a NEW subdir?

            if (!dirTopPool.empty()) {

                // there's still a toplevel with room for a subdir

                DirHash::iterator iter = dirTopPool.begin();
                int newcount = 0;
                topnum = iter->first;


                // search for unused midlevels; linear search adequate
                // add 32 NEW topnum/midnum dirs full of  prospective filenames to midpool
                for (midnum = 0; midnum < 256; midnum++) {
                    midkey = (topnum << 8) + midnum;
                    if (BalanceMidAdd(topnum, midkey)) {
                        newcount++;
                        if (newcount >= 32)break;
                    }
                }

                if (dirMidPool.empty()) {
                    // all the midlevels in this toplevel are in use yet the
                    // toplevel claims some are free; this implies multiple
                    // internal logic faults, but simply giving up and going
                    // into an infinite loop isn't acceptible.  Just in case,
                    // for some reason, we get here, dynamite this toplevel so
                    // we don't just fail.

                    // this is 'wrong', but the best we can do given that
                    // something else is also wrong.  It will contain the
                    // problem so we can keep going without worry.
                    dirTopPool.erase(topnum);
                    dirTopFull[topnum] = 256;
                }
                continue;
            }
        }

        if (dirMidPool.empty()) {
            // still empty, thus an absurdly large project; all dirs are
            // full to 256/256/256; keep working, but fall back to 'big
            // filenames' and randomized placement

            filenum = rand();
            midnum = (int) (256. * rand() / (RAND_MAX + 1.));
            topnum = (int) (256. * rand() / (RAND_MAX + 1.));
            midkey = (topnum << 8) + midnum;


        } else {

            DirHash::iterator iter = dirMidPool.begin();
            midkey = iter->first;

            // split the retrieved 16 bit directory key into two 8 bit numbers
            topnum = midkey >> 8;
            midnum = midkey & 0xff;
            filenum = (int) (4096. * rand() / (RAND_MAX + 1.));

        }

        baseFileName = string_format("e%02x%02x%03x", topnum, midnum, filenum);

        if (!ContainsBlockFile(baseFileName)) {
            // not in the hash, good.
            if (!this->AssignFile(ret, baseFileName, true)) {
                // this indicates an on-disk collision, likely due to an
                // orphan blockfile.  We should try again, but first
                // alert the balancing info there's a phantom file here;
                // if the directory is nearly full of orphans we neither
                // want performance to suffer nor potentially get into an
                // infinite loop if all possible filenames are taken by
                // orphans (unlikely but possible)
                BalanceFileAdd(midkey);

            } else break;
        }
    }
    // FIXME: Might we get here without midkey having been set?
    //    Seemed like a possible problem in these changes in .aup directory hierarchy.
    BalanceFileAdd(midkey);
    return ret;
}

int DirManager::BalanceMidAdd(int topnum, int midkey) {
    // enter the midlevel directory if it doesn't exist

    auto &balanceInfo = GetBalanceInfo();
    auto &dirMidPool = balanceInfo.dirMidPool;
    auto &dirMidFull = balanceInfo.dirMidFull;
    auto &dirTopPool = balanceInfo.dirTopPool;
    auto &dirTopFull = balanceInfo.dirTopFull;

    if (dirMidPool.find(midkey) == dirMidPool.end() &&
        dirMidFull.find(midkey) == dirMidFull.end()) {
        dirMidPool[midkey] = 0;

        // increment toplevel directory fill
        dirTopPool[topnum]++;
        if (dirTopPool[topnum] >= 256) {
            // this toplevel is now full; move it to the full hash
            dirTopPool.erase(topnum);
            dirTopFull[topnum] = 256;
        }
        return 1;
    }
    return 0;
}

void DirManager::BalanceFileAdd(int midkey) {
    auto &balanceInfo = GetBalanceInfo();
    auto &dirMidPool = balanceInfo.dirMidPool;
    auto &dirMidFull = balanceInfo.dirMidFull;

    // increment the midlevel directory usage information
    if (dirMidPool.find(midkey) != dirMidPool.end()) {
        dirMidPool[midkey]++;
        if (dirMidPool[midkey] >= 256) {
            // this middir is now full; move it to the full hash
            dirMidPool.erase(midkey);
            dirMidFull[midkey] = 256;
        }
    } else {
        // this case only triggers in absurdly large projects; we still
        // need to track directory fill even if we're over 256/256/256
        dirMidPool[midkey]++;
    }
}

bool DirManager::ContainsBlockFile(const std::string &filepath) const {
    // check what the hash returns in case the blockfile is from a different project
    BlockHash::const_iterator it = mBlockFileHash.find(filepath);
    return it != mBlockFileHash.end() &&
           BlockFilePtr{it->second.lock()};
}

bool DirManager::AssignFile(wxFileNameWrapper &fileName,
                            const std::string &value,
                            bool diskcheck) {
    wxFileNameWrapper dir{MakeBlockFilePath(value)};

    if (diskcheck) {
        // verify that there's no possible collision on disk.  If there
        // is, log the problem and return FALSE so that MakeBlockFileName
        // can try again
        DIR *dp = opendir(dir.GetFullPath().c_str());
        if (dp == nullptr) {
            return false;
        }
        // this code is only valid if 'value' has no extention; that
        // means, effectively, AssignFile may be called with 'diskcheck'
        // set to true only if called from MakeFileBlockName().

        std::string filespec = string_format("%s.*", value);
        struct dirent *dirp;
        if ((dirp = readdir(dp)) != nullptr) {
            std::string filename = std::string(dirp->d_name);
            if (filename.length() >= value.length() + 1 &&
                filename.substr(0, value.length() + 1) == (value + ".")) {

                // collision with on-disk state!
                std::string msg = string_format(
                        "Audacity found an orphan block file: %s. \nPlease consider saving and reloading the project to perform a complete project check.",
                        filename);
                std::cerr << msg << std::endl;
                closedir(dp);
                return false;
            }
        }
        closedir(dp);
    }
    fileName.Assign(dir.GetFullPath(), value);
    return fileName.IsOk();
}

static bool makePath(const std::string &path) {

    mode_t mode = 0755;
    int ret = mkdir(path.c_str(), mode);
    if (ret == 0)
        return true;

    switch (errno) {
        case ENOENT:
            // parent didn't exist, try to create it
        {
            auto pos = path.find_last_of('/');
            if (pos == std::string::npos) {
                return false;
            }
            if (!makePath(path.substr(0, pos))) {
                return false;
            }

            // now, try to create again
            if (isDirExist(path)) {
                return true;
            } else {
                return 0 == mkdir(path.c_str(), mode);
            }
        }
        case EEXIST:
            // done!
            return isDirExist(path);

        default:
            return false;
    }
}

wxFileNameWrapper DirManager::MakeBlockFilePath(const std::string &value) {

    wxFileNameWrapper dir;
    dir.AssignDir(GetDataFilesDir());

    if (value.at(0) == 'd') {
        // this file is located in a subdirectory tree
        std::size_t location = value.find('b');
        std::string subdir = value.substr(0, location);
        dir.AppendDir(subdir);

        if (!isDirExist(dir.GetFullPath()))
            makePath(dir.GetFullPath());
    }

    if (value.at(0) == 'e') {
        // this file is located in a NEW style two-deep subdirectory tree
        std::string topdir = value.substr(0, 3);
        std::string middir = "d";
        middir += value.substr(3, 2);

        dir.AppendDir(topdir);
        dir.AppendDir(middir);

        if (!isDirExist(dir.GetFullPath()) && !makePath(dir.GetFullPath())) {
            std::cerr << "mkdir in DirManager::MakeBlockFilePath failed." << std::endl;
        }
    }
    return dir;
}

bool DirManager::CopyFile(
        const std::string &file1, const std::string &file2) {
    std::ifstream src(file1.c_str(), std::ifstream::binary);
    std::ofstream dst(file2.c_str(), std::ios::binary);
    dst << src.rdbuf();
    return true;
}

// Adds one to the reference count of the block file,
// UNLESS it is "locked", then it makes a NEW copy of
// the BlockFile.
// This function returns non-NULL, or else throws
BlockFilePtr DirManager::CopyBlockFile(const BlockFilePtr &b) {
    if (!b)
        THROW_INCONSISTENCY_EXCEPTION;

    auto result = b->GetFileName();
    const auto &fn = result.name;

    if (!b->IsLocked()) {
        //mchinen:July 13 2009 - not sure about this, but it needs to be added to the hash to be able to save if not locked.
        //note that this shouldn't hurt mBlockFileHash's that already contain the filename, since it should just overwrite.
        //but it's something to watch out for.
        //
        // LLL: Except for silent block files which have uninitialized filename.
        if (fn.IsOk())
            mBlockFileHash[fn.GetName()] = b;
        return b;
    }

    // Copy the blockfile
    BlockFilePtr b2;
    if (!fn.IsOk())
        // Block files with uninitialized filename (i.e. SilentBlockFile)
        // just need an in-memory copy.
        b2 = b->Copy(wxFileNameWrapper{});
    else {
        wxFileNameWrapper newFile{MakeBlockFileName()};
        const std::string newName{newFile.GetName()};
        const std::string newPath{newFile.GetFullPath()};

        // We assume that the NEW file should have the same extension
        // as the existing file
        newFile.SetExt(fn.GetExt());

        //some block files such as ODPCMAliasBlockFIle don't always have
        //a summary file, so we should check before we copy.
        if (b->IsSummaryAvailable()) {
            if (!CopyFile(fn.GetFullPath(),
                          newFile.GetFullPath()))
                // Disk space exhaustion, maybe
                throw FileException{
                        FileException::Cause::Write, newFile};
        }

        b2 = b->Copy(std::move(newFile));

        mBlockFileHash[newName] = b2;
        aliasList.push_back(newPath);
    }

    if (!b2)
        THROW_INCONSISTENCY_EXCEPTION;

    return b2;
}

std::string DirManager::GetDataFilesDir() const {
    return projFull != "" ? projFull : mytemp;
}

auto DirManager::GetBalanceInfo() -> BalanceInfo & {
    // Before returning the map,
    // see whether any block files have disappeared,
    // and if so update

    auto count = BlockFile::gBlockFileDestructionCount;
    if (mLastBlockFileDestructionCount != count) {
        auto it = mBlockFileHash.begin(), end = mBlockFileHash.end();
        while (it != end) {
            BlockFilePtr ptr{it->second.lock()};
            if (!ptr) {
                auto name = it->first;
                mBlockFileHash.erase(it++);
                BalanceInfoDel(name);
            } else
                ++it;
        }
    }

    mLastBlockFileDestructionCount = count;

    return mBalanceInfo;
}

static inline unsigned int hexchar_to_int(unsigned int x) {
    if (x < 48U)return 0;
    if (x < 58U)return x - 48U;
    if (x < 65U)return 10U;
    if (x < 71U)return x - 55U;
    if (x < 97U)return 10U;
    if (x < 103U)return x - 87U;
    return 15U;
}

// Note that this will try to clean up directories out from under even
// locked blockfiles; this is actually harmless as the rmdir will fail
// on non-empty directories.
void DirManager::BalanceInfoDel(const std::string &file) {
    // do not use GetBalanceInfo(),
    // rather this function will be called from there.
    auto &balanceInfo = mBalanceInfo;
    auto &dirMidPool = balanceInfo.dirMidPool;
    auto &dirMidFull = balanceInfo.dirMidFull;
    auto &dirTopPool = balanceInfo.dirTopPool;
    auto &dirTopFull = balanceInfo.dirTopFull;

    const char *s = file.c_str();
    if (s[0] == 'e') {
        // this is one of the modern two-deep managed files

        unsigned int topnum = (hexchar_to_int(s[1]) << 4) |
                              hexchar_to_int(s[2]);
        unsigned int midnum = (hexchar_to_int(s[3]) << 4) |
                              hexchar_to_int(s[4]);
        unsigned int midkey = topnum << 8 | midnum;

        // look for midkey in the mid pool
        if (dirMidFull.find(midkey) != dirMidFull.end()) {
            // in the full pool

            if (--dirMidFull[midkey] < 256) {
                // move out of full into available
                dirMidPool[midkey] = dirMidFull[midkey];
                dirMidFull.erase(midkey);
            }
        } else {
            if (--dirMidPool[midkey] < 1) {
                // erasing the key here is OK; we have provision to add it
                // back if its needed (unlike the dirTopPool hash)
                dirMidPool.erase(midkey);

                // DELETE the actual directory
                std::string dir = (projFull != "" ? projFull : mytemp);
                dir += "/";
                dir += file.substr(0, 3);
                dir += "/";
                dir += "d";
                dir += file.substr(3, 2);
                remove_directory(dir.c_str());

                // also need to remove from toplevel
                if (dirTopFull.find(topnum) != dirTopFull.end()) {
                    // in the full pool
                    if (--dirTopFull[topnum] < 256) {
                        // move out of full into available
                        dirTopPool[topnum] = dirTopFull[topnum];
                        dirTopFull.erase(topnum);
                    }
                } else {
                    if (--dirTopPool[topnum] < 1) {
                        // do *not* erase the hash entry from dirTopPool
                        // *do* DELETE the actual directory
                        std::string dir = (projFull != "" ? projFull : mytemp);
                        dir += "/";
                        dir += file.substr(0, 3);
                        remove_directory(dir.c_str());
                    }
                }
            }
        }
    }
}
