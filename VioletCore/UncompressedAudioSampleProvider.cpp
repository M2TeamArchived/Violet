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

extern "C"
{
#include <libswresample/swresample.h>
}

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
		inChannels = outChannels = m_pAvCodecCtx->profile == FF_PROFILE_AAC_HE_V2 && m_pAvCodecCtx->channels == 1 ? 2 : m_pAvCodecCtx->channels;
		inChannelLayout = m_pAvCodecCtx->channel_layout && (m_pAvCodecCtx->profile != FF_PROFILE_AAC_HE_V2 || m_pAvCodecCtx->channels > 1) ? m_pAvCodecCtx->channel_layout : av_get_default_channel_layout(inChannels);
		outChannelLayout = av_get_default_channel_layout(outChannels);
		inSampleRate = outSampleRate = m_pAvCodecCtx->sample_rate;
		inSampleFormat = m_pAvCodecCtx->sample_fmt;
		outSampleFormat =
			(inSampleFormat == AV_SAMPLE_FMT_S32 || inSampleFormat == AV_SAMPLE_FMT_S32P) ? AV_SAMPLE_FMT_S32 :
			(inSampleFormat == AV_SAMPLE_FMT_FLT || inSampleFormat == AV_SAMPLE_FMT_FLTP) ? AV_SAMPLE_FMT_FLT :
			AV_SAMPLE_FMT_S16;
	
		// Set up resampler to convert to output format and channel layout.
		m_pSwrCtx = swr_alloc_set_opts(
			NULL,
			outChannelLayout,
			outSampleFormat,
			outSampleRate,
			inChannelLayout,
			inSampleFormat,
			inSampleRate,
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
				swr_free(&m_pSwrCtx);
			}
		}
	}

	return hr;
}

UncompressedAudioSampleProvider::~UncompressedAudioSampleProvider()
{
	// Free 
	if (m_pSwrCtx)
	{
		swr_free(&m_pSwrCtx);
	}
}

HRESULT UncompressedAudioSampleProvider::CreateBufferFromFrame(IBuffer^* pBuffer, AVFrame* avFrame, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;
	
	// Resample uncompressed frame to output format
	uint8_t **resampledData = nullptr;
	unsigned int aBufferSize = av_samples_alloc_array_and_samples(&resampledData, NULL, outChannels, avFrame->nb_samples, outSampleFormat, 0);
	int resampledDataSize = swr_convert(m_pSwrCtx, resampledData, aBufferSize, (const uint8_t **)avFrame->extended_data, avFrame->nb_samples);

	if (resampledDataSize < 0)
	{
		hr = E_FAIL;
	}
	else
	{
		auto size = min(aBufferSize, (unsigned int)(resampledDataSize * outChannels * av_get_bytes_per_sample(outSampleFormat)));
		*pBuffer = NativeBuffer::NativeBufferFactory::CreateNativeBuffer(resampledData[0], size, av_freep, resampledData);
	}

	if (SUCCEEDED(hr))
	{
		// always update duration with real decoded sample duration
		auto actualDuration = (long long)avFrame->nb_samples * this->m_pAvStream->time_base.den / (outSampleRate * this->m_pAvStream->time_base.num);

		if (frameDuration != actualDuration)
		{
			// compensate for start encoder padding (gapless playback)
			if (this->m_pAvStream->nb_decoded_frames == 1 && this->m_pAvStream->start_skip_samples > 0)
			{
				// check if duration difference matches encoder padding
				auto skipDuration = (long long)this->m_pAvStream->start_skip_samples * this->m_pAvStream->time_base.den / (outSampleRate * this->m_pAvStream->time_base.num);
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

