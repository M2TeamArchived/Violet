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

#include "UncompressedAudioSampleProvider.h"
#include "NativeBufferFactory.h"

using namespace FFmpegInterop;

UncompressedAudioSampleProvider::UncompressedAudioSampleProvider(
	FFmpegReader^ reader,
	AVFormatContext* avFormatCtx,
	AVCodecContext* avCodecCtx,
	FFmpegInteropConfig^ config,
	int streamIndex)
	: UncompressedSampleProvider(reader, avFormatCtx, avCodecCtx, config, streamIndex)
	, m_pSwrCtx(nullptr)
{
}

HRESULT UncompressedAudioSampleProvider::AllocateResources()
{
	HRESULT hr = S_OK;
	hr = UncompressedSampleProvider::AllocateResources();
	if (SUCCEEDED(hr))
	{
		// Set default channel layout when the value is unknown (0)
		bool IsFFProfileAACHEV2 = (
			m_pAvCodecCtx->profile == FF_PROFILE_AAC_HE_V2 &&
			m_pAvCodecCtx->extradata_size != 0);

		int channels = m_pAvCodecCtx->channels;
		if (IsFFProfileAACHEV2)
		{
			channels *= 2;
		}
		int64 inChannelLayout = 0;
		if (m_pAvCodecCtx->channel_layout && !IsFFProfileAACHEV2)
		{
			inChannelLayout = m_pAvCodecCtx->channel_layout;
		}
		else
		{
			inChannelLayout= av_get_default_channel_layout(channels);
		}
		int64 outChannelLayout = av_get_default_channel_layout(channels);

		switch (m_pAvCodecCtx->sample_fmt)
		{
		case AV_SAMPLE_FMT_S32:
		case AV_SAMPLE_FMT_S32P:
			m_outputSampleFormat = AV_SAMPLE_FMT_S32;
			break;
		case AV_SAMPLE_FMT_FLT:
		case AV_SAMPLE_FMT_FLTP:
			m_outputSampleFormat = AV_SAMPLE_FMT_FLT;
			break;
		default:
			m_outputSampleFormat = AV_SAMPLE_FMT_S16;
			break;
		}

		// Set up resampler to convert to output format and channel layout.
		m_pSwrCtx = swr_alloc_set_opts(
			NULL,
			outChannelLayout,
			m_outputSampleFormat,
			m_pAvCodecCtx->sample_rate,
			inChannelLayout,
			m_pAvCodecCtx->sample_fmt,
			m_pAvCodecCtx->sample_rate,
			0,
			NULL);

		if (!m_pSwrCtx)
		{
			hr = E_OUTOFMEMORY;
		}

		if (SUCCEEDED(hr))
		{
			if (swr_init(m_pSwrCtx) < 0)
			{
				hr = E_FAIL;
			}
		}
	}

	return hr;
}

UncompressedAudioSampleProvider::~UncompressedAudioSampleProvider()
{
	// Free 
	swr_free(&m_pSwrCtx);
}

HRESULT UncompressedAudioSampleProvider::CreateBufferFromFrame(IBuffer^* pBuffer, AVFrame* avFrame, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;

	// Resample uncompressed frame to output format
	uint8_t **resampledData = nullptr;
	unsigned int aBufferSize = av_samples_alloc_array_and_samples(&resampledData, NULL, m_pAvCodecCtx->channels, avFrame->nb_samples, m_outputSampleFormat, 0);
	int resampledDataSize = swr_convert(m_pSwrCtx, resampledData, aBufferSize, (const uint8_t **)avFrame->extended_data, avFrame->nb_samples);

	if (resampledDataSize < 0)
	{
		hr = E_FAIL;
	}
	else
	{
		auto size = min(aBufferSize, (unsigned int)(resampledDataSize * m_pAvCodecCtx->channels * av_get_bytes_per_sample(m_outputSampleFormat)));
		*pBuffer = NativeBuffer::NativeBufferFactory::CreateNativeBuffer(resampledData[0], size, av_freep, resampledData);
	}

	if (SUCCEEDED(hr))
	{
		// always update duration with real decoded sample duration
		auto actualDuration = (long long)avFrame->nb_samples * m_pAvFormatCtx->streams[m_streamIndex]->time_base.den / (m_pAvCodecCtx->sample_rate * m_pAvFormatCtx->streams[m_streamIndex]->time_base.num);
	
		if (frameDuration != actualDuration)
		{
			// compensate for start encoder padding (gapless playback)
			if (m_pAvFormatCtx->streams[m_streamIndex]->nb_decoded_frames == 1 && m_pAvFormatCtx->streams[m_streamIndex]->start_skip_samples > 0)
			{
				// check if duration difference matches encoder padding
				auto skipDuration = (long long)m_pAvFormatCtx->streams[m_streamIndex]->start_skip_samples * m_pAvFormatCtx->streams[m_streamIndex]->time_base.den / (m_pAvCodecCtx->sample_rate * m_pAvFormatCtx->streams[m_streamIndex]->time_base.num);
				if (skipDuration == frameDuration - actualDuration)
				{
					framePts += skipDuration;
				}
			}
			frameDuration = actualDuration;
		}
	}

	return hr;
}

