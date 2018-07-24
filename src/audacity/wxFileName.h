/////////////////////////////////////////////////////////////////////////////
// Name:        wx/filename.h
// Purpose:     wxFileName - encapsulates a file path
// Author:      Robert Roebling, Vadim Zeitlin
// Modified by:
// Created:     28.12.00
// Copyright:   (c) 2000 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef   _WX_FILENAME_H_
#define   _WX_FILENAME_H_

#include <string>
#include <vector>

#if wxUSE_FILE
class WXDLLIMPEXP_FWD_BASE wxFile;
#endif

#if wxUSE_FFILE
class WXDLLIMPEXP_FWD_BASE wxFFile;
#endif

// this symbol is defined for the platforms where file systems use volumes in
// paths
#if defined(__WINDOWS__)
#define wxHAS_FILESYSTEM_VOLUMES
#endif

// the various values for the path format: this mainly affects the path
// separator but also whether or not the path has the drive part (as under
// Windows)
enum wxPathFormat {
    wxPATH_NATIVE = 0,      // the path format for the current platform
    wxPATH_UNIX,
    wxPATH_BEOS = wxPATH_UNIX,
    wxPATH_MAC,
    wxPATH_DOS,
    wxPATH_WIN = wxPATH_DOS,
    wxPATH_OS2 = wxPATH_DOS,
    wxPATH_VMS,

    wxPATH_MAX // Not a valid value for specifying path format
};

// what exactly should GetPath() return?
enum {
    wxPATH_NO_SEPARATOR = 0x0000,  // for symmetry with wxPATH_GET_SEPARATOR
    wxPATH_GET_VOLUME = 0x0001,  // include the volume if applicable
    wxPATH_GET_SEPARATOR = 0x0002   // terminate the path with the separator
};

// between drive/volume name and the path
#define wxFILE_SEP_DSK        ':'

// between file name and extension
#define wxFILE_SEP_EXT        '.'

// between the path components
#define wxFILE_SEP_PATH_DOS   '\\'
#define wxFILE_SEP_PATH_UNIX  '/'
#define wxFILE_SEP_PATH_MAC   ':'
#define wxFILE_SEP_PATH_VMS   '.' // VMS also uses '[' and ']'

class wxFileName {
public:
    // constructors and assignment

    // the usual stuff
    wxFileName() { Clear(); }

    wxFileName(const wxFileName &filepath) { Assign(filepath); }

    // reset all components to default, uninitialized state
    void Clear();

    // Replace current path with this one
    void SetPath( const std::string &path, wxPathFormat format = wxPATH_NATIVE );

    void Assign(const wxFileName &filepath);


    void Assign(const std::string &fullpath,
                wxPathFormat format = wxPATH_NATIVE);
    void Assign(const std::string& path,
                const std::string& name,
                wxPathFormat format = wxPATH_NATIVE);
    
    void Assign(const std::string& volume,
                const std::string& path,
                const std::string& name,
                const std::string& ext,
                bool hasExt,
                wxPathFormat format = wxPATH_NATIVE);

    bool AppendDir(const std::string& dir);
    const std::vector<std::string> &GetDirs() const { return m_dirs; }

    // Other accessors
    void SetExt(const std::string &ext) {
        m_ext = ext;
        m_hasExt = !m_ext.empty();
    }

    void ClearExt() {
        m_ext.clear();
        m_hasExt = false;
    }

    void SetEmptyExt() {
        m_ext.clear();
        m_hasExt = true;
    }

    std::string GetExt() const { return m_ext; }

    bool HasExt() const { return m_hasExt; }

    void SetName(const std::string &name) { m_name = name; }

    std::string GetName() const { return m_name; }

    bool HasName() const { return !m_name.empty(); }

    void SetVolume(const std::string &volume) { m_volume = volume; }

    std::string GetVolume() const { return m_volume; }

    bool HasVolume() const { return !m_volume.empty(); }

    // full name is the file name + extension (but without the path)
    void SetFullName(const std::string &fullname);

