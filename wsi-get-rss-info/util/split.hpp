//-------------------------------------------------------------------------------------------------
//
// File: split.h  Author: Dennis Lang Desc: Split string into tokens   
//
//-------------------------------------------------------------------------------------------------
//
// Author: Dennis Lang - 2020
// http://landenlabs.com
//
// This file is part of llcsv project.
//
// ----- License ----
//
// Copyright (c) 2020 Dennis Lang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include <vector>
#include "lstring.hpp"

// Split string into parts.
class Split : public std::vector<lstring>
{
public:
    typedef size_t(*Find_of)(const lstring& str, const char* delimList, size_t begIdx);

    Split(const lstring& str, const char* delimList, Find_of find_of, unsigned skip=1)
    {
        size_t lastPos = 0;
        // size_t pos = str.find_first_of(delimList);
        size_t pos = (*find_of)(str, delimList, 0);

        while (pos != lstring::npos)
        {
            if (pos != lastPos)
                push_back(str.substr(lastPos, pos - lastPos));
            lastPos = pos + skip;
            // pos = str.find_first_of(delimList, lastPos);
            pos = (*find_of)(str, delimList, lastPos);
        }
        if (lastPos < str.length())
            push_back(str.substr(lastPos, pos - lastPos));
    }
    
    Split(const lstring& str, const char* delimList, int maxSplit=numeric_limits<int>::max())
    {
        size_t lastPos = 0;
        size_t pos = str.find_first_of(delimList);
        
        while (pos != lstring::npos && --maxSplit > 0)
        {
            if (pos != lastPos)
                push_back(str.substr(lastPos, pos - lastPos));
            lastPos = pos + 1;
            pos = str.find_first_of(delimList, lastPos);
        }
        if (lastPos < str.length())
            push_back(str.substr(lastPos, (maxSplit == 0) ? str.length() : pos - lastPos));
    }

};

