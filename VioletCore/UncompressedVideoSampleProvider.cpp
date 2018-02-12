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
#include "UncompressedVideoSampleProvider.h"
#include "NativeBufferFactory.h"
#include <mfapi.h>

extern "C"
{
#include <libavutil/imgutils.h>
}

using namespace FFmpegInterop;
using namespace NativeBuffer;
using namespace Windows::Media::MediaProperties;

UncompressedVideoSampleProvider::UncompressedVideoSampleProvider(
	FFmpegReader^ reader,
	AVFormatContext* avFormatCtx,
	AVCodecContext* avCodecCtx,
	FFmpegInteropConfig^ config,
	int streamIndex)
	: UncompressedSampleProvider(reader, avFormatCtx, avCodecCtx, config, streamIndex)
{
	for (int i = 0; i < 4; i++)
	{
		this->m_VideoBufferLineSize[i] = 0;
		this->m_VideoBufferData[i] = nullptr;
	}
	
	if (config->VideoOutputAllowIyuv && (m_pAvCodecCtx->pix_fmt == AV_PIX_FMT_YUV420P || m_pAvCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ420P)
		&& m_pAvCodecCtx->codec->capabilities & AV_CODEC_CAP_DR1)
	{
		// if format is yuv and yuv is allowed and codec supports direct buffer decoding, use yuv
		m_OutputPixelFormat = m_pAvCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ420P ? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P;
		OutputMediaSubtype = MediaEncodingSubtypes::Iyuv;
	}
	else if (config->VideoOutputAllowNv12)
	{
		// NV12 is generally the preferred format
		m_OutputPixelFormat = AV_PIX_FMT_NV12;
		OutputMediaSubtype = MediaEncodingSubtypes::Nv12;
	}
	else if (config->VideoOutputAllowIyuv)
	{
		m_OutputPixelFormat = m_pAvCodecCtx->pix_fmt == AV_PIX_FMT_YUVJ420P 	? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P;
		OutputMediaSubtype = MediaEncodingSubtypes::Iyuv;
	}
	else if (config->VideoOutputAllowBgra8)
	{
		m_OutputPixelFormat = AV_PIX_FMT_BGRA;
		OutputMediaSubtype = MediaEncodingSubtypes::Bgra8;
	}
	else // if no format is allowed, we still use NV12
	{
		m_OutputPixelFormat = AV_PIX_FMT_NV12;
		OutputMediaSubtype = MediaEncodingSubtypes::Nv12;
	}

	auto width = avCodecCtx->width;
	auto height = avCodecCtx->height;

	if (m_pAvCodecCtx->pix_fmt == m_OutputPixelFormat)
	{
		if (m_pAvCodecCtx->codec->capabilities & AV_CODEC_CAP_DR1)
		{
			// This codec supports direct buffer decoding.
			// Get decoder frame size and override get_buffer2...
			avcodec_align_dimensions(m_pAvCodecCtx, &width, &height);

			m_pAvCodecCtx->get_buffer2 = get_buffer2;
			m_pAvCodecCtx->opaque = (void*)this;
		}
		else
		{
			m_bUseScaler = true;
		}
	}
	else
	{
		// Scaler required to convert pixel format
		m_bUseScaler = true;
	}

	if (!m_bUseScaler)
	{

	}

	DecoderWidth = width;
	DecoderHeight = height;
}

#include <M2.Helpers.WinRT.h>

