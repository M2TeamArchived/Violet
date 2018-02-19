/******************************************************************************
* Project: VioletCore
* Description: The precompiled header, used to include external libraries.
* File Name: pch.h
* License: The MIT License
******************************************************************************/

#pragma once

#include <collection.h>
#include <ppltasks.h>

#include <queue>

#include "shcore.h"

#include <mfapi.h>
#pragma comment(lib,"mfuuid.lib")


// 为编译通过而禁用的警告
#if _MSC_VER >= 1200
#pragma warning(push)
// 该文件包含不能在当前代码页中表示的字符。以 Unicode 格式保存该文件防止数据丢失。(等级 1)
#pragma warning(disable:4819) 
// 一个浮点类型转换为整数类型。可能发生了数据丢失。(等级 2)
#pragma warning(disable:4244) 
#endif

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#pragma comment(lib, "FFmpegUniversal.lib")
}

#if _MSC_VER >= 1200
#pragma warning(pop)
#endif

#include "M2AsyncHelpers.h"

// Disable debug string output on non-debug build
#if !_DEBUG
#define DebugMessage(x)
#else
#define DebugMessage(x) OutputDebugString(x)
#endif
