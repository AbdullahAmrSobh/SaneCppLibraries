// Copyright (c) Stefano Cristiano
// SPDX-License-Identifier: MIT
#include "../String.h"
#include "../../Testing/Testing.h"

namespace SC
{
struct StringTest;
}

struct SC::StringTest : public SC::TestCase
{
    StringTest(SC::TestReport& report) : TestCase(report, "StringTest")
    {
        using namespace SC;

        if (test_section("construction_comparison"))
        {
            StringView sv  = "Test String";
            String     str = String("Test String");
            SC_TEST_EXPECT(str == sv);
            SC_TEST_EXPECT(str.view() == sv);
            SC_TEST_EXPECT(str.owns(str.view().sliceStart(1)));
            String str2 = "Another String";
            SC_TEST_EXPECT(not str.owns(str2.view().sliceStart(1)));
            SC_TEST_EXPECT(str != "ASD");
            SC_TEST_EXPECT(str == "Test String");
            SC_TEST_EXPECT(str == str);
            SC_TEST_EXPECT(str != String("ASD"));
            str = "Salver";
            SC_TEST_EXPECT(str == "Salver");
            SC_TEST_EXPECT(str < "Zest string");
        }
    }
};

namespace SC
{
void runStringTest(SC::TestReport& report) { StringTest test(report); }
} // namespace SC
