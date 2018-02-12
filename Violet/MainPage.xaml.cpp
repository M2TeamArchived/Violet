//
// MainPage.xaml.cpp
// MainPage 类的实现。
//

#include "pch.h"
#include "MainPage.xaml.h"

using namespace Violet;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

using namespace FFmpegInterop;

using namespace Windows::UI;
using namespace Windows::UI::ViewManagement;

// 设置暗黑风标题栏颜色
void SetDarkTitleBarColor(
	_In_ ApplicationViewTitleBar ^TitleBar)
{
	// 标题栏

	TitleBar->ForegroundColor = Colors::White;
	TitleBar->BackgroundColor = Colors::Black;

	// 非活动状态标题栏

	TitleBar->InactiveForegroundColor = Colors::Gray;
	TitleBar->InactiveBackgroundColor = Colors::Black;

	// 标题栏按钮

	TitleBar->ButtonForegroundColor = Colors::White;
	TitleBar->ButtonBackgroundColor = Colors::Black;

	// 非活动状态标题栏按钮

	TitleBar->ButtonInactiveForegroundColor = Colors::White;
	TitleBar->ButtonInactiveBackgroundColor = Colors::Black;

	// 当指针位于标题栏按钮

	TitleBar->ButtonHoverForegroundColor = Colors::White;
	TitleBar->ButtonHoverBackgroundColor = Colors::Gray;
}

using namespace Concurrency;
using namespace Windows::UI::Popups;

// 显示错误信息
void DisplayErrorMessage(Platform::String^ message)
{
	auto errorDialog = ref new MessageDialog(message);

	create_task(errorDialog->ShowAsync()).then([](IUICommand^ UICommand)
	{

	});
}


MainPage::MainPage()
{
	InitializeComponent();

	// 设置标题栏颜色
	SetDarkTitleBarColor(
		Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->TitleBar);

	// 在启动时显示设置面板以便打开媒体文件
	this->Splitter->IsPaneOpen = true;
}

void Violet::MainPage::OpenMediaFile(Windows::Storage::StorageFile^ Item)
{
	if (Item)
	{
		this->HeaderTitle->Text = Item->Path;

		using namespace Windows::Media::Core;

		MediaSource ^Source = MediaSource::CreateFromStorageFile(Item);

		using namespace Concurrency;
		using namespace Windows::Storage;
		using namespace Windows::Storage::Streams;



		// Open StorageFile as IRandomAccessStream to be passed to FFmpegInteropMSS
		create_task(Item->OpenAsync(FileAccessMode::Read)).then([this, Item](task<IRandomAccessStream^> stream)
		{
			try
			{
				// Read toggle switches states and use them to setup FFmpeg MSS
				bool forceDecodeAudio = true; //toggleSwitchAudioDecode->IsOn;
				bool forceDecodeVideo = true; //toggleSwitchVideoDecode->IsOn;

											  // Instantiate FFmpegInteropMSS using the opened local file stream
				IRandomAccessStream^ readStream = stream.get();
				FFmpegMSS = FFmpegInteropMSS::CreateFFmpegInteropMSSFromStream(readStream, forceDecodeAudio, forceDecodeVideo);
				if (FFmpegMSS != nullptr)
				{
					MediaStreamSource^ mss = FFmpegMSS->GetMediaStreamSource();

					if (mss)
					{
						// Pass MediaStreamSource to Media Element
						this->MediaPlayerControl->SetMediaStreamSource(mss);

						// Close control panel after file open
						//Splitter->IsPaneOpen = false;
					}
					else
					{
						//DisplayErrorMessage("Cannot open media");
					}
				}
				else
				{
					//DisplayErrorMessage("Cannot open media");
				}
			}
			catch (COMException^ ex)
			{
				//DisplayErrorMessage(ex->Message);
			}
		});


		// Open StorageFile as IRandomAccessStream to be passed to FFmpegInteropMSS
		/*create_task(Item->OpenAsync(FileAccessMode::Read)).then([this, Item, Source](task<IRandomAccessStream^> stream)
		{
		TimedTextSource ^txtsrc = TimedTextSource::CreateFromStream(stream.get());
		Source->ExternalTimedTextSources->Clear();
		Source->ExternalTimedTextSources->Append(txtsrc);
		});*/

		//this->MediaPlayerControl->SetPlaybackSource(Source);

		// 播放开始后隐藏面板
		this->Splitter->IsPaneOpen = false;
	}
}

void Violet::MainPage::MediaPlayerControl_MediaFailed(Platform::Object^ sender, Windows::UI::Xaml::ExceptionRoutedEventArgs^ e)
{
	// 显示错误信息
	//IScream::UI::DisplayErrorMessage(L"视频类型不受支持或文件路径无效");

	// 播放失败显示设置面板以便打开其他的媒体文件
	this->Splitter->IsPaneOpen = true;
}


void Violet::MainPage::TextBox_KeyUp(Platform::Object^ sender, Windows::UI::Xaml::Input::KeyRoutedEventArgs^ e)
{
	String ^URIString = safe_cast<TextBox^>(sender)->Text;

	// Only respond when the text box is not empty and after Enter key is pressed
	if (e->Key == Windows::System::VirtualKey::Enter && !URIString->IsEmpty())
	{
		// Mark event as handled to prevent duplicate event to re-triggered
		e->Handled = true;

		this->HeaderTitle->Text = URIString;

		FFmpegMSS = FFmpegInteropMSS::CreateFFmpegInteropMSSFromUri(URIString, true, true);
		if (FFmpegMSS != nullptr)
		{
			using namespace Windows::Media::Core;
			MediaStreamSource^ mss = FFmpegMSS->GetMediaStreamSource();

			if (mss)
			{
				// Pass MediaStreamSource to Media Element
				this->MediaPlayerControl->SetMediaStreamSource(mss);

				// Close control panel after opening media
				//Splitter->IsPaneOpen = false;
			}
			else
			{
				//DisplayErrorMessage("Cannot open media");
			}
		}
		else
		{
			//DisplayErrorMessage("Cannot open media");
		}

		// 播放开始后隐藏面板
		this->Splitter->IsPaneOpen = false;
	}
}


void Violet::MainPage::AppBarButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	using namespace Concurrency;
	using namespace Windows::Storage;
	using namespace Windows::Storage::Pickers;

	// Create and open the file picker
	FileOpenPicker ^openPicker = ref new FileOpenPicker();
	openPicker->ViewMode = PickerViewMode::Thumbnail;
	openPicker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	openPicker->FileTypeFilter->Append(L"*");

	create_task(openPicker->PickSingleFileAsync()).then([this](StorageFile^ file)
	{
		this->OpenMediaFile(file);
	});
}
