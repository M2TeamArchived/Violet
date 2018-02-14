//
// MainPage.xaml.cpp
// MainPage 类的实现。
//

#include "pch.h"
#include "MainPage.xaml.h"

#include "M2AsyncHelpers.h"

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

using namespace VioletCore;

using namespace Windows::UI;
using namespace Windows::UI::ViewManagement;

using Platform::Box;
using Platform::IBox;




// 设置暗黑风标题栏颜色
void SetDarkTitleBarColor(
	_In_ ApplicationViewTitleBar ^TitleBar)
{
	

	IBox<Color>^ ColorWhite = ref new Box<Color>(Colors::White);
	IBox<Color>^ ColorBlack = ref new Box<Color>(Colors::Black);
	IBox<Color>^ ColorGray = ref new Box<Color>(Colors::Gray);
	
	// 标题栏

	TitleBar->ForegroundColor = ColorWhite;
	TitleBar->BackgroundColor = ColorBlack;

	// 非活动状态标题栏

	TitleBar->InactiveForegroundColor = ColorGray;
	TitleBar->InactiveBackgroundColor = ColorBlack;

	// 标题栏按钮

	TitleBar->ButtonForegroundColor = ColorWhite;
	TitleBar->ButtonBackgroundColor = ColorBlack;

	// 非活动状态标题栏按钮

	TitleBar->ButtonInactiveForegroundColor = ColorWhite;
	TitleBar->ButtonInactiveBackgroundColor = ColorBlack;

	// 当指针位于标题栏按钮

	TitleBar->ButtonHoverForegroundColor = ColorWhite;
	TitleBar->ButtonHoverBackgroundColor = ColorGray;
}

using namespace Windows::UI::Popups;

// 显示错误信息
void DisplayErrorMessage(Platform::String^ message)
{
	auto errorDialog = ref new MessageDialog(message);

	errorDialog->ShowAsync();
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
		using namespace Windows::Media::Core;
		using namespace Windows::Storage;
		using namespace Windows::Storage::Streams;

		try
		{
			IRandomAccessStream^ readStream = M2AsyncWait(Item->OpenAsync(FileAccessMode::Read));
			
			// Read toggle switches states and use them to setup FFmpeg MSS

			bool IsSuccess = false;

			// Instantiate FFmpegInteropMSS using the opened local file stream
			MSSObject = VioletCoreMSS::CreateFromStream(readStream);
			if (MSSObject != nullptr)
			{
				MediaStreamSource^ mss = MSSObject->GetMediaStreamSource();

				if (mss)
				{
					// Pass MediaStreamSource to Media Element
					M2ExecuteOnUIThread([this, mss, Item]()
					{
						this->HeaderTitle->Text = Item->Path;
						this->MediaPlayerControl->SetMediaStreamSource(mss);

						// 播放开始后隐藏面板
						this->Splitter->IsPaneOpen = false;
					});

					IsSuccess = true;
				}
			}

			if (!IsSuccess)
			{
				DisplayErrorMessage("Cannot open media");
			}
		}
		catch (COMException^ ex)
		{
			DisplayErrorMessage(ex->Message);
		}
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

		bool IsSuccess = false;

		MSSObject = VioletCoreMSS::CreateFromUri(URIString);
		if (MSSObject != nullptr)
		{
			using namespace Windows::Media::Core;
			MediaStreamSource^ mss = MSSObject->GetMediaStreamSource();

			if (mss)
			{
				// Pass MediaStreamSource to Media Element
				M2ExecuteOnUIThread([this, mss, URIString]()
				{
					this->HeaderTitle->Text = URIString;
					this->MediaPlayerControl->SetMediaStreamSource(mss);

					// 播放开始后隐藏面板
					this->Splitter->IsPaneOpen = false;
				});
					
				IsSuccess = true;
			}
		}
		
		if (!IsSuccess)
		{
			DisplayErrorMessage("Cannot open media");
		}

		// 播放开始后隐藏面板
		this->Splitter->IsPaneOpen = false;
	}
}


void Violet::MainPage::AppBarButton_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	using namespace Windows::Storage;
	using namespace Windows::Storage::Pickers;

	// Create and open the file picker
	FileOpenPicker ^openPicker = ref new FileOpenPicker();
	openPicker->ViewMode = PickerViewMode::Thumbnail;
	openPicker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
	openPicker->FileTypeFilter->Append(L"*");

	IAsyncOperation<StorageFile^>^ Operation = openPicker->PickSingleFileAsync();

	M2::CThread([this, Operation]()
	{
		StorageFile^ File = M2AsyncWait(Operation);

		if (nullptr != File)
		{
			this->OpenMediaFile(File);
		}
	});;
}
