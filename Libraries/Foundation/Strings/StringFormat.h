// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "StringView.h"

namespace SC
{
template <typename T>
struct Vector;
struct Console;
struct String;
template <typename T>
struct StringFormatterFor;

struct StringFormatOutput
{
    StringFormatOutput(StringEncoding encoding, Vector<char>& destination);
    StringFormatOutput(StringEncoding encoding, Console& newConsole);

    [[nodiscard]] bool write(StringView text);

    void               onFormatBegin();
    void               onFormatFailed();
    [[nodiscard]] bool onFormatSucceded();

  private:
    Vector<char>*  data    = nullptr;
    Console*       console = nullptr;
    StringEncoding encoding;
    size_t         backupSize = 0;
};

template <typename RangeIterator>
struct StringFormat
{
    template <typename... Types>
    [[nodiscard]] static bool format(StringFormatOutput& data, StringView fmt, Types&&... args);

  private:
    struct Implementation;
};

} // namespace SC

//-----------------------------------------------------------------------------------------------------------------------
// Implementations Details
//-----------------------------------------------------------------------------------------------------------------------
template <typename RangeIterator>
struct SC::StringFormat<RangeIterator>::Implementation
{
    template <int Total, int N, typename T, typename... Rest>
    static bool formatArgument(StringFormatOutput& data, StringView specifier, int position, T&& arg, Rest&&... rest)
    {
        if (position == Total - N)
        {
            using First = typename RemoveConst<typename RemoveReference<T>::type>::type;
            return StringFormatterFor<First>::format(data, specifier, arg);
        }
        else
        {
            return formatArgument<Total, N - 1>(data, specifier, position, forward<Rest>(rest)...);
        }
    }

    template <int Total, int N, typename... Args>
    static typename SC::EnableIf<sizeof...(Args) == 0, bool>::type formatArgument(StringFormatOutput&, StringView, int,
                                                                                  Args...)
    {
        return false;
    }

    template <typename... Types>
    static bool parsePosition(StringFormatOutput& data, RangeIterator& it, int32_t& parsedPosition, Types&&... args)
    {
        const auto startOfSpecifier = it;
        if (it.advanceUntilMatches('}')) // We have an already matched '{' when arriving here
        {
            auto specifier         = startOfSpecifier.sliceFromStartUntil(it);
            auto specifierPosition = specifier;
            if (specifier.advanceUntilMatches(':'))
            {
                specifierPosition = startOfSpecifier.sliceFromStartUntil(specifier);
                (void)specifier.stepForward(); // eat '{'
            }
            (void)specifierPosition.stepForward(); // eat '{'
            (void)it.stepForward();                // eat '}'
            const StringView positionString  = StringView::fromIteratorUntilEnd(specifierPosition);
            const StringView specifierString = StringView::fromIteratorUntilEnd(specifier);
            if (not positionString.isEmpty())
            {
                if (not positionString.parseInt32(parsedPosition))
                {
                    return false;
                }
            }
            constexpr auto maxArgs = sizeof...(args);
            return formatArgument<maxArgs, maxArgs>(data, specifierString, parsedPosition, forward<Types>(args)...);
        }
        return false;
    }

    template <typename... Types>
    static bool executeFormat(StringFormatOutput& data, RangeIterator it, Types&&... args)
    {
        StringCodePoint matchedChar;

        auto    start       = it;
        int32_t position    = 0;
        int32_t maxPosition = 0;
        while (true)
        {
            if (it.advanceUntilMatchesAny({'{', '}'}, matchedChar)) // match start or end of specifier
            {
                if (it.isFollowedBy(matchedChar))
                    SC_LANGUAGE_UNLIKELY // if it's the same matched, let's escape it
                    {
                        (void)it.stepForward(); // we want to make sure we insert the escaped '{' or '}'
                        if (not data.write(StringView::fromIterators(start, it)))
                            return false;
                        (void)it.stepForward(); // we don't want to insert the additional '{' or '}' needed for escaping
                        start = it;
                    }
                else if (matchedChar == '{') // it's a '{' not followed by itself, so let's parse specifier
                {
                    if (not data.write(StringView::fromIterators(start, it))) // write everything before '{
                        return false;
                    // try parse '}' and eventually format
                    int32_t parsedPosition = position;
                    if (not parsePosition(data, it, parsedPosition, forward<Types>(args)...))
                        return false;
                    start = it;
                    position += 1;
                    maxPosition = max(maxPosition, parsedPosition + 1);
                }
                else
                {
                    return false; // arriving here means end of string with as single, unescaped '}'
                }
            }
            else
            {
                if (not data.write(StringView::fromIterators(start, it))) // write everything before '{
                    return false;
                return maxPosition == static_cast<int32_t>(sizeof...(args)); // check right number of args
            }
        }
    }
};

