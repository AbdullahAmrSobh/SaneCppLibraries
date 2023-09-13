// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#include "HttpParser.h"

// https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html
#if SC_MSVC
#define SC_CAT(x, y)  SC_CAT2(x, y)
#define SC_CAT2(x, y) x##y
// https://developercommunity.visualstudio.com/t/-line-cannot-be-used-as-an-argument-for-constexpr/195665
//  __LINE__ cannot be used as a constexpr expression when edit and continue is enabled
#define SC_LINE int(SC_CAT(__LINE__, u))
#else
#define SC_LINE __LINE__
#endif

#define SC_CO_BEGIN(state)                                                                                             \
    switch (state)                                                                                                     \
    {                                                                                                                  \
    case 0:
#define SC_CO_RETURN(state, x)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        state = SC_LINE;                                                                                               \
        return x;                                                                                                      \
    case SC_LINE:;                                                                                                     \
    } while (0)
#define SC_CO_FINISH(state)                                                                                            \
    state = -1;                                                                                                        \
    }
#define crReset(state) state = 0;

SC::ReturnCode SC::HttpParser::parse(Span<const char> data, size_t& readBytes, Span<const char>& parsedData)
{
    if (state == State::Finished)
    {
        return false;
    }
    readBytes = 0;
    if (type == Type::Request)
    {
        if (result == Result::HeadersEnd)
        {
            if (state == State::Result)
            {
                state = State::Finished;
                return true;
            }
        }
    }
    else
    {
        if (result == Result::Body)
        {
            if (state == State::Result)
            {
                state = State::Finished;
                return true;
            }
        }
    }

    if (data.sizeInBytes() == 0)
    {
        return false;
    }

    SC_CO_BEGIN(topLevelCoroutine);
    if (type == Type::Request)
    {
        //------------------------
        // Parse Method
        //------------------------
        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseMethod, Result::Method>(data, readBytes, parsedData)));
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);
        //------------------------
        // Parse URL
        //------------------------
        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        matchIndex   = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseUrl, Result::Url>(data, readBytes, parsedData)));
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);
        //------------------------
        // Parse Version
        //------------------------
        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        matchIndex   = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseVersion<false>, Result::Version>(data, readBytes, parsedData)));
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);
    }
    else
    {
        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        matchIndex   = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseVersion<true>, Result::Version>(data, readBytes, parsedData)));
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);

        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        matchIndex   = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseStatusCode, Result::StatusCode>(data, readBytes, parsedData)));
            statusCode = static_cast<uint32_t>(number);
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);
        //------------------------
        // Parse name
        //------------------------
        globalStart += globalLength;
        tokenStart   = globalStart;
        tokenLength  = 0;
        globalLength = 0;
        matchIndex   = 0;
        do
        {
            SC_TRY_IF((process<&HttpParser::parseHeaderValue, Result::StatusString>(data, readBytes, parsedData)));
            SC_CO_RETURN(topLevelCoroutine, true);
        } while (state == State::Parsing);
    }
    //------------------------
    // Parse Headers
    //------------------------
    while (true)
    {
        if (data.data()[0] == '\r')
        {
            //------------------------
            // Parse End of Headers
            //------------------------
            globalStart += globalLength;
            tokenStart   = globalStart;
            tokenLength  = 0;
            globalLength = 0;
            matchIndex   = 0;
            do
            {
                SC_TRY_IF((process<&HttpParser::parseHeadersEnd, Result::HeadersEnd>(data, readBytes, parsedData)));
                SC_CO_RETURN(topLevelCoroutine, true);
            } while (state == State::Parsing);
            if (type == Type::Request)
            {
                break;
            }
            else
            {
                globalStart += globalLength;
                tokenStart   = globalStart;
                tokenLength  = 0;
                globalLength = 0;
                matchIndex   = 0;
                //------------------------
                // Parse Body
                //------------------------
                state  = State::Parsing;
                result = Result::Body;
                do
                {
                    {
                        auto oldLength = tokenLength;
                        tokenLength += data.sizeInBytes();
                        if (tokenLength > contentLength)
                        {
                            tokenLength = contentLength;
                        }
                        readBytes = tokenLength - oldLength;
                    }
                    SC_TRY_IF(data.sliceStartLength(0, readBytes, parsedData));
                    if (tokenLength == contentLength)
                    {
                        state = State::Result;
                        break;
                    }
                    else
                    {
                        SC_CO_RETURN(topLevelCoroutine, true);
                    }
                } while (true);
                break;
            }
        }
        else
        {
            //------------------------
            // Parse Header Name
            //------------------------
            globalStart += globalLength;
            tokenStart   = globalStart;
            tokenLength  = 0;
            globalLength = 0;
            matchIndex   = 0;
            do
            {
                SC_TRY_IF((process<&HttpParser::parseHeaderName, Result::HeaderName>(data, readBytes, parsedData)));
                SC_CO_RETURN(topLevelCoroutine, true);
            } while (state == State::Parsing);

            if (matchesHeader(HeaderType::ContentLength) and not parsedcontentLength)
            {

                parsedcontentLength = true;

                globalStart += globalLength;
                tokenStart   = globalStart;
                tokenLength  = 0;
                globalLength = 0;
                matchIndex   = 0;
                do
                {
                    SC_TRY_IF(
                        (process<&HttpParser::parseNumberValue, Result::HeaderValue>(data, readBytes, parsedData)));
                    contentLength = static_cast<uint32_t>(number);
                    SC_CO_RETURN(topLevelCoroutine, true);
                } while (state == State::Parsing);
            }
            else
            {
                //------------------------
                // Parse Header Value
                //------------------------
                globalStart += globalLength;
                tokenStart   = globalStart;
                tokenLength  = 0;
                globalLength = 0;
                matchIndex   = 0;
                do
                {
                    SC_TRY_IF(
                        (process<&HttpParser::parseHeaderValue, Result::HeaderValue>(data, readBytes, parsedData)));
                    SC_CO_RETURN(topLevelCoroutine, true);
                } while (state == State::Parsing);
            }
        }
    }
    SC_CO_FINISH(topLevelCoroutine);
    return true;
}

