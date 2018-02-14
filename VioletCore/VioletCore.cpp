/******************************************************************************
* Project: VioletCore
* Description: The main source file.
* File Name: VioletCore.cpp
* License: The MIT License
******************************************************************************/

#include "pch.h"
#include "VioletCore.h"

using namespace VioletCore;

namespace VioletCore
{
	namespace Global
	{
		IVioletCoreLogHandler^ LogHandler = nullptr;
	}

	namespace Internal
	{
		void FFmpegLogCallBack(
			void *avcl, int level, const char *fmt, va_list vl)
		{
			if (level > av_log_get_level()) 
				return;

			if (nullptr == Global::LogHandler) 
				return;
			
			char pLine[1024];
			int printPrefix = 1;
			av_log_format_line(avcl, level, fmt, vl, pLine, sizeof(pLine), &printPrefix);

			wchar_t wLine[sizeof(pLine)];
			if (MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, pLine, -1, wLine, sizeof(pLine)) != 0)
			{
				Global::LogHandler->WriteLog(
					static_cast<VioletCoreLogLevel>(level), ref new String(wLine));
			}
		}
	}
}


VioletCoreLogLevel VioletCoreConfig::LogLevel::get()
{
	return static_cast<VioletCoreLogLevel>(av_log_get_level());
}

void VioletCoreConfig::LogLevel::set(VioletCoreLogLevel LogLevel)
{
	av_log_set_level(static_cast<int>(LogLevel));
}

IVioletCoreLogHandler^ VioletCoreConfig::LogHandler::get()
{
	return Global::LogHandler;
}

void VioletCoreConfig::LogHandler::set(IVioletCoreLogHandler^ LogHandler)
{
	if (nullptr == LogHandler)
	{
		av_log_set_callback(av_log_default_callback);
	}
	else
	{
		Global::LogHandler = LogHandler;
		av_log_set_callback(Internal::FFmpegLogCallBack);
	}
}

VioletCoreMSS::~VioletCoreMSS()
{
}

VioletCoreMSS^ VioletCore::VioletCoreMSS::CreateFromStream(IRandomAccessStream ^ stream)
{
	return ref new VioletCoreMSS(FFmpegInterop::FFmpegInteropMSS::CreateFromStream(stream, ref new FFmpegInterop::FFmpegInteropConfig(), nullptr));
}

VioletCoreMSS ^ VioletCore::VioletCoreMSS::CreateFromUri(String ^ uri)
{
	return ref new VioletCoreMSS(FFmpegInterop::FFmpegInteropMSS::CreateFromUri(uri, ref new FFmpegInterop::FFmpegInteropConfig()));
}

MediaStreamSource ^ VioletCore::VioletCoreMSS::GetMediaStreamSource()
{
	return this->m_interop->GetMediaStreamSource();
}
