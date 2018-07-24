/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/filename.cpp
// Purpose:     wxFileName - encapsulates a file path
// Author:      Robert Roebling, Vadim Zeitlin
// Modified by:
// Created:     28.12.2000
// Copyright:   (c) 2000 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////


#include <iostream>
#include <assert.h>
#include "wxFileName.h"
#include "wxTokenzr.h"

void wxFileName::Clear() {
    m_dirs.clear();
    m_volume.clear();
    m_name.clear();
    m_ext.clear();

    // we don't have any absolute path for now
    m_relative = true;

    // nor any extension
    m_hasExt = false;

    // follow symlinks by default
    m_dontFollowLinks = false;
}

void wxFileName::Assign(const std::string &fullpath,
                        wxPathFormat format) {
    std::string volume, path, name, ext;
    bool hasExt;
    SplitPath(fullpath, &volume, &path, &name, &ext, &hasExt, format);

    Assign(volume, path, name, ext, hasExt, format);
}

void wxFileName::Assign(const std::string &fullpathOrig,
                        const std::string &fullname,
                        wxPathFormat format) {
    // always recognize fullpath as directory, even if it doesn't end with a
    // slash
    std::string fullpath = fullpathOrig;
    if (!fullpath.empty() && !(fullpath.substr(fullpath.length() - 1, fullpath.length()) == "/")) {
        fullpath += GetPathSeparator(format);
    }

    std::string volume, path, name, ext;
    bool hasExt;

    // do some consistency checks: the name should be really just the filename
    // and the path should be really just a path
    std::string volDummy, pathDummy, nameDummy, extDummy;

    SplitPath(fullname, &volDummy, &pathDummy, &name, &ext, &hasExt, format);

    assert(volDummy.empty() && pathDummy.empty());

    SplitPath(fullpath, &volume, &path, &nameDummy, &extDummy, format);

#ifndef __VMS
    // This test makes no sense on an OpenVMS system.
    assert(nameDummy.empty() && extDummy.empty());
#endif
    Assign(volume, path, name, ext, hasExt, format);
}

void wxFileName::SetPath(const std::string &pathOrig, wxPathFormat format) {
    m_dirs.clear();

    if (pathOrig.empty()) {
        // no path at all
        m_relative = true;

        return;
    }

    format = GetFormat(format);

    // 0) deal with possible volume part first
    std::string volume,
            path;
    SplitVolume(pathOrig, &volume, &path, format);
    if (!volume.empty()) {
        m_relative = false;

        SetVolume(volume);
    }

    // 1) Determine if the path is relative or absolute.

    if (path.empty()) {
        // we had only the volume
        return;
    }

    char leadingChar = path[0u];

    switch (format) {
        case wxPATH_MAC:
            m_relative = leadingChar == ':';

            // We then remove a leading ":". The reason is in our
            // storage form for relative paths:
            // ":dir:file.txt" actually means "./dir/file.txt" in
            // DOS notation and should get stored as
            // (relative) (dir) (file.txt)
            // "::dir:file.txt" actually means "../dir/file.txt"
            // stored as (relative) (..) (dir) (file.txt)
            // This is important only for the Mac as an empty dir
            // actually means <UP>, whereas under DOS, double
            // slashes can be ignored: "\\\\" is the same as "\\".
            if (m_relative)
                path.erase(0, 1);
            break;

        case wxPATH_VMS:
            // TODO: what is the relative path format here?
            m_relative = false;
            break;

        default:
            std::cerr << "Unknown path format" << std::endl;
            // falls through
        case wxPATH_UNIX:
            m_relative = leadingChar != '/';
            break;

        case wxPATH_DOS:
            m_relative = !IsPathSeparator(leadingChar, format);
            break;

    }

    // 2) Break up the path into its members. If the original path
    //    was just "/" or "\\", m_dirs will be empty. We know from
    //    the m_relative field, if this means "nothing" or "root dir".

    wxStringTokenizer tn(path, GetPathSeparators(format));

    while (tn.HasMoreTokens()) {
        std::string token = tn.GetNextToken();

        // Remove empty token under DOS and Unix, interpret them
        // as .. under Mac.
        if (token.empty()) {
            if (format == wxPATH_MAC)
                m_dirs.push_back("..");
            // else ignore
        } else {
            m_dirs.push_back(token);
        }
    }
}