template <typename RangeIterator>
template <typename... Types>
bool SC::StringFormat<RangeIterator>::format(StringFormatOutput& data, StringView fmt, Types&&... args)
{
    data.onFormatBegin();
    if (Implementation::executeFormat(data, fmt.getIterator<RangeIterator>(), forward<Types>(args)...))
        SC_LANGUAGE_LIKELY { return data.onFormatSucceded(); }
    else
    {
        data.onFormatFailed();
        return false;
    }
}

namespace SC
{
// clang-format off
template <> struct StringFormatterFor<float>        {static bool format(StringFormatOutput&, const StringView, const float);};
template <> struct StringFormatterFor<double>       {static bool format(StringFormatOutput&, const StringView, const double);};
#if SC_COMPILER_MSVC || SC_COMPILER_CLANG_CL
#if SC_PLATFORM_64_BIT == 0
template <> struct StringFormatterFor<SC::ssize_t>  {static bool format(StringFormatOutput&, const StringView, const SC::ssize_t);};
#endif
#else
template <> struct StringFormatterFor<SC::size_t>   {static bool format(StringFormatOutput&, const StringView, const SC::size_t);};
template <> struct StringFormatterFor<SC::ssize_t>  {static bool format(StringFormatOutput&, const StringView, const SC::ssize_t);};
#endif
template <> struct StringFormatterFor<SC::int64_t>  {static bool format(StringFormatOutput&, const StringView, const SC::int64_t);};
template <> struct StringFormatterFor<SC::uint64_t> {static bool format(StringFormatOutput&, const StringView, const SC::uint64_t);};
template <> struct StringFormatterFor<SC::int32_t>  {static bool format(StringFormatOutput&, const StringView, const SC::int32_t);};
template <> struct StringFormatterFor<SC::uint32_t> {static bool format(StringFormatOutput&, const StringView, const SC::uint32_t);};
template <> struct StringFormatterFor<SC::int16_t>  {static bool format(StringFormatOutput&, const StringView, const SC::int16_t);};
template <> struct StringFormatterFor<SC::uint16_t> {static bool format(StringFormatOutput&, const StringView, const SC::uint16_t);};
template <> struct StringFormatterFor<SC::int8_t>   {static bool format(StringFormatOutput&, const StringView, const SC::int8_t);};
template <> struct StringFormatterFor<SC::uint8_t>  {static bool format(StringFormatOutput&, const StringView, const SC::uint8_t);};
template <> struct StringFormatterFor<char>         {static bool format(StringFormatOutput&, const StringView, const char);};
template <> struct StringFormatterFor<bool>         {static bool format(StringFormatOutput&, const StringView, const bool);};
template <> struct StringFormatterFor<StringView>   {static bool format(StringFormatOutput&, const StringView, const StringView);};
template <> struct StringFormatterFor<String>       {static bool format(StringFormatOutput&, const StringView, const String&);};
template <> struct StringFormatterFor<const char*>  {static bool format(StringFormatOutput&, const StringView, const char*);};
#if SC_PLATFORM_WINDOWS
template <> struct StringFormatterFor<wchar_t>      {static bool format(StringFormatOutput&, const StringView, const wchar_t);};
template <> struct StringFormatterFor<const wchar_t*> {static bool format(StringFormatOutput&, const StringView, const wchar_t*);};
#endif
// clang-format on

template <int N>
struct StringFormatterFor<char[N]>
{
    static bool format(StringFormatOutput& data, const StringView specifier, const char* str)
    {
        const StringView sv(str, N - 1, true, StringEncoding::Ascii);
        return StringFormatterFor<StringView>::format(data, specifier, sv);
    }
};
} // namespace SC
