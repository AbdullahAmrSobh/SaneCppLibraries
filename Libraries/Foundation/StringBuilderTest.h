// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "StringBuilder.h"
#include "Test.h"

namespace SC
{
struct StringBuilderTest;
}

struct SC::StringBuilderTest : public SC::TestCase
{
    StringBuilderTest(SC::TestReport& report) : TestCase(report, "StringBuilderTest")
    {
        using namespace SC;
        if (test_section("edge_cases"))
        {
            String        buffer(StringEncoding::Ascii);
            StringBuilder builder(buffer);
            SC_TEST_EXPECT(builder.append(StringView(nullptr, 0, true, StringEncoding::Ascii)));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(builder.append(""));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(builder.append("asd"));
            SC_TEST_EXPECT(builder.getResultString() == "asd");
            SC_TEST_EXPECT(not builder.format("asd", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("{", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("}", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("{{", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("}}", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(builder.format("{}{{{{", 1));
            SC_TEST_EXPECT(builder.getResultString() == "1{{");
            SC_TEST_EXPECT(builder.format("{}}}}}", 1));
            SC_TEST_EXPECT(builder.getResultString() == "1}}");
            SC_TEST_EXPECT(not builder.format("{}}}}", 1));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(builder.format("{{{}", 1));
            SC_TEST_EXPECT(builder.getResultString() == "{1");
            SC_TEST_EXPECT(builder.format("{{{}}}-{{{}}}", 1, 2));
            SC_TEST_EXPECT(builder.getResultString() == "{1}-{2}");
            SC_TEST_EXPECT(not builder.format("{{{{}}}-{{{}}}", 1, 2));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
            SC_TEST_EXPECT(not builder.format("{{{{}}}-{{{}}}}", 1, 2));
            SC_TEST_EXPECT(builder.getResultString().isEmpty());
        }
        if (test_section("append"))
        {
            String        buffer(StringEncoding::Ascii);
            StringBuilder builder(buffer);
            SC_TEST_EXPECT(builder.append(StringView("asdf", 3, false, StringEncoding::Ascii)));
            SC_TEST_EXPECT(builder.append("asd"));
            SC_TEST_EXPECT(builder.append(String("asd").view()));
            SC_TEST_EXPECT(builder.getResultString() == "asdasdasd");
        }
        if (test_section("append"))
        {
            String        buffer(StringEncoding::Ascii);
            StringBuilder builder(buffer);
            SC_TEST_EXPECT(not builder.append("{", 1));
            SC_TEST_EXPECT(not builder.append("", 123));
            SC_TEST_EXPECT(builder.append("{}", 123));
            SC_TEST_EXPECT(builder.getResultString() == "123");
            SC_TEST_EXPECT(builder.format("_{}", 123));
            SC_TEST_EXPECT(builder.getResultString() == "_123");
            SC_TEST_EXPECT(builder.format("_{}_", 123));
            SC_TEST_EXPECT(builder.getResultString() == "_123_");
            SC_TEST_EXPECT(builder.format("_{}_TEXT_{}", 123, 12.4));
            SC_TEST_EXPECT(builder.getResultString() == "_123_TEXT_12.400000");
            SC_TEST_EXPECT(builder.format("__{:.2}__", 12.4567f));
            SC_TEST_EXPECT(builder.getResultString() == "__12.46__");
            SC_TEST_EXPECT(builder.format("__{}__", 12.4567f));
            SC_TEST_EXPECT(builder.getResultString() == "__12.456700__");
        }
        if (test_section("append_formats"))
        {
            String        buffer(StringEncoding::Ascii);
            StringBuilder builder(buffer);
            SC_TEST_EXPECT(builder.append("__{}__", static_cast<uint64_t>(MaxValue())));
            SC_TEST_EXPECT(builder.getResultString() == "__18446744073709551615__");
            SC_TEST_EXPECT(builder.format("__{}__", static_cast<int64_t>(MaxValue())));
            SC_TEST_EXPECT(builder.getResultString() == "__9223372036854775807__");
            SC_TEST_EXPECT(builder.format("__{}__", float(1.2)));
            SC_TEST_EXPECT(builder.getResultString() == "__1.200000__");
            SC_TEST_EXPECT(builder.format("__{}__", double(1.2)));
            SC_TEST_EXPECT(builder.getResultString() == "__1.200000__");
            SC_TEST_EXPECT(builder.format("__{}__", ssize_t(-4)));
            SC_TEST_EXPECT(builder.getResultString() == "__-4__");
            SC_TEST_EXPECT(builder.format("__{}__", size_t(+4)));
            SC_TEST_EXPECT(builder.getResultString() == "__4__");
            SC_TEST_EXPECT(builder.format("__{}__", int32_t(-4)));
            SC_TEST_EXPECT(builder.getResultString() == "__-4__");
            SC_TEST_EXPECT(builder.format("__{}__", uint32_t(+4)));
            SC_TEST_EXPECT(builder.getResultString() == "__4__");
            SC_TEST_EXPECT(builder.format("__{}__", int16_t(-4)));
            SC_TEST_EXPECT(builder.getResultString() == "__-4__");
            SC_TEST_EXPECT(builder.format("__{}__", uint16_t(+4)));
            SC_TEST_EXPECT(builder.getResultString() == "__4__");
            SC_TEST_EXPECT(builder.format("__{}__", char('c')));
            SC_TEST_EXPECT(builder.getResultString() == "__c__");
            SC_TEST_EXPECT(builder.format("__{}__", "asd"));
            SC_TEST_EXPECT(builder.getResultString() == "__asd__");
            SC_TEST_EXPECT(builder.format("__{}__", StringView("asd")));
            SC_TEST_EXPECT(builder.getResultString() == "__asd__");
            SC_TEST_EXPECT(builder.format("__{}__", StringView("")));
            SC_TEST_EXPECT(builder.getResultString() == "____");
            SC_TEST_EXPECT(builder.format("__{}__", StringView(nullptr, 0, true, StringEncoding::Ascii)));
            SC_TEST_EXPECT(builder.getResultString() == "____");
            SC_TEST_EXPECT(builder.format("__{}__", String("asd")));
            SC_TEST_EXPECT(builder.getResultString() == "__asd__");
            SC_TEST_EXPECT(builder.format("__{}__", String("")));
            SC_TEST_EXPECT(builder.getResultString() == "____");
            SC_TEST_EXPECT(builder.format("__{}__", String()));
            SC_TEST_EXPECT(builder.getResultString() == "____");
        }
    }
};
