// Copyright (c) 2022-2023, Stefano Cristiano
//
// All Rights Reserved. Reproduction is not allowed.
#include "System.h"

#include "../Foundation/Strings/StringConverter.h"
#include "../System/Console.h"

#include <dlfcn.h> // dlopen

SC::ReturnCode SC::SystemDynamicLibraryTraits::releaseHandle(Handle& handle)
{
    if (handle)
    {
        const int res = ::dlclose(handle);
        return ReturnCode(res == 0);
    }
    return ReturnCode(true);
}

SC::ReturnCode SC::SystemDynamicLibrary::load(StringView fullPath)
{
    SC_TRY(close());
    SmallString<1024> string = StringEncoding::Native;
    StringConverter   converter(string);
    StringView        fullPathZeroTerminated;
    SC_TRY(converter.convertNullTerminateFastPath(fullPath, fullPathZeroTerminated));
    handle = ::dlopen(fullPathZeroTerminated.getNullTerminatedNative(), RTLD_LAZY);
    if (handle == nullptr)
    {
        return ReturnCode::Error("dlopen failed");
    }
    return ReturnCode(true);
}

SC::ReturnCode SC::SystemDynamicLibrary::loadSymbol(StringView symbolName, void*& symbol) const
{
    SC_TRY_MSG(isValid(), "Invalid dlsym handle");
    SmallString<1024> string = StringEncoding::Native;
    StringConverter   converter(string);
    StringView        symbolZeroTerminated;
    SC_TRY(converter.convertNullTerminateFastPath(symbolName, symbolZeroTerminated));
    symbol = ::dlsym(handle, symbolZeroTerminated.getNullTerminatedNative());
    return ReturnCode(symbol != nullptr);
}