HRESULT UncompressedVideoSampleProvider::InitializeScalerIfRequired()
{
	HRESULT hr = S_OK;
	if (m_bUseScaler && !m_pSwsCtx)
	{
		// Setup software scaler to convert frame to output pixel type
		m_pSwsCtx = sws_getContext(
			m_pAvCodecCtx->width,
			m_pAvCodecCtx->height,
			m_pAvCodecCtx->pix_fmt,
			m_pAvCodecCtx->width,
			m_pAvCodecCtx->height,
			m_OutputPixelFormat,
			SWS_BICUBIC,
			NULL,
			NULL,
			NULL);

		if (m_pSwsCtx == nullptr)
		{
			hr = E_OUTOFMEMORY;
		}

		// Using scaler: allocate a new frame from buffer pool
		hr = FillLinesAndBuffer(this->m_VideoBufferLineSize, this->m_VideoBufferData, &this->m_VideoBufferReference);

		this->m_VideoBufferObject = M2MakeIBuffer(this->m_VideoBufferReference->data, this->m_VideoBufferReference->size);
	}

	return hr;
}

UncompressedVideoSampleProvider::~UncompressedVideoSampleProvider()
{
	if (m_pSwsCtx)
	{
		sws_freeContext(m_pSwsCtx);
	}

	if (m_pBufferPool)
	{
		av_buffer_pool_uninit(&m_pBufferPool);
	}



	if (nullptr != this->m_VideoBufferReference)
	{
		free_buffer(this->m_VideoBufferReference);
	}
}



HRESULT UncompressedVideoSampleProvider::CreateBufferFromFrame(IBuffer^* pBuffer, AVFrame* avFrame, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;

	hr = InitializeScalerIfRequired();

	if (SUCCEEDED(hr))
	{
		if (!m_bUseScaler)
		{
			// Using direct buffer: just create a buffer reference to hand out to MSS pipeline
			auto bufferRef = av_buffer_ref(avFrame->buf[0]);
			if (bufferRef)
			{
				*pBuffer = NativeBufferFactory::CreateNativeBuffer(bufferRef->data, bufferRef->size, free_buffer, bufferRef);
			}
			else
			{
				hr = E_FAIL;
			}
		}
		else
		{
			// Convert to output format using FFmpeg software scaler
			if (sws_scale(m_pSwsCtx, (const uint8_t **)(avFrame->data), avFrame->linesize, 0, m_pAvCodecCtx->height, this->m_VideoBufferData, this->m_VideoBufferLineSize) > 0)
			{
				*pBuffer = this->m_VideoBufferObject;
			}
			else
			{
				free_buffer(this->m_VideoBufferReference);
				hr = E_FAIL;
			}
		}
	}

	// Don't set a timestamp on S_FALSE
	if (hr == S_OK)
	{
		// Try to get the best effort timestamp for the frame.
		framePts = avFrame->best_effort_timestamp;
		m_interlaced_frame = avFrame->interlaced_frame == 1;
		m_top_field_first = avFrame->top_field_first == 1;
		m_chroma_location = avFrame->chroma_location;
	}

	return hr;
}

Guid g_MFMTVideoChromaSiting(MF_MT_VIDEO_CHROMA_SITING);

IBox<uint32>^ g_MFVideoChromaSubsampling_MPEG2 = ref new Box<uint32>(MFVideoChromaSubsampling_MPEG2);
IBox<uint32>^ g_MFVideoChromaSubsampling_MPEG1 = ref new Box<uint32>(MFVideoChromaSubsampling_MPEG1);
IBox<uint32>^ g_MFVideoChromaSubsampling_DV_PAL = ref new Box<uint32>(MFVideoChromaSubsampling_DV_PAL);
IBox<uint32>^ g_MFVideoChromaSubsampling_Cosited = ref new Box<uint32>(MFVideoChromaSubsampling_Cosited);

IBox<int>^ g_TrueValue = ref new Box<int>(TRUE);
IBox<int>^ g_FalseValue = ref new Box<int>(FALSE);

Guid g_MFSampleExtension_Interlaced(MFSampleExtension_Interlaced);
Guid g_MFSampleExtension_BottomFieldFirst(MFSampleExtension_BottomFieldFirst);
Guid g_MFSampleExtension_RepeatFirstField(MFSampleExtension_RepeatFirstField);

