// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#pragma once
#include "String.h"
#include "Types.h"

namespace SC
{
struct OS;
struct OSPaths;
} // namespace SC

struct SC::OS
{
    [[nodiscard]] static bool   printBacktrace();
    [[nodiscard]] static bool   printBacktrace(void** backtraceBuffer, size_t backtraceBufferSizeInBytes);
    [[nodiscard]] static size_t captureBacktrace(size_t framesToSkip, void** backtraceBuffer,
                                                 size_t backtraceBufferSizeInBytes, uint32_t* hash);
};

struct SC::OSPaths
{
    static const int            StaticPathSize = 1024;
    SmallString<StaticPathSize> executableFile; // Full path (native encoding) to executable file including extension
    SmallString<StaticPathSize> applicationRootDirectory; // Full path to (native encoding) Application directory

    [[nodiscard]] static bool init();
    [[nodiscard]] static bool close();

    [[nodiscard]] static const OSPaths& get();
};
