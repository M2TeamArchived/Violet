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

	// We use NV12 format, NV12 is generally the preferred format.
	m_OutputPixelFormat = AV_PIX_FMT_NV12;
	OutputMediaSubtype = MediaEncodingSubtypes::Nv12;

	auto width = avCodecCtx->width;
	auto height = avCodecCtx->height;

	DecoderWidth = width;
	DecoderHeight = height;
}

#include <M2.Helpers.WinRT.h>

HRESULT UncompressedVideoSampleProvider::InitializeScalerIfRequired()
{
	HRESULT hr = S_OK;
	if (!m_pSwsCtx)
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

		// Allocate a frame for output.
		if (av_image_alloc(
			this->m_VideoBufferData,
			this->m_VideoBufferLineSize,
			DecoderWidth,
			DecoderHeight,
			m_OutputPixelFormat,
			1) < 0)
		{
			hr = E_FAIL;
		}

		int YBufferSize = this->m_VideoBufferLineSize[0] * DecoderHeight;
		int UBufferSize = this->m_VideoBufferLineSize[1] * DecoderHeight / 2;
		int VBufferSize = this->m_VideoBufferLineSize[2] * DecoderHeight / 2;
		int YUVBufferSize = YBufferSize + UBufferSize + VBufferSize;
		
		this->m_VideoBufferObject = M2MakeIBuffer(this->m_VideoBufferData[0], YUVBufferSize);
	}

	return hr;
}

UncompressedVideoSampleProvider::~UncompressedVideoSampleProvider()
{
	if (m_pSwsCtx)
	{
		sws_freeContext(m_pSwsCtx);
	}

	if (nullptr != this->m_VideoBufferData)
	{
		av_freep(this->m_VideoBufferData);
	}
}

HRESULT UncompressedVideoSampleProvider::CreateBufferFromFrame(IBuffer^* pBuffer, AVFrame* avFrame, int64_t& framePts, int64_t& frameDuration)
{
	HRESULT hr = S_OK;

	hr = InitializeScalerIfRequired();

	if (SUCCEEDED(hr))
	{
		// Convert to output format using FFmpeg software scaler
		if (sws_scale(
			m_pSwsCtx,
			(const uint8_t **)(avFrame->data), 
			avFrame->linesize,
			0,
			m_pAvCodecCtx->height,
			this->m_VideoBufferData,
			this->m_VideoBufferLineSize) > 0)
		{
			*pBuffer = this->m_VideoBufferObject;
		}
		else
		{
			av_freep(this->m_VideoBufferData);
			hr = E_FAIL;
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

HRESULT UncompressedVideoSampleProvider::SetSampleProperties(MediaStreamSample^ sample)
{
	MediaStreamSamplePropertySet^ ExtendedProperties = sample->ExtendedProperties;
	
	ExtendedProperties->Insert(
		Guid(MFSampleExtension_Interlaced), 
		ref new Box<int>(m_interlaced_frame ? TRUE : FALSE));
	
	if (m_interlaced_frame)
	{
		ExtendedProperties->Insert(
			Guid(MFSampleExtension_BottomFieldFirst), 
			ref new Box<int>(m_top_field_first ? FALSE : TRUE));

		ExtendedProperties->Insert(
			Guid(MFSampleExtension_RepeatFirstField), 
			ref new Box<int>(FALSE));
	}
	
	bool NeedToSetMFMTVideoChromaSiting = false;
	MFVideoChromaSubsampling MFMTVideoChromaSitingValue;

	switch (m_chroma_location)
	{
	case AVCHROMA_LOC_LEFT:
		MFMTVideoChromaSitingValue = MFVideoChromaSubsampling_MPEG2;
		NeedToSetMFMTVideoChromaSiting = true;		
		break;
	case AVCHROMA_LOC_CENTER:
		MFMTVideoChromaSitingValue = MFVideoChromaSubsampling_MPEG1;
		NeedToSetMFMTVideoChromaSiting = true;
		break;
	case AVCHROMA_LOC_TOPLEFT:
		MFMTVideoChromaSitingValue = m_interlaced_frame
			? MFVideoChromaSubsampling_DV_PAL
			: MFVideoChromaSubsampling_Cosited;
		NeedToSetMFMTVideoChromaSiting = true;
		break;
	default:
		break;
	}

	if (NeedToSetMFMTVideoChromaSiting)
	{
		ExtendedProperties->Insert(
			Guid(MF_MT_VIDEO_CHROMA_SITING), 
			ref new Box<uint32>(MFMTVideoChromaSitingValue));
	}

	return S_OK;
}