// return true if the character is a DOS path separator i.e. either a slash or
// a backslash
inline bool IsDOSPathSep(char ch) {
    return ch == wxFILE_SEP_PATH_DOS || ch == wxFILE_SEP_PATH_UNIX;
}

// return true if the format used is the DOS/Windows one and the string looks
// like a UNC path
static bool IsUNCPath(const std::string &path, wxPathFormat format) {
    return format == wxPATH_DOS &&
           path.length() >= 4 && // "\\a" can't be a UNC path
           IsDOSPathSep(path[0u]) &&
           IsDOSPathSep(path[1u]) &&
           !IsDOSPathSep(path[2u]);
}

void wxFileName::Assign(const std::string &volume,
                        const std::string &path,
                        const std::string &name,
                        const std::string &ext,
                        bool hasExt,
                        wxPathFormat format) {
    // we should ignore paths which look like UNC shares because we already
    // have the volume here and the UNC notation (\\server\path) is only valid
    // for paths which don't start with a volume, so prevent SetPath() from
    // recognizing "\\foo\bar" in "c:\\foo\bar" as an UNC path
    //
    // note also that this is a rather ugly way to do what we want (passing
    // some kind of flag telling to ignore UNC paths to SetPath() would be
    // better) but this is the safest thing to do to avoid breaking backwards
    // compatibility in 2.8
    if (IsUNCPath(path, format)) {
        // remove one of the 2 leading backslashes to ensure that it's not
        // recognized as an UNC path by SetPath()
        std::string pathNonUNC(path, 1, std::string::npos);
        SetPath(pathNonUNC, format);
    } else // no UNC complications
    {
        SetPath(path, format);
    }

    m_volume = volume;
    m_ext = ext;
    m_name = name;

    m_hasExt = hasExt;
}

void wxFileName::Assign(const wxFileName &filepath) {
    m_volume = filepath.GetVolume();
    m_dirs = filepath.GetDirs();
    m_name = filepath.GetName();
    m_ext = filepath.GetExt();
    m_relative = filepath.m_relative;
    m_hasExt = filepath.m_hasExt;
    m_dontFollowLinks = filepath.m_dontFollowLinks;
}

void wxFileName::AssignDir(const std::string &dir, wxPathFormat format) {
    Assign(dir, std::string(""), format);
}

bool wxFileName::AppendDir(const std::string &dir) {
    if (!IsValidDirComponent(dir))
        return false;
    m_dirs.push_back(dir);
    return true;
}

void wxFileName::SetFullName(const std::string &fullname) {
    SplitPath(fullname, NULL /* no volume */, NULL /* no path */,
              &m_name, &m_ext, &m_hasExt);
}

