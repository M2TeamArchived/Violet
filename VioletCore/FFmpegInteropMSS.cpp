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

#include "pch.h"
#include "FFmpegInteropMSS.h"
#include "UncompressedAudioSampleProvider.h"
#include "UncompressedVideoSampleProvider.h"
#include "CritSec.h"

using namespace concurrency;
using namespace FFmpegInterop;
using namespace Platform;
using namespace Platform::Collections;
using namespace Windows::Storage::Streams;
using namespace Windows::Media::MediaProperties;

// Static functions passed to FFmpeg
static int FileStreamRead(void* ptr, uint8_t* buf, int bufSize);
static int64_t FileStreamSeek(void* ptr, int64_t pos, int whence);
static int lock_manager(void **mtx, enum AVLockOp op);

// Flag for ffmpeg global setup
static bool isRegistered = false;

// Initialize an FFmpegInteropObject
FFmpegInteropMSS::FFmpegInteropMSS(FFmpegInteropConfig^ interopConfig)
	: config(interopConfig)
	, isFirstSeek(true)
{
	if (!isRegistered)
	{
		av_register_all();
		av_lockmgr_register(lock_manager);
		isRegistered = true;
	}

	this->m_NumberOfHardwareThreads = M2GetNumberOfHardwareThreads();
}

FFmpegInteropMSS::~FFmpegInteropMSS()
{
	this->csGuard.Lock();

	if (mss)
	{
		mss->Starting -= startingRequestedToken;
		mss->SampleRequested -= sampleRequestedToken;
		mss->SwitchStreamsRequested -= switchStreamRequestedToken;
		mss = nullptr;
	}

	// Clear our data
	currentAudioStream = nullptr;
	videoStream = nullptr;

	if (m_pReader != nullptr)
	{
		m_pReader = nullptr;
	}

	sampleProviders.clear();
	audioStreams.clear();

	avformat_close_input(&avFormatCtx);
	av_free(avIOCtx);
	av_dict_free(&avDict);
	
	if (fileStreamData != nullptr)
	{
		fileStreamData->Release();
	}

	this->csGuard.Unlock();
}

FFmpegInteropMSS^ FFmpegInteropMSS::CreateFromStream(IRandomAccessStream^ stream, FFmpegInteropConfig^ config, MediaStreamSource^ mss)
{
	auto interopMSS = ref new FFmpegInteropMSS(config);
	auto hr = interopMSS->CreateMediaStreamSource(stream, mss);
	if (!SUCCEEDED(hr))
	{
		throw ref new Exception(hr, "Failed to open media.");
	}
	return interopMSS;
}

FFmpegInteropMSS^ FFmpegInteropMSS::CreateFromUri(String^ uri, FFmpegInteropConfig^ config)
{
	auto interopMSS = ref new FFmpegInteropMSS(config);
	auto hr = interopMSS->CreateMediaStreamSource(uri);
	if (!SUCCEEDED(hr))
	{
		throw ref new Exception(hr, "Failed to open media.");
	}
	return interopMSS;
}

MediaStreamSource^ FFmpegInteropMSS::GetMediaStreamSource()
{
	return mss;
}

