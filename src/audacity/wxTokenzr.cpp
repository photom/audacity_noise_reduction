//
// Created by sthin on 18/07/20.
//

#include "wxTokenzr.h"
/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/tokenzr.cpp
// Purpose:     String tokenizer
// Author:      Guilhem Lavaux
// Modified by: Vadim Zeitlin (almost full rewrite)
// Created:     04/22/98
// Copyright:   (c) Guilhem Lavaux
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#include "wxTokenzr.h"

// Required for wxIs... functions
#include <ctype.h>
#include <cassert>
#include <iostream>
#include <cstring>

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

static std::string::const_iterator
find_first_of(const char *delims, size_t len,
              const std::string::const_iterator &from,
              const std::string::const_iterator &end) {
    assert(from <= end);

    for (std::string::const_iterator i = from; i != end; ++i) {
        if (memchr(delims, *i, len))
            return i;
    }

    return end;
}

static std::string::const_iterator
find_first_not_of(const char *delims, size_t len,
                  const std::string::const_iterator &from,
                  const std::string::const_iterator &end) {
    assert(from <= end);

    for (std::string::const_iterator i = from; i != end; ++i) {
        if (!memchr(delims, *i, len))
            return i;
    }

    return end;
}

// ----------------------------------------------------------------------------
// wxStringTokenizer construction
// ----------------------------------------------------------------------------

wxStringTokenizer::wxStringTokenizer(const std::string &str,
                                     const std::string &delims,
                                     wxStringTokenizerMode mode) {
    SetString(str, delims, mode);
}

wxStringTokenizer::wxStringTokenizer(const wxStringTokenizer &src) {
    DoCopyFrom(src);
}

wxStringTokenizer &wxStringTokenizer::operator=(const wxStringTokenizer &src) {
    if (this != &src) {
        DoCopyFrom(src);
    }

    return *this;
}

void wxStringTokenizer::SetString(const std::string &str,
                                  const std::string &delims,
                                  wxStringTokenizerMode mode) {
    if (mode == wxTOKEN_DEFAULT) {
        // by default, we behave like strtok() if the delimiters are only
        // whitespace characters and as wxTOKEN_RET_EMPTY otherwise (for
        // whitespace delimiters, strtok() behaviour is better because we want
        // to count consecutive spaces as one delimiter)
        std::string::const_iterator p;
        for (p = delims.begin(); p != delims.end(); ++p) {
            if (!isspace(*p))
                break;
        }

        if (p != delims.end()) {
            // not whitespace char in delims
            mode = wxTOKEN_RET_EMPTY;
        } else {
            // only whitespaces
            mode = wxTOKEN_STRTOK;
        }
    }

#if wxUSE_UNICODE // FIXME-UTF8: only wc_str()
    m_delims = delims.wc_str();
#else
    m_delims = delims;
#endif
    m_delimsLen = delims.length();

    m_mode = mode;

    Reinit(str);
}

void wxStringTokenizer::Reinit(const std::string &str) {
    assert(IsOk());

    m_string = str;
    m_stringEnd = m_string.end();
    m_pos = m_string.begin();
    m_lastDelim = '\0';
    m_hasMoreTokens = MoreTokens_Unknown;
}

void wxStringTokenizer::DoCopyFrom(const wxStringTokenizer &src) {
    m_string = src.m_string;
    m_stringEnd = m_string.end();
    m_pos = m_string.begin() + (src.m_pos - src.m_string.begin());
    m_delims = src.m_delims;
    m_delimsLen = src.m_delimsLen;
    m_mode = src.m_mode;
    m_lastDelim = src.m_lastDelim;
    m_hasMoreTokens = src.m_hasMoreTokens;
}

// ----------------------------------------------------------------------------
// access to the tokens
// ----------------------------------------------------------------------------

