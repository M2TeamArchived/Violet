# Project Violet 

## License
- MIT License

## Third-party
- FFmpegUniversalSDK
  - Project Repository
    - https://github.com/M2Team/FFmpegUniversal
  - License
    - LGPL version 2.1 or later
  - Folder
    - [SourceRoot]\ThirdParty\FFmpegUniversalSDK
  - Note
    - You need to download it and compile it sepreately.
	- Compiled binaries and headers download
	  - https://github.com/M2Team/FFmpegUniversal/releases
- FFmpegInterop
  - Project Repository
    - https://github.com/Microsoft/FFmpegInterop
  - License
    - Apache 2.0 License
  - Folder
    - [SourceRoot]\ThirdParty\FFmpegInterop
  - Note
    - The solutions of Windows 8.x and Windows Phone 8.x are removed.
	- FFmpeg compile script is removed because we have FFmpegUniversalSDK.
	- UnitTest is removed because we don't need to modify FFmpegInterop.
	- C++ and JavaScript samples are removed because we only need C++ sample.