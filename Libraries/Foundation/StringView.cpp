// Copyright (c) 2022, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#include "StringView.h"
#include "StringUtility.h"

bool SC::StringView::parseInt32(int32_t* value) const
{
    if (text::isIntegerNumber(text))
    {
        if (hasNullTerm)
        {
            *value = atoi(text.data);
        }
        else
        {
            char_t buffer[12]; // 10 digits + sign + nullterm
            memcpy(buffer, text.data, text.size);
            buffer[text.size] = 0;

            *value = atoi(buffer);
        }
        return true;
    }
    else
    {
        return false;
    }
}