HRESULT FFmpegInteropMSS::CreateMediaStreamSource(String^ uri)
{
	HRESULT hr = S_OK;
	const char* charStr = nullptr;
	if (!uri)
	{
		hr = E_INVALIDARG;
	}

	if (SUCCEEDED(hr))
	{
		avFormatCtx = avformat_alloc_context();
		if (avFormatCtx == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		// Populate AVDictionary avDict based on PropertySet ffmpegOptions. List of options can be found in https://www.ffmpeg.org/ffmpeg-protocols.html
		hr = ParseOptions(config->FFmpegOptions);
	}

	if (SUCCEEDED(hr))
	{
		std::wstring uriW(uri->Begin());
		std::string uriA(uriW.begin(), uriW.end());
		charStr = uriA.c_str();

		// Open media in the given URI using the specified options
		if (avformat_open_input(&avFormatCtx, charStr, NULL, &avDict) < 0)
		{
			hr = E_FAIL; // Error opening file
		}

		// avDict is not NULL only when there is an issue with the given ffmpegOptions such as invalid key, value type etc. Iterate through it to see which one is causing the issue.
		if (avDict != nullptr)
		{
			DebugMessage(L"Invalid FFmpeg option(s)");
			av_dict_free(&avDict);
			avDict = nullptr;
		}
	}

	if (SUCCEEDED(hr))
	{
		this->mss = nullptr;
		hr = InitFFmpegContext();
	}

	return hr;
}

HRESULT FFmpegInteropMSS::CreateMediaStreamSource(IRandomAccessStream^ stream, MediaStreamSource^ MSS)
{
	HRESULT hr = S_OK;
	if (!stream)
	{
		hr = E_INVALIDARG;
	}

	if (SUCCEEDED(hr))
	{
		// Convert asynchronous IRandomAccessStream to synchronous IStream. This API requires shcore.h and shcore.lib
		hr = CreateStreamOverRandomAccessStream(reinterpret_cast<IUnknown*>(stream), IID_PPV_ARGS(&fileStreamData));
	}

	if (SUCCEEDED(hr))
	{
		// Setup FFmpeg custom IO to access file as stream. This is necessary when accessing any file outside of app installation directory and appdata folder.
		// Credit to Philipp Sch http://www.codeproject.com/Tips/489450/Creating-Custom-FFmpeg-IO-Context
		fileStreamBuffer = (unsigned char*)av_malloc(config->StreamBufferSize);
		if (fileStreamBuffer == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		avIOCtx = avio_alloc_context(fileStreamBuffer, config->StreamBufferSize, 0, fileStreamData, FileStreamRead, 0, FileStreamSeek);
		if (avIOCtx == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		avFormatCtx = avformat_alloc_context();
		if (avFormatCtx == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	if (SUCCEEDED(hr))
	{
		// Populate AVDictionary avDict based on PropertySet ffmpegOptions. List of options can be found in https://www.ffmpeg.org/ffmpeg-protocols.html
		hr = ParseOptions(config->FFmpegOptions);
	}

	if (SUCCEEDED(hr))
	{
		avFormatCtx->pb = avIOCtx;
		avFormatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

		// Open media file using custom IO setup above instead of using file name. Opening a file using file name will invoke fopen C API call that only have
		// access within the app installation directory and appdata folder. Custom IO allows access to file selected using FilePicker dialog.
		if (avformat_open_input(&avFormatCtx, "", NULL, &avDict) < 0)
		{
			hr = E_FAIL; // Error opening file
		}

		// avDict is not NULL only when there is an issue with the given ffmpegOptions such as invalid key, value type etc. Iterate through it to see which one is causing the issue.
		if (avDict != nullptr)
		{
			DebugMessage(L"Invalid FFmpeg option(s)");
			av_dict_free(&avDict);
			avDict = nullptr;
		}
	}

	if (SUCCEEDED(hr))
	{
		this->mss = MSS;
		hr = InitFFmpegContext();
	}

	return hr;
}

static int is_hwaccel_pix_fmt(enum AVPixelFormat pix_fmt)
{
	const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
	return desc->flags & AV_PIX_FMT_FLAG_HWACCEL;
}

static AVPixelFormat get_format(struct AVCodecContext *s, const enum AVPixelFormat *fmt)
{
	UNREFERENCED_PARAMETER(s);
	
	AVPixelFormat result = (AVPixelFormat)-1;
	AVPixelFormat format;
	int index = 0;
	do
	{
		format = fmt[index++];
		if (format != -1 && result == -1 && !is_hwaccel_pix_fmt(format))
		{
			// take first non hw accelerated format
			result = format;
		}
		else if (format == AV_PIX_FMT_NV12 && result != AV_PIX_FMT_YUVA420P)
		{
			// switch to NV12 if available, unless this is an alpha channel file
			result = format;
		}
	} while (format != -1);
	return result;
}

HRESULT FFmpegInteropMSS::InitFFmpegContext()
{
	HRESULT hr = S_OK;

	if (SUCCEEDED(hr))
	{
		if (avformat_find_stream_info(avFormatCtx, NULL) < 0)
		{
			hr = E_FAIL; // Error finding info
		}
	}

	if (SUCCEEDED(hr))
	{
		m_pReader = ref new FFmpegReader(avFormatCtx, &sampleProviders);
		if (m_pReader == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}
	}

	auto audioStrInfos = ref new Vector<AudioStreamInfo^>();
	auto subtitleStrInfos = ref new Vector<SubtitleStreamInfo^>();

	AVCodec* avVideoCodec;
	auto videoStreamIndex = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &avVideoCodec, 0);
	auto audioStreamIndex = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	auto subtitleStreamIndex = av_find_best_stream(avFormatCtx, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);

	for (int index = 0; index < static_cast<int>(avFormatCtx->nb_streams); ++index)
	{
		auto avStream = avFormatCtx->streams[index];
		MediaSampleProvider^ stream;

		if (avStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			stream = CreateAudioStream(avStream, index);
			if (stream)
			{
				bool isDefault = index == audioStreamIndex;

				// TODO get info from sample provider
				auto info = ref new AudioStreamInfo(stream->Name, stream->Language, stream->CodecName, avStream->codecpar->bit_rate, isDefault,
					avStream->codecpar->channels, avStream->codecpar->sample_rate,
					max(avStream->codecpar->bits_per_raw_sample, avStream->codecpar->bits_per_coded_sample));
				if (isDefault)
				{
					currentAudioStream = stream;
					audioStrInfos->InsertAt(0, info);
					audioStreams.insert(audioStreams.begin(), stream);
				}
				else
				{
					audioStrInfos->Append(info);
					audioStreams.push_back(stream);
				}
			}
		}
		else if (avStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && index == videoStreamIndex)
		{
			videoStream = stream = CreateVideoStream(avStream, index);

			if (videoStream)
			{
				videoStreamInfo = ref new VideoStreamInfo(stream->Name, stream->Language, stream->CodecName, avStream->codecpar->bit_rate, true,
					avStream->codecpar->width, avStream->codecpar->height,
					max(avStream->codecpar->bits_per_raw_sample, avStream->codecpar->bits_per_coded_sample));
			}
		}
		else if (avStream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
		{
			auto title = av_dict_get(avStream->metadata, "title", NULL, 0);
			auto language = av_dict_get(avStream->metadata, "language", NULL, 0);
			auto codec = avcodec_find_decoder(avStream->codecpar->codec_id);
			auto codecName = codec ? codec->name : NULL;
			auto isDefault = index == subtitleStreamIndex;
			auto info = ref new SubtitleStreamInfo(
				ConvertString(title ? title->value : NULL),
				ConvertString(language ? language->value : NULL),
				ConvertString(codecName),
				isDefault,
				(avStream->disposition & AV_DISPOSITION_FORCED) == AV_DISPOSITION_FORCED);

			if (isDefault)
			{
				subtitleStrInfos->InsertAt(0, info);
			}
			else
			{
				subtitleStrInfos->Append(info);
			}
		}

		sampleProviders.push_back(stream);
	}

	if (!currentAudioStream && audioStreams.size() > 0)
	{
		currentAudioStream = audioStreams[0];
	}

	audioStreamInfos = audioStrInfos->GetView();
	subtitleStreamInfos = subtitleStrInfos->GetView();

	if (videoStream && currentAudioStream)
	{
		mss = ref new MediaStreamSource(videoStream->StreamDescriptor, currentAudioStream->StreamDescriptor);
		videoStream->EnableStream();
		currentAudioStream->EnableStream();
	}
	else if (currentAudioStream)
	{
		mss = ref new MediaStreamSource(currentAudioStream->StreamDescriptor);
		currentAudioStream->EnableStream();
	}
	else
	{
		hr = E_FAIL;
	}

	if (SUCCEEDED(hr))
	{
		for each (auto stream in audioStreams)
		{
			if (stream != currentAudioStream)
			{
				mss->AddStreamDescriptor(stream->StreamDescriptor);
			}
		}
	}

	if (SUCCEEDED(hr))
	{
		// Convert media duration from AV_TIME_BASE to TimeSpan unit
		mediaDuration = { LONGLONG(avFormatCtx->duration * 10000000 / double(AV_TIME_BASE)) };

		TimeSpan buffer = { 0 };
		mss->BufferTime = buffer;
		if (mediaDuration.Duration > 0)
		{
			mss->Duration = mediaDuration;
			mss->CanSeek = true;
		}

		startingRequestedToken = mss->Starting += ref new TypedEventHandler<MediaStreamSource ^, MediaStreamSourceStartingEventArgs ^>(this, &FFmpegInteropMSS::OnStarting);
		sampleRequestedToken = mss->SampleRequested += ref new TypedEventHandler<MediaStreamSource ^, MediaStreamSourceSampleRequestedEventArgs ^>(this, &FFmpegInteropMSS::OnSampleRequested);
		switchStreamRequestedToken = mss->SwitchStreamsRequested += ref new TypedEventHandler<MediaStreamSource ^, MediaStreamSourceSwitchStreamsRequestedEventArgs ^>(this, &FFmpegInteropMSS::OnSwitchStreamsRequested);
	}

	return hr;
}

MediaSampleProvider^ FFmpegInteropMSS::CreateAudioStream(AVStream * avStream, int index)
{
	HRESULT hr = S_OK;
	MediaSampleProvider^ audioStream = nullptr;
	auto avAudioCodec = avcodec_find_decoder(avStream->codecpar->codec_id);
	if (avAudioCodec)
	{
		// allocate a new decoding context
		auto avAudioCodecCtx = avcodec_alloc_context3(avAudioCodec);
		if (!avAudioCodecCtx)
		{
			DebugMessage(L"Could not allocate a decoding context\n");
			hr = E_OUTOFMEMORY;
		}

		if (SUCCEEDED(hr))
		{
			// initialize the stream parameters with demuxer information
			if (avcodec_parameters_to_context(avAudioCodecCtx, avStream->codecpar) < 0)
			{
				hr = E_FAIL;
			}

			if (SUCCEEDED(hr))
			{
				if (avAudioCodecCtx->sample_fmt == AV_SAMPLE_FMT_S16P)
				{
					avAudioCodecCtx->request_sample_fmt = AV_SAMPLE_FMT_S16;
				}
				else if (avAudioCodecCtx->sample_fmt == AV_SAMPLE_FMT_S32P)
				{
					avAudioCodecCtx->request_sample_fmt = AV_SAMPLE_FMT_S32;
				}
				else if (avAudioCodecCtx->sample_fmt == AV_SAMPLE_FMT_FLTP)
				{
					avAudioCodecCtx->request_sample_fmt = AV_SAMPLE_FMT_FLT;
				}

				// enable multi threading
				unsigned threads = this->m_NumberOfHardwareThreads;
				if (threads > 0)
				{
					avAudioCodecCtx->thread_count = config->MaxAudioThreads == 0 ? threads : min(threads, config->MaxAudioThreads);
					avAudioCodecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
				}

				if (avcodec_open2(avAudioCodecCtx, avAudioCodec, NULL) < 0)
				{
					hr = E_FAIL;
				}
				else
				{
					// Detect audio format and create audio stream descriptor accordingly
					audioStream = CreateAudioSampleProvider(avStream, avAudioCodecCtx, index);
				}
			}
		}

		// free codec context if failed
		if (!audioStream && avAudioCodecCtx)
		{
			avcodec_free_context(&avAudioCodecCtx);
		}
	}
	else
	{
		DebugMessage(L"Could not find decoder\n");
	}

	return audioStream;
}

MediaSampleProvider^ FFmpegInteropMSS::CreateVideoStream(AVStream * avStream, int index)
{
	HRESULT hr = S_OK;
	MediaSampleProvider^ result = nullptr;

	// Find the video stream and its decoder
	auto avVideoCodec = avcodec_find_decoder(avStream->codecpar->codec_id);

	if (avVideoCodec)
	{
		// allocate a new decoding context
		auto avVideoCodecCtx = avcodec_alloc_context3(avVideoCodec);
		if (!avVideoCodecCtx)
		{
			DebugMessage(L"Could not allocate a decoding context\n");
			hr = E_OUTOFMEMORY;
		}

		if (SUCCEEDED(hr))
		{
			avVideoCodecCtx->get_format = &get_format;

			// initialize the stream parameters with demuxer information
			if (avcodec_parameters_to_context(avVideoCodecCtx, avStream->codecpar) < 0)
			{
				hr = E_FAIL;
			}
		}

		if (SUCCEEDED(hr))
		{
			// enable multi threading
			unsigned threads = this->m_NumberOfHardwareThreads;
			if (threads > 0)
			{
				avVideoCodecCtx->thread_count = config->MaxVideoThreads == 0 ? threads : min(threads, config->MaxVideoThreads);
				avVideoCodecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
			}

			if (avcodec_open2(avVideoCodecCtx, avVideoCodec, NULL) < 0)
			{
				hr = E_FAIL;
			}
			else
			{
				// Detect video format and create video stream descriptor accordingly
				result = CreateVideoSampleProvider(avStream, avVideoCodecCtx, index);
			}
		}

		// free codec context if failed
		if (!result && avVideoCodecCtx)
		{
			avcodec_free_context(&avVideoCodecCtx);
		}
	}

	return result;
}

MediaSampleProvider^ FFmpegInteropMSS::CreateAudioSampleProvider(AVStream* avStream, AVCodecContext* avAudioCodecCtx, int index)
{
	UNREFERENCED_PARAMETER(avStream);
	
	MediaSampleProvider^ audioSampleProvider = ref new UncompressedAudioSampleProvider(m_pReader, avFormatCtx, avAudioCodecCtx, config, index);

	auto hr = audioSampleProvider->Initialize();
	if (FAILED(hr))
	{
		audioSampleProvider = nullptr;
	}

	return audioSampleProvider;
}

MediaSampleProvider^ FFmpegInteropMSS::CreateVideoSampleProvider(AVStream* avStream, AVCodecContext* avVideoCodecCtx, int index)
{
	UNREFERENCED_PARAMETER(avStream);

	MediaSampleProvider^ videoSampleProvider = ref new UncompressedVideoSampleProvider(m_pReader, avFormatCtx, avVideoCodecCtx, config, index);

	auto hr = videoSampleProvider->Initialize();
	if (FAILED(hr))
	{
		videoSampleProvider = nullptr;
	}

	return videoSampleProvider;
}

HRESULT FFmpegInteropMSS::ParseOptions(PropertySet^ ffmpegOptions)
{
	HRESULT hr = S_OK;

	// Convert FFmpeg options given in PropertySet to AVDictionary. List of 
	// options can be found in https://www.ffmpeg.org/ffmpeg-protocols.html
	if (ffmpegOptions != nullptr)
	{
		for (auto options = ffmpegOptions->First(); options->HasCurrent; options->MoveNext())
		{
			auto option = options->Current;
			
			std::string Key = M2MakeUTF8String(option->Key);
			std::string Value = M2MakeUTF8String(option->Value->ToString());

			// Add key and value pair entry
			if (av_dict_set(&avDict, Key.c_str(), Value.c_str(), 0) < 0)
			{
				hr = E_INVALIDARG;
				break;
			}
		}
	}

	return hr;
}

void FFmpegInteropMSS::OnStarting(MediaStreamSource ^sender, MediaStreamSourceStartingEventArgs ^args)
{
	MediaStreamSourceStartingRequest^ request = args->Request;

	// Perform seek operation when MediaStreamSource received seek event from MediaElement
	if (request->StartPosition && request->StartPosition->Value.Duration <= mediaDuration.Duration && (!isFirstSeek || request->StartPosition->Value.Duration > 0))
	{
		auto hr = Seek(request->StartPosition->Value);
		if (SUCCEEDED(hr))
		{
			request->SetActualStartPosition(request->StartPosition->Value);
		}

		if (videoStream && !videoStream->IsEnabled)
		{
			videoStream->EnableStream();
		}

		if (currentAudioStream && !currentAudioStream->IsEnabled)
		{
			currentAudioStream->EnableStream();
		}
	}

	isFirstSeek = false;
}

void FFmpegInteropMSS::OnSampleRequested(Windows::Media::Core::MediaStreamSource ^sender, MediaStreamSourceSampleRequestedEventArgs ^args)
{
	this->csGuard.Lock();

	if (mss != nullptr)
	{
		if (currentAudioStream && args->Request->StreamDescriptor == currentAudioStream->StreamDescriptor)
		{
			args->Request->Sample = currentAudioStream->GetNextSample();
		}
		else if (videoStream && args->Request->StreamDescriptor == videoStream->StreamDescriptor)
		{
			args->Request->Sample = videoStream->GetNextSample();
		}
		else
		{
			args->Request->Sample = nullptr;
		}
	}

	this->csGuard.Unlock();
}

void FFmpegInteropMSS::OnSwitchStreamsRequested(MediaStreamSource ^ sender, MediaStreamSourceSwitchStreamsRequestedEventArgs ^ args)
{
	this->csGuard.Lock();
	if (currentAudioStream && args->Request->OldStreamDescriptor && currentAudioStream->StreamDescriptor)
	{
		for each (auto stream in audioStreams)
		{
			if (stream->StreamDescriptor == args->Request->NewStreamDescriptor)
			{
				currentAudioStream->DisableStream();
				currentAudioStream = stream;
				currentAudioStream->EnableStream();
			}
		}
	}
	this->csGuard.Unlock();
}

HRESULT FFmpegInteropMSS::Seek(TimeSpan position)
{
	auto hr = S_OK;

	// Select the first valid stream either from video or audio
	int streamIndex = videoStream ? videoStream->StreamIndex : currentAudioStream ? currentAudioStream->StreamIndex : -1;

	if (streamIndex >= 0)
	{
		// Compensate for file start_time, then convert to stream time_base
		auto correctedPosition = position.Duration + (avFormatCtx->start_time * 10);
		int64_t seekTarget = static_cast<int64_t>(correctedPosition / (av_q2d(avFormatCtx->streams[streamIndex]->time_base) * 10000000));

		if (av_seek_frame(avFormatCtx, streamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0)
		{
			hr = E_FAIL;
			DebugMessage(L" - ### Error while seeking\n");
		}
		else
		{
			// Flush the AudioSampleProvider
			if (currentAudioStream != nullptr)
			{
				currentAudioStream->Flush();
			}

			// Flush the VideoSampleProvider
			if (videoStream != nullptr)
			{
				videoStream->Flush();
			}
		}
	}
	else
	{
		hr = E_FAIL;
	}

	return hr;
}

// Static function to read file stream and pass data to FFmpeg. Credit to Philipp Sch http://www.codeproject.com/Tips/489450/Creating-Custom-FFmpeg-IO-Context
static int FileStreamRead(void* ptr, uint8_t* buf, int bufSize)
{
	IStream* pStream = reinterpret_cast<IStream*>(ptr);
	ULONG bytesRead = 0;
	HRESULT hr = pStream->Read(buf, bufSize, &bytesRead);

	if (FAILED(hr))
	{
		return -1;
	}

	// If we succeed but don't have any bytes, assume end of file
	if (bytesRead == 0)
	{
		return AVERROR_EOF;  // Let FFmpeg know that we have reached eof
	}

	return bytesRead;
}

// Static function to seek in file stream. Credit to Philipp Sch http://www.codeproject.com/Tips/489450/Creating-Custom-FFmpeg-IO-Context
static int64_t FileStreamSeek(void* ptr, int64_t pos, int whence)
{
	IStream* pStream = reinterpret_cast<IStream*>(ptr);
	if (whence == AVSEEK_SIZE)
	{
		// get stream size
		STATSTG status;
		if (FAILED(pStream->Stat(&status, STATFLAG_NONAME)))
		{
			return -1;
		}
		return status.cbSize.QuadPart;
	}
	else
	{
		LARGE_INTEGER in;
		in.QuadPart = pos;
		ULARGE_INTEGER out = { 0 };

		if (FAILED(pStream->Seek(in, whence, &out)))
		{
			return -1;
		}

		return out.QuadPart; // Return the new position:
	}
}

static int lock_manager(void **mtx, enum AVLockOp op)
{
	switch (op)
	{
	case AV_LOCK_CREATE:
	{
		*mtx = new CritSec();
		return 0;
	}
	case AV_LOCK_OBTAIN:
	{
		auto mutex = static_cast<CritSec*>(*mtx);
		mutex->Lock();
		return 0;
	}
	case AV_LOCK_RELEASE:
	{
		auto mutex = static_cast<CritSec*>(*mtx);
		mutex->Unlock();
		return 0;
	}
	case AV_LOCK_DESTROY:
	{
		auto mutex = static_cast<CritSec*>(*mtx);
		delete mutex;
		return 0;
	}
	}
	return 1;
}
