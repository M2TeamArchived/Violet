//*****************************************************************************
//
//	Copyright 2015 Microsoft Corporation
//
//	Licensed under the Apache License, Version 2.0 (the "License");
//	you may not use this file except in compliance with the License.
//	You may obtain a copy of the License at
//
//	http ://www.apache.org/licenses/LICENSE-2.0
//
//	Unless required by applicable law or agreed to in writing, software
//	distributed under the License is distributed on an "AS IS" BASIS,
//	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//	See the License for the specific language governing permissions and
//	limitations under the License.
//
//*****************************************************************************

#pragma once
#include <queue>
#include <mutex>
#include <pplawait.h>
#include "FFmpegReader.h"
#include "MediaSampleProvider.h"
#include "StreamInfo.h"

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Media::Core;

extern "C"
{
#include <libavformat/avformat.h>
}

#include "CritSec.h"

namespace FFmpegInterop
{
	/*public*/ ref class FFmpegInteropMSS sealed
	{
	public:
		static FFmpegInteropMSS^ CreateFromStream(IRandomAccessStream^ stream, FFmpegInteropConfig^ config, MediaStreamSource^ mss);
		static FFmpegInteropMSS^ CreateFromUri(String^ uri, FFmpegInteropConfig^ config);

		// Contructor
		MediaStreamSource^ GetMediaStreamSource();
		virtual ~FFmpegInteropMSS();

		// Properties
		property String^ VideoCodecName
		{
			String^ get()
			{
				return videoStream ? videoStream->CodecName : nullptr;
			};
		};
		property String^ AudioCodecName
		{
			String^ get()
			{
				return audioStreamInfos->Size > 0 ? audioStreamInfos->GetAt(0)->CodecName : nullptr;
			};
		};
		property TimeSpan Duration
		{
			TimeSpan get()
			{
				return mediaDuration;
			};
		};

		property VideoStreamInfo^ VideoStream
		{
			VideoStreamInfo^ get() { return videoStreamInfo; }
		}

		property IVectorView<AudioStreamInfo^>^ AudioStreams
		{
			IVectorView<AudioStreamInfo^>^ get() { return audioStreamInfos; }
		}

		property IVectorView<SubtitleStreamInfo^>^ SubtitleStreams
		{
			IVectorView<SubtitleStreamInfo^>^ get() { return subtitleStreamInfos; }
		}

	internal:
		int ReadPacket();

	private:
		FFmpegInteropMSS(FFmpegInteropConfig^ config);

		DWORD m_NumberOfHardwareThreads = 0;
		
		HRESULT CreateMediaStreamSource(IRandomAccessStream^ stream, MediaStreamSource^ MSS);
		HRESULT CreateMediaStreamSource(String^ uri);
		HRESULT InitFFmpegContext();
		MediaSampleProvider^ CreateAudioStream(AVStream * avStream, int index);
		MediaSampleProvider^ CreateVideoStream(AVStream * avStream, int index);
		MediaSampleProvider^ CreateAudioSampleProvider(AVStream * avStream, AVCodecContext* avCodecCtx, int index);
		MediaSampleProvider^ CreateVideoSampleProvider(AVStream * avStream, AVCodecContext* avCodecCtx, int index);
		HRESULT ParseOptions(PropertySet^ ffmpegOptions);
		void OnStarting(MediaStreamSource ^sender, MediaStreamSourceStartingEventArgs ^args);
		void OnSampleRequested(MediaStreamSource ^sender, MediaStreamSourceSampleRequestedEventArgs ^args);
		void OnSwitchStreamsRequested(MediaStreamSource^ sender, MediaStreamSourceSwitchStreamsRequestedEventArgs^ args);
		HRESULT Seek(TimeSpan position);

		MediaStreamSource^ mss;
		EventRegistrationToken startingRequestedToken;
		EventRegistrationToken sampleRequestedToken;
		EventRegistrationToken switchStreamRequestedToken;

	internal:
		AVDictionary* avDict;
		AVIOContext* avIOCtx;
		AVFormatContext* avFormatCtx;

	private:
		FFmpegInteropConfig ^ config;
		std::vector<MediaSampleProvider^> sampleProviders;
		std::vector<MediaSampleProvider^> audioStreams;
		MediaSampleProvider^ videoStream;
		MediaSampleProvider^ currentAudioStream;
		
		VideoStreamInfo^ videoStreamInfo;
		IVectorView<AudioStreamInfo^>^ audioStreamInfos;
		IVectorView<SubtitleStreamInfo^>^ subtitleStreamInfos;

		CritSec csGuard;

		String^ videoCodecName;
		String^ audioCodecName;
		TimeSpan mediaDuration;
		IStream* fileStreamData;
		unsigned char* fileStreamBuffer;
		FFmpegReader^ m_pReader;
		bool isFirstSeek;
	};
}
