#pragma once

using namespace Windows::Foundation::Collections;

namespace FFmpegInterop
{
	/*public*/ ref class FFmpegInteropConfig sealed
	{
	public:
		FFmpegInteropConfig()
		{
			SkipErrors = 50;
			MaxAudioThreads = 2;

			MaxPlaybackRate = 4.0;
			StreamBufferSize = 16384;

			FFmpegOptions = ref new PropertySet();
		};

		property unsigned int SkipErrors;

		property unsigned int MaxVideoThreads;
		property unsigned int MaxAudioThreads;

		property double MaxPlaybackRate;
		property unsigned int StreamBufferSize;

		property PropertySet^ FFmpegOptions;
	};
}