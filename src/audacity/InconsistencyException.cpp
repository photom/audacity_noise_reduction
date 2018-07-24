//
//  InconsistencyException.cpp
//  
//
//  Created by Paul Licameli on 11/27/16.
//
//

#include <memory>
#include <iostream>
#include <string>
#include <cstdio>

#include "Audacity.h"
#include "InconsistencyException.h"
#include "Utils.h"

InconsistencyException::~InconsistencyException() {
}

std::string InconsistencyException::ErrorMessage() const {

#ifdef __func__
    return string_format("Internal error in %s line %d.\n", func, line);
#else
    return "Internal error.";
#endif

}
