/******************************************************************************
* Project: VioletCore
* Description: The main header file.
* File Name: VioletCore.h
* License: The MIT License
******************************************************************************/

#pragma once

#include "FFmpegInteropMSS.h"

namespace VioletCore
{
	using Platform::String;
	using Windows::Foundation::Collections::PropertySet;
	using Windows::Foundation::TimeSpan;
	using Windows::Media::Core::AudioStreamDescriptor;
	using Windows::Media::Core::VideoStreamDescriptor;
	using Windows::Media::Core::MediaStreamSource;
	using Windows::Storage::Streams::IRandomAccessStream;
	
	// Level values from ffmpeg: libavutil/log.h
	public enum class VioletCoreLogLevel
	{
		Panic = 0,
		Fatal = 8,
		Error = 16,
		Warning = 24,
		Info = 32,
		Verbose = 40,
		Debug = 48,
		Trace = 56
	};

	public interface class IVioletCoreLogHandler
	{
		void WriteLog(VioletCoreLogLevel LogLevel, String^ LogMessage);
	};
	
	public ref class VioletCoreConfig sealed
	{
	public:
		static property VioletCoreLogLevel LogLevel
		{
			VioletCoreLogLevel get();
			void set(VioletCoreLogLevel LogLevel);
		}

		static property IVioletCoreLogHandler^ LogHandler
		{
			IVioletCoreLogHandler^ get();
			void set(IVioletCoreLogHandler^ LogHandler);
		}
	};
	
	/*public ref class VioletCoreMSSConfig sealed
	{
	
	};*/
	
	public ref class VioletCoreMSS sealed
	{
	private:
		FFmpegInterop::FFmpegInteropMSS^ m_interop = nullptr;

		VioletCoreMSS(FFmpegInterop::FFmpegInteropMSS^ interop) : m_interop(interop)
		{

		}

	public:
		
		virtual ~VioletCoreMSS();

		static VioletCoreMSS^ CreateFromStream(IRandomAccessStream^ stream);
		static VioletCoreMSS^ CreateFromUri(String^ uri);

		// Contructor
		MediaStreamSource^ GetMediaStreamSource();

		// Properties
		property AudioStreamDescriptor^ AudioDescriptor
		{
			AudioStreamDescriptor^ get()
			{
				return this->m_interop->AudioDescriptor;
			};
		};
		property VideoStreamDescriptor^ VideoDescriptor
		{
			VideoStreamDescriptor^ get()
			{
				return this->m_interop->VideoDescriptor;
			};
		};
		property TimeSpan Duration
		{
			TimeSpan get()
			{
				return this->m_interop->Duration;
			};
		};
		property String^ VideoCodecName
		{
			String^ get()
			{
				return this->m_interop->VideoCodecName;
			};
		};
		property String^ AudioCodecName
		{
			String^ get()
			{
				return this->m_interop->AudioCodecName;
			};
		};
	};

}
