/******************************************************************************
Project: M2-Team Common Library
Description: Implemention for Windows Runtime part of M2-Team Common Library
File Name: M2.Helpers.WinRT.cpp
License: The MIT License
******************************************************************************/

#include "pch.h"

#include "M2.Helpers.WinRT.h"

#include <Windows.h>

#include <string>

using Platform::String;

using std::string;
using std::wstring;

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
wstring M2MakeUTF16String(const string& UTF8String)
{
	wstring UTF16String;

	int UTF16StringLength = MultiByteToWideChar(
		CP_UTF8,
		0,
		UTF8String.data(),
		(int)UTF8String.size(),
		nullptr,
		0);
	if (UTF16StringLength > 0)
	{
		UTF16String.resize(UTF16StringLength);
		MultiByteToWideChar(
			CP_UTF8,
			0,
			UTF8String.data(),
			(int)UTF8String.size(),
			&UTF16String[0],
			UTF16StringLength);
	}

	return UTF16String;
}

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
string M2MakeUTF8String(const wstring& UTF16String)
{
	string UTF8String;

	int UTF8StringLength = WideCharToMultiByte(
		CP_UTF8,
		0,
		UTF16String.data(),
		(int)UTF16String.size(),
		nullptr,
		0,
		nullptr,
		nullptr);
	if (UTF8StringLength > 0)
	{
		UTF8String.resize(UTF8StringLength);
		WideCharToMultiByte(
			CP_UTF8,
			0,
			UTF16String.data(),
			(int)UTF16String.size(),
			&UTF8String[0],
			UTF8StringLength,
			nullptr,
			nullptr);
	}

	return UTF8String;
}

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
wstring M2MakeUTF16String(String^& PlatformString)
{
	return wstring(PlatformString->Data(), PlatformString->Length());
}

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
string M2MakeUTF8String(String^& PlatformString)
{
	return M2MakeUTF8String(M2MakeUTF16String(PlatformString));
}