bool SC::HttpParser::matchesHeader(HeaderType headerName) const
{
    auto headerIndex = static_cast<int>(headerName);
    if (headerIndex > 0)
        return false;
    return matchingHeaderValid[headerIndex];
}

template <bool (SC::HttpParser::*Func)(char), SC::HttpParser::Result currentResult>
SC::ReturnCode SC::HttpParser::process(Span<const char>& data, size_t& readBytes, Span<const char>& parsedData)
{
    result                   = currentResult;
    state                    = State::Parsing;
    const auto initialStart  = tokenStart;
    const auto initialLength = tokenLength;
    readBytes                = 0;
    for (auto c : data)
    {
        tokenLength++;
        SC_TRY_IF((this->*Func)(c)); // can modify start or length with spaces
        readBytes++;
        if (state == State::Result)
        {
            break;
        }
    }
    globalLength += readBytes;
    const auto startDelta  = tokenStart - initialStart;
    const auto lengthDelta = tokenLength - initialLength;
    if (state == State::Result)
    {
        SC_TRY_IF(data.sliceStartLength(startDelta, lengthDelta, parsedData));
        SC_TRY_IF(data.sliceStart(readBytes, data));
        crReset(nestedParserCoroutine);
    }
    else
    {
        SC_TRY_IF(data.sliceStartLength(startDelta, lengthDelta, parsedData));
    }
    return true;
}