/* static */
void wxFileName::SplitPath(const std::string &fullpathWithVolume,
                           std::string *pstrVolume,
                           std::string *pstrPath,
                           std::string *pstrName,
                           std::string *pstrExt,
                           bool *hasExt,
                           wxPathFormat format) {
    format = GetFormat(format);

    std::string fullpath;
    SplitVolume(fullpathWithVolume, pstrVolume, &fullpath, format);

    // find the positions of the last dot and last path separator in the path
    size_t posLastDot = fullpath.find_last_of(wxFILE_SEP_EXT);
    size_t posLastSlash = fullpath.find_last_of(GetPathTerminators(format));

    // check whether this dot occurs at the very beginning of a path component
    if ((posLastDot != std::string::npos) &&
        (posLastDot == 0 ||
         IsPathSeparator(fullpath[posLastDot - 1]) ||
         (format == wxPATH_VMS && fullpath[posLastDot - 1] == ']'))) {
        // dot may be (and commonly -- at least under Unix -- is) the first
        // character of the filename, don't treat the entire filename as
        // extension in this case
        posLastDot = std::string::npos;
    }

    // if we do have a dot and a slash, check that the dot is in the name part
    if ((posLastDot != std::string::npos) &&
        (posLastSlash != std::string::npos) &&
        (posLastDot < posLastSlash)) {
        // the dot is part of the path, not the start of the extension
        posLastDot = std::string::npos;
    }

    // now fill in the variables provided by user
    if (pstrPath) {
        if (posLastSlash == std::string::npos) {
            // no path at all
            pstrPath->clear();
        } else {
            // take everything up to the path separator but take care to make
            // the path equal to something like '/', not empty, for the files
            // immediately under root directory
            size_t len = posLastSlash;

            // this rule does not apply to mac since we do not start with colons (sep)
            // except for relative paths
            if (!len && format != wxPATH_MAC)
                len++;

            *pstrPath = fullpath.substr(0, len);

            // special VMS hack: remove the initial bracket
            if (format == wxPATH_VMS) {
                if ((*pstrPath)[0u] == '[')
                    pstrPath->erase(0, 1);
            }
        }
    }

    if (pstrName) {
        // take all characters starting from the one after the last slash and
        // up to, but excluding, the last dot
        size_t nStart = posLastSlash == std::string::npos ? 0 : posLastSlash + 1;
        size_t count;
        if (posLastDot == std::string::npos) {
            // take all until the end
            count = std::string::npos;
        } else if (posLastSlash == std::string::npos) {
            count = posLastDot;
        } else // have both dot and slash
        {
            count = posLastDot - posLastSlash - 1;
        }

        *pstrName = fullpath.substr(nStart, count);
    }

    // finally deal with the extension here: we have an added complication that
    // extension may be empty (but present) as in "foo." where trailing dot
    // indicates the empty extension at the end -- and hence we must remember
    // that we have it independently of pstrExt
    if (posLastDot == std::string::npos) {
        // no extension
        if (pstrExt)
            pstrExt->clear();
        if (hasExt)
            *hasExt = false;
    } else {
        // take everything after the dot
        if (pstrExt)
            *pstrExt = fullpath.substr(posLastDot + 1);
        if (hasExt)
            *hasExt = true;
    }
}

// return a string with the volume par
static std::string wxGetVolumeString(const std::string &volume, wxPathFormat format) {
    std::string path;
    return path;
}


/* static */
void wxFileName::SplitPath(const std::string &fullpath,
                           std::string *path,
                           std::string *name,
                           std::string *ext,
                           wxPathFormat format) {
    std::string volume;
    SplitPath(fullpath, &volume, path, name, ext, format);
}

wxPathFormat wxFileName::GetFormat(wxPathFormat format) {
    if (format == wxPATH_NATIVE) {
#if defined(__WINDOWS__)
        format = wxPATH_DOS;
#elif defined(__VMS)
        format = wxPATH_VMS;
#else
        format = wxPATH_UNIX;
#endif
    }
    return format;
}

/* static */
void
wxFileName::SplitVolume(const std::string &fullpathWithVolume,
                        std::string *pstrVolume,
                        std::string *pstrPath,
                        wxPathFormat format) {
    format = GetFormat(format);

    std::string fullpath = fullpathWithVolume;


    if (pstrPath)
        *pstrPath = fullpath;
}

/* static */
std::string wxFileName::GetPathTerminators(wxPathFormat format) {
    format = GetFormat(format);

    // under VMS the end of the path is ']', not the path separator used to
    // separate the components
    return format == wxPATH_VMS ? std::string("]") : GetPathSeparators(format);
}


/* static */
std::string wxFileName::GetPathSeparators(wxPathFormat format) {
    std::string seps;
    switch (GetFormat(format)) {
        default:
        case wxPATH_UNIX:
            seps = wxFILE_SEP_PATH_UNIX;
            break;

        case wxPATH_MAC:
            seps = wxFILE_SEP_PATH_MAC;
            break;

        case wxPATH_VMS:
            seps = wxFILE_SEP_PATH_VMS;
            break;
    }

    return seps;
}

