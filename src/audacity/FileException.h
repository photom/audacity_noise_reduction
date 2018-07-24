//
//  FileException.h
//
//
//  Created by Paul Licameli on 11/22/16.
//
//

#ifndef __AUDACITY_FILE_EXCEPTION__
#define __AUDACITY_FILE_EXCEPTION__

#include <wxFileName.h>

class FileException {
public:
    enum class Cause {
        Open, Read, Write, Rename
    };

    explicit FileException
            (Cause cause_, const wxFileName &fileName_,
             const std::string &caption = "File Error",
             const wxFileName &renameTarget_ = {})
            : cause{cause_}, fileName{fileName_}, renameTarget{renameTarget_} {}

    FileException(FileException &&that)
            :
            cause{that.cause}, fileName{that.fileName}, renameTarget{that.renameTarget} {}

    FileException &operator=(FileException &&) = delete;

    ~FileException();

protected:
    // Format a default, internationalized error message for this exception.
    std::string ErrorMessage() const;

public:
    Cause cause;
    wxFileName fileName;
    wxFileName renameTarget;
};


#endif //PROJECT_FILEEXCEPTION_H