bool SC::HttpParser::parseMethod(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    if (currentChar == 'G' or currentChar == 'g')
    {
        SC_CO_RETURN(nestedParserCoroutine, true);
        SC_TRY_IF(currentChar == 'E' or currentChar == 'e');
        SC_CO_RETURN(nestedParserCoroutine, true);
        SC_TRY_IF(currentChar == 'T' or currentChar == 't');
        SC_CO_RETURN(nestedParserCoroutine, true);
        SC_TRY_IF(currentChar == ' ');
        tokenLength--;
        method = Method::HttpGET;
    }
    else if (currentChar == 'P' or currentChar == 'p')
    {
        SC_CO_RETURN(nestedParserCoroutine, true);
        if (currentChar == 'U' or currentChar == 'u')
        {
            SC_CO_RETURN(nestedParserCoroutine, true);
            SC_TRY_IF(currentChar == 'T' or currentChar == 't');
            SC_CO_RETURN(nestedParserCoroutine, true);
            SC_TRY_IF(currentChar == ' ');
            tokenLength--;
            method = Method::HttpPUT;
        }
        else if (currentChar == 'O' or currentChar == 'o')
        {
            SC_CO_RETURN(nestedParserCoroutine, true);
            SC_TRY_IF(currentChar == 'S' or currentChar == 's');
            SC_CO_RETURN(nestedParserCoroutine, true);
            SC_TRY_IF(currentChar == 'T' or currentChar == 't');
            SC_CO_RETURN(nestedParserCoroutine, true);
            SC_TRY_IF(currentChar == ' ');
            tokenLength--;
            method = Method::HttpPOST;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    state = State::Result;
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseUrl(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    if (currentChar == '*')
    {
        state = State::Result;
    }
    else
    {
        while (currentChar != ' ')
            SC_CO_RETURN(nestedParserCoroutine, true);
        tokenLength--;
        state = State::Result;
    }
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

template <bool spaces>
bool SC::HttpParser::parseVersion(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    SC_TRY_IF(currentChar == 'H' or currentChar == 'h');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == 'T' or currentChar == 't');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == 'T' or currentChar == 't');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == 'P' or currentChar == 'p');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == '/' or currentChar == '/');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == '1' or currentChar == '1');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == '.' or currentChar == '.');
    SC_CO_RETURN(nestedParserCoroutine, true);
    SC_TRY_IF(currentChar == '1' or currentChar == '1');
    SC_CO_RETURN(nestedParserCoroutine, true);
    if (spaces)
    {
        SC_TRY_IF(currentChar == ' ');
        tokenLength--;
    }
    else
    {
        SC_TRY_IF(currentChar == '\r');
        tokenLength--;
        SC_CO_RETURN(nestedParserCoroutine, true);
        SC_TRY_IF(currentChar == '\n');
        tokenLength--;
    }
    state = State::Result;
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseHeaderName(char currentChar)
{
    static constexpr StringView headers[] = {"Content-Length"};

    SC_CO_BEGIN(nestedParserCoroutine);
    matchIndex = 0;
    for (size_t idx = 0; idx < numMatches; ++idx)
    {
        matchingHeader[idx]      = headers[idx].sizeInBytes();
        matchingHeaderValid[idx] = false;
    }
    while (currentChar != ':')
    {
        for (size_t idx = 0; idx < numMatches; ++idx)
        {
            if (matchingHeader[idx] == 0)
                continue;
            if (headers[idx].bytesIncludingTerminator()[matchIndex] == currentChar)
            {
                matchingHeader[idx] -= 1;
                if (matchingHeader[idx] == 0)
                {
                    matchingHeaderValid[idx] = true;
                }
            }
            else
            {
                matchingHeader[idx] = 0;
            }
        }
        matchIndex += 1;
        SC_CO_RETURN(nestedParserCoroutine, true);
    }
    state = State::Result;
    tokenLength--;
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseHeaderValue(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    while (currentChar == ' ')
    {
        tokenLength--;
        tokenStart++;
        SC_CO_RETURN(nestedParserCoroutine, true);
    }
    while (true)
    {
        if (currentChar == '\r')
        {
            tokenLength--;
            SC_CO_RETURN(nestedParserCoroutine, true);
            if (currentChar != '\n')
            {
                return false;
            }
            tokenLength--;
            state = State::Result;
            break;
        }
        SC_CO_RETURN(nestedParserCoroutine, true);
    }
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseStatusCode(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    number = 0;
    while (currentChar == ' ')
    {
        tokenLength--;
        tokenStart++;
        SC_CO_RETURN(nestedParserCoroutine, true);
    }
    while (true)
    {
        if (currentChar >= '0' and currentChar <= '9')
        {
            if (matchIndex >= 20)
                return false; // too many digits
            number = number * 10 + static_cast<decltype(number)>(currentChar - '0');
            matchIndex++;
            SC_CO_RETURN(nestedParserCoroutine, true);
        }
        else if (currentChar == ' ')
        {
            tokenLength--;
            state = State::Result;
            break;
        }
        else
        {
            return false;
        }
    }
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseNumberValue(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    while (currentChar == ' ')
    {
        tokenLength--;
        tokenStart++;
        SC_CO_RETURN(nestedParserCoroutine, true);
    }
    number = 0;
    while (true)
    {
        if (currentChar >= '0' and currentChar <= '9')
        {
            if (matchIndex >= 20)
                return false; // too many digits to hold in int64
            number = number * 10 + static_cast<decltype(number)>(currentChar - '0');
            matchIndex++;
            SC_CO_RETURN(nestedParserCoroutine, true);
        }
        else if (currentChar == '\r')
        {
            tokenLength--;
            SC_CO_RETURN(nestedParserCoroutine, true);
            if (currentChar == '\n')
            {
                tokenLength--;
                state = State::Result;
                break;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}

bool SC::HttpParser::parseHeadersEnd(char currentChar)
{
    SC_CO_BEGIN(nestedParserCoroutine);
    if (currentChar != '\r')
    {
        return false;
    }
    tokenLength--;
    SC_CO_RETURN(nestedParserCoroutine, true);
    if (currentChar != '\n')
    {
        return false;
    }
    tokenLength--;
    state = State::Result;
    SC_CO_FINISH(nestedParserCoroutine);
    return true;
}