/* static */
bool wxFileName::IsPathSeparator(char ch, wxPathFormat format) {
    // std::string::Find() doesn't work as expected with NUL - it will always find
    // it, so test for it separately
    return ch != '\0' && GetPathSeparators(format).find(ch) != std::string::npos;
}

void wxFileName::RemoveDir(size_t pos) {
    m_dirs.erase(m_dirs.begin() + pos);
}

std::string wxFileName::GetFullName() const {
    std::string fullname = m_name;
    if (m_hasExt) {
        fullname += wxFILE_SEP_EXT + m_ext;
    }

    return fullname;
}


std::string wxFileName::GetPath(int flags, wxPathFormat format) const {
    format = GetFormat(format);

    std::string fullpath;

    // return the volume with the path as well if requested
    if (flags & wxPATH_GET_VOLUME) {
        fullpath += wxGetVolumeString(GetVolume(), format);
    }

    // the leading character
    switch (format) {
        case wxPATH_MAC:
            if (m_relative)
                fullpath += wxFILE_SEP_PATH_MAC;
            break;

        case wxPATH_DOS:
            if (!m_relative)
                fullpath += wxFILE_SEP_PATH_DOS;
            break;

        default:
        case wxPATH_UNIX:
            if (!m_relative) {
                fullpath += wxFILE_SEP_PATH_UNIX;
            }
            break;

        case wxPATH_VMS:
            // no leading character here but use this place to unset
            // wxPATH_GET_SEPARATOR flag: under VMS it doesn't make sense
            // as, if I understand correctly, there should never be a dot
            // before the closing bracket
            flags &= ~wxPATH_GET_SEPARATOR;
    }

    if (m_dirs.empty()) {
        // there is nothing more
        return fullpath;
    }

    // then concatenate all the path components using the path separator
    if (format == wxPATH_VMS) {
        fullpath += '[';
    }

    const size_t dirCount = m_dirs.size();
    for (size_t i = 0; i < dirCount; i++) {
        switch (format) {
            case wxPATH_MAC:
                if (m_dirs[i] == ".") {
                    // skip appending ':', this shouldn't be done in this
                    // case as "::" is interpreted as ".." under Unix
                    continue;
                }

                // convert back from ".." to nothing
                if (!(m_dirs[i] == ".."))
                    fullpath += m_dirs[i];
                break;

            default:
            case wxPATH_DOS:
            case wxPATH_UNIX:
                fullpath += m_dirs[i];
                break;

            case wxPATH_VMS:
                // TODO: What to do with ".." under VMS

                // convert back from ".." to nothing
                if (!(m_dirs[i] == ".."))
                    fullpath += m_dirs[i];
                break;
        }

        if ((flags & wxPATH_GET_SEPARATOR) || (i != dirCount - 1))
            fullpath += GetPathSeparator(format);
    }

    if (format == wxPATH_VMS) {
        fullpath += ']';
    }

    return fullpath;
}

std::string wxFileName::GetFullPath(wxPathFormat format) const {
    // we already have a function to get the path
    std::string fullpath = GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR,
                                   format);

    // now just add the file name and extension to it
    fullpath += GetFullName();

    return fullpath;
}

/* static */
std::string wxFileName::GetVolumeSeparator(wxPathFormat format) {
    std::string sepVol;

    if ((GetFormat(format) == wxPATH_DOS) ||
        (GetFormat(format) == wxPATH_VMS)) {
        sepVol = wxFILE_SEP_DSK;
    }
    //else: leave empty

    return sepVol;
}

// ----------------------------------------------------------------------------
// path components manipulation
// ----------------------------------------------------------------------------

/* static */ bool wxFileName::IsValidDirComponent(const std::string &dir) {
    if (dir.empty()) {
        std::cerr << "empty directory passed to wxFileName::InsertDir()" << std::endl;
        return false;
    }

    const size_t len = dir.length();
    for (size_t n = 0; n < len; n++) {
        if (dir.substr(n, n + 1) == GetVolumeSeparator() || IsPathSeparator(dir[n])) {
            std::cerr << "invalid directory component in wxFileName" << std::endl;

            return false;
        }
    }

    return true;
}