    // split a fullpath into the volume, path, (base) name and extension
    // (all of the pointers can be NULL)
    static void SplitPath(const std::string &fullpath,
                          std::string *volume,
                          std::string *path,
                          std::string *name,
                          std::string *ext,
                          bool *hasExt = NULL,
                          wxPathFormat format = wxPATH_NATIVE);

    static void SplitPath(const std::string &fullpath,
                          std::string *volume,
                          std::string *path,
                          std::string *name,
                          std::string *ext,
                          wxPathFormat format) {
        SplitPath(fullpath, volume, path, name, ext, NULL, format);
    }

    // compatibility version: volume is part of path
    static void SplitPath(const std::string &fullpath,
                          std::string *path,
                          std::string *name,
                          std::string *ext,
                          wxPathFormat format = wxPATH_NATIVE);


    // get the canonical path format for this platform
    static wxPathFormat GetFormat(wxPathFormat format = wxPATH_NATIVE);

    // split a path into volume and pure path part
    static void SplitVolume(const std::string &fullpathWithVolume,
                            std::string *volume,
                            std::string *path,
                            wxPathFormat format = wxPATH_NATIVE);

    // get the string of path terminators, i.e. characters which terminate the
    // path
    static std::string GetPathTerminators(wxPathFormat format = wxPATH_NATIVE);

    // get the string of path separators for this format
    static std::string GetPathSeparators(wxPathFormat format = wxPATH_NATIVE);

    // is the char a path separator for this format?
    static bool IsPathSeparator(char ch, wxPathFormat format = wxPATH_NATIVE);

    // Dir accessors
    size_t GetDirCount() const { return m_dirs.size(); }

    void RemoveDir(size_t pos);

    void RemoveLastDir() { RemoveDir(GetDirCount() - 1); }

    std::string GetFullName() const;

    // flags are combination of wxPATH_GET_XXX flags
    std::string GetPath(int flags = wxPATH_GET_VOLUME,
                        wxPathFormat format = wxPATH_NATIVE) const;

    // Construct full path with name and ext
    std::string GetFullPath(wxPathFormat format = wxPATH_NATIVE) const;


    // get the canonical path separator for this format
    static char GetPathSeparator(wxPathFormat format = wxPATH_NATIVE) { return GetPathSeparators(format)[0u]; }
    
    
    void AssignDir(const std::string& dir, wxPathFormat format = wxPATH_NATIVE);

    // file tests

    // is the filename valid at all?
    bool IsOk() const {
        // we're fine if we have the path or the name or if we're a root dir
        return m_dirs.size() != 0 || !m_name.empty() || !m_relative ||
               !m_ext.empty() || m_hasExt;
    }

private:
    // get the string separating the volume from the path for this format,
    // return an empty string if this format doesn't support the notion of
    // volumes at all
    static std::string GetVolumeSeparator(wxPathFormat format = wxPATH_NATIVE);

    // check whether this dir is valid for Append/Prepend/InsertDir()
    static bool IsValidDirComponent(const std::string& dir);
    // the drive/volume/device specification (always empty for Unix)
    std::string m_volume;

    // the path components of the file
    std::vector<std::string> m_dirs;

    // the file name and extension (empty for directories)
    std::string m_name,
            m_ext;

    // when m_dirs is empty it may mean either that we have no path at all or
    // that our path is '/', i.e. the root directory
    //
    // we use m_relative to distinguish between these two cases, it will be
    // true in the former and false in the latter
    //
    // NB: the path is not absolute just because m_relative is false, it still
    //     needs the drive (i.e. volume) in some formats (Windows)
    bool m_relative;

    // when m_ext is empty, it may be because we don't have any extension or
    // because we have an empty extension
    //
    // the difference is important as file with name "foo" and without
    // extension has full name "foo" while with empty extension it is "foo."
    bool m_hasExt;

    // by default, symlinks are dereferenced but this flag can be set with
    // DontFollowLink() to change this and make different operations work on
    // this file path itself instead of the target of the symlink
    bool m_dontFollowLinks;
};


#endif