HRESULT UncompressedVideoSampleProvider::SetSampleProperties(MediaStreamSample^ sample)
{
	MediaStreamSamplePropertySet^ ExtendedProperties = sample->ExtendedProperties;
	
	
	if (m_interlaced_frame)
	{
		ExtendedProperties->Insert(g_MFSampleExtension_Interlaced, g_TrueValue);
		ExtendedProperties->Insert(g_MFSampleExtension_BottomFieldFirst, m_top_field_first ? g_FalseValue : g_TrueValue);
		ExtendedProperties->Insert(g_MFSampleExtension_RepeatFirstField, g_FalseValue);
	}
	else
	{
		ExtendedProperties->Insert(g_MFSampleExtension_Interlaced, g_FalseValue);
	}

	switch (m_chroma_location)
	{
	case AVCHROMA_LOC_LEFT:
		ExtendedProperties->Insert(g_MFMTVideoChromaSiting, g_MFVideoChromaSubsampling_MPEG2);
		break;
	case AVCHROMA_LOC_CENTER:
		ExtendedProperties->Insert(g_MFMTVideoChromaSiting, g_MFVideoChromaSubsampling_MPEG1);
		break;
	case AVCHROMA_LOC_TOPLEFT:
		if (m_interlaced_frame)
		{
			ExtendedProperties->Insert(g_MFMTVideoChromaSiting, g_MFVideoChromaSubsampling_DV_PAL);
		}
		else
		{
			ExtendedProperties->Insert(g_MFMTVideoChromaSiting, g_MFVideoChromaSubsampling_Cosited);
		}
		break;
	default:
		break;
	}

	return S_OK;
}

HRESULT UncompressedVideoSampleProvider::FillLinesAndBuffer(int* linesize, byte** data, AVBufferRef** buffer)
{
	if (av_image_fill_linesizes(linesize, m_OutputPixelFormat, DecoderWidth) < 0)
	{
		return E_FAIL;
	}

	auto YBufferSize = linesize[0] * DecoderHeight;
	auto UBufferSize = linesize[1] * DecoderHeight / 2;
	auto VBufferSize = linesize[2] * DecoderHeight / 2;
	auto totalSize = YBufferSize + UBufferSize + VBufferSize;

	buffer[0] = AllocateBuffer(totalSize);
	if (!buffer[0])
	{
		return E_OUTOFMEMORY;
	}

	data[0] = buffer[0]->data;
	data[1] = UBufferSize > 0 ? buffer[0]->data + YBufferSize : NULL;
	data[2] = VBufferSize > 0 ? buffer[0]->data + YBufferSize + UBufferSize : NULL;
	data[3] = NULL;

	return S_OK;
}

AVBufferRef* UncompressedVideoSampleProvider::AllocateBuffer(int totalSize)
{
	if (!m_pBufferPool)
	{
		m_pBufferPool = av_buffer_pool_init(totalSize, NULL);
		if (!m_pBufferPool)
		{
			return NULL;
		}
	}

	auto buffer = av_buffer_pool_get(m_pBufferPool);
	if (!buffer)
	{
		return NULL;
	}
	if (buffer->size != totalSize)
	{
		free_buffer(buffer);
		return NULL;
	}

	return buffer;
}

int UncompressedVideoSampleProvider::get_buffer2(AVCodecContext *avCodecContext, AVFrame *frame, int flags)
{
	// If frame size changes during playback and gets larger than our buffer, we need to switch to sws_scale
	auto provider = reinterpret_cast<UncompressedVideoSampleProvider^>(avCodecContext->opaque);
	provider->m_bUseScaler = frame->height > provider->DecoderHeight || frame->width > provider->DecoderWidth;
	if (provider->m_bUseScaler)
	{
		return avcodec_default_get_buffer2(avCodecContext, frame, flags);
	}
	else
	{
		return provider->FillLinesAndBuffer(frame->linesize, frame->data, frame->buf);
	}
}
