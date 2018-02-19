/******************************************************************************
Project: M2-Team Common Library
Description: Definition for Windows Runtime part of M2-Team Common Library
File Name: M2.Helpers.WinRT.h
License: The MIT License
******************************************************************************/

#pragma once

#ifndef _M2_HELPERS_WINRT_
#define _M2_HELPERS_WINRT_

#include <Windows.h>
#include <wrl\client.h>

#include <string>

// The M2MakeUTF16String function converts from UTF-8 string to UTF-16 string.
//
// Parameters:
//
// UTF8String
//     The UTF-8 string you want to convert.
//
// Return value:
//
// The return value is the UTF-16 string.
std::wstring M2MakeUTF16String(const std::string& UTF8String);

// The M2MakeUTF8String function converts from UTF-16 string to UTF-8 string.
//
// Parameters:
//
// UTF16String
//     The UTF-16 string you want to convert.
//
// Return value:
//
// The return value is the UTF-8 string.
std::string M2MakeUTF8String(const std::wstring& UTF16String);

// The M2MakeUTF16String function converts from C++/CX string to UTF-16 string.
//
// Parameters:
//
// PlatformString
//     The C++/CX string you want to convert.
//
// Return value:
//
// The return value is the UTF-16 string.
std::wstring M2MakeUTF16String(Platform::String^& PlatformString);

// The M2MakeUTF8String function converts from C++/CX string to UTF-8 string.
//
// Parameters:
//
// PlatformString
//     The C++/CX string you want to convert.
//
// Return value:
//
// The return value is the UTF-8 string.
std::string M2MakeUTF8String(Platform::String^& PlatformString);

#endif // _M2_HELPERS_WINRT_