// do we have more of them?
bool wxStringTokenizer::HasMoreTokens() {
    // GetNextToken() calls HasMoreTokens() and so HasMoreTokens() is called
    // twice in every interation in the following common usage patten:
    //     while ( HasMoreTokens() )
    //        GetNextToken();
    // We optimize this case by caching HasMoreTokens() return value here:
    if (m_hasMoreTokens == MoreTokens_Unknown) {
        bool r = DoHasMoreTokens();
        this->m_hasMoreTokens =
                r ? MoreTokens_Yes : MoreTokens_No;
        return r;
    } else
        return m_hasMoreTokens == MoreTokens_Yes;
}

bool wxStringTokenizer::DoHasMoreTokens() const {
    assert(IsOk());

    if (find_first_not_of(m_delims.c_str(), m_delimsLen, m_pos, m_stringEnd)
        != m_stringEnd) {
        // there are non delimiter characters left, so we do have more tokens
        return true;
    }

    switch (m_mode) {
        case wxTOKEN_RET_EMPTY:
        case wxTOKEN_RET_DELIMS:
            // special hack for wxTOKEN_RET_EMPTY: we should return the initial
            // empty token even if there are only delimiters after it
            return !m_string.empty() && m_pos == m_string.begin();

        case wxTOKEN_RET_EMPTY_ALL:
            // special hack for wxTOKEN_RET_EMPTY_ALL: we can know if we had
            // already returned the trailing empty token after the last
            // delimiter by examining m_lastDelim: it is set to NUL if we run
            // up to the end of the string in GetNextToken(), but if it is not
            // NUL yet we still have this last token to return even if m_pos is
            // already at m_string.length()
            return m_pos < m_stringEnd || m_lastDelim != '\0';

        case wxTOKEN_INVALID:
        case wxTOKEN_DEFAULT:
            std::cerr << "unexpected tokenizer mode" << std::endl;


        case wxTOKEN_STRTOK:
            // never return empty delimiters
            break;
    }

    return false;
}

// count the number of (remaining) tokens in the string
size_t wxStringTokenizer::CountTokens() const {
    assert(IsOk());
    // VZ: this function is IMHO not very useful, so it's probably not very
    //     important if its implementation here is not as efficient as it
    //     could be -- but OTOH like this we're sure to get the correct answer
    //     in all modes
    wxStringTokenizer tkz(std::string(m_pos, m_stringEnd), m_delims, m_mode);

    size_t count = 0;
    while (tkz.HasMoreTokens()) {
        count++;

        (void) tkz.GetNextToken();
    }

    return count;
}

// ----------------------------------------------------------------------------
// token extraction
// ----------------------------------------------------------------------------

std::string wxStringTokenizer::GetNextToken() {
    std::string token;
    do {
        if (!HasMoreTokens()) {
            break;
        }

        m_hasMoreTokens = MoreTokens_Unknown;

        // find the end of this token
        std::string::const_iterator pos =
                find_first_of(m_delims.c_str(), m_delimsLen, m_pos, m_stringEnd);

        // and the start of the next one
        if (pos == m_stringEnd) {
            // no more delimiters, the token is everything till the end of
            // string
            token.assign(m_pos, m_stringEnd);

            // skip the token
            m_pos = m_stringEnd;

            // it wasn't terminated
            m_lastDelim = '\0';
        } else // we found a delimiter at pos
        {
            // in wxTOKEN_RET_DELIMS mode we return the delimiter character
            // with token, otherwise leave it out
            std::string::const_iterator tokenEnd(pos);
            if (m_mode == wxTOKEN_RET_DELIMS)
                ++tokenEnd;

            token.assign(m_pos, tokenEnd);

            // skip the token and the trailing delimiter
            m_pos = pos + 1;

            m_lastDelim = (pos == m_stringEnd) ? '\0' : (char) *pos;
        }
    } while (!AllowEmpty() && token.empty());

    return token;
}

// ----------------------------------------------------------------------------
// public functions
// ----------------------------------------------------------------------------

std::vector<std::string> wxStringTokenize(const std::string &str,
                                          const std::string &delims,
                                          wxStringTokenizerMode mode) {
    std::vector<std::string> tokens;
    wxStringTokenizer tk(str, delims, mode);
    while (tk.HasMoreTokens()) {
        tokens.push_back(tk.GetNextToken());
    }

    return tokens;
}