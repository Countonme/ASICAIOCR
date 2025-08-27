#include <windows.h>
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <string>

#pragma comment(lib, "opencv_world460.lib") // 根据你的 OpenCV 版本调整

HINSTANCE hInst;
HWND hwndVideo, hwndButton, hwndLog;
std::atomic<bool> running{ false };
cv::VideoCapture cap;

// 辅助函数：向日志追加文字
void AppendLog(const std::wstring& text)
{
	int len = GetWindowTextLength(hwndLog);
	SendMessage(hwndLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
	SendMessage(hwndLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
	SendMessage(hwndLog, EM_SCROLLCARET, 0, 0);
}

// 视频绘制线程
void VideoThread(HWND hwnd)
{
	cv::Mat frame;
	HDC hdc = GetDC(hwnd);
	while (running)
	{
		cap >> frame;
		if (frame.empty()) continue;
		cv::cvtColor(frame, frame, cv::COLOR_BGR2BGRA); // 转32位ARGB
		BITMAPINFO bmi = { 0 };
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = frame.cols;
		bmi.bmiHeader.biHeight = -frame.rows; // 上下翻转
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		StretchDIBits(hdc, 0, 0, frame.cols, frame.rows,
			0, 0, frame.cols, frame.rows,
			frame.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	ReleaseDC(hwnd, hdc);
}

// 初始化控件
BOOL InitControls(HINSTANCE hInstance, HWND hWnd)
{
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	int width = rcClient.right;
	int height = rcClient.bottom;

	hwndVideo = CreateWindow(L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
		0, 0, width / 2, height / 2,
		hWnd, nullptr, hInstance, nullptr);

	hwndButton = CreateWindow(L"BUTTON", L"Start",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		width / 2, 0, width / 2, height / 2,
		hWnd, (HMENU)101, hInstance, nullptr);

	hwndLog = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL,
		0, height / 2, width, height / 2,
		hWnd, nullptr, hInstance, nullptr);

	return TRUE;
}

// 调整控件大小
void ResizeControls(HWND hWnd)
{
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);
	int width = rcClient.right;
	int height = rcClient.bottom;

	MoveWindow(hwndVideo, 0, 0, width / 2, height / 2, TRUE);
	MoveWindow(hwndButton, width / 2, 0, width / 2, height / 2, TRUE);
	MoveWindow(hwndLog, 0, height / 2, width, height / 2, TRUE);
}

// Win32 主窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
		if (LOWORD(wParam) == 101) // 按钮点击
		{
			if (!running)
			{
				running = true;
				AppendLog(L"视频开始...\r\n");
				std::thread(VideoThread, hwndVideo).detach();
			}
			else
			{
				running = false;
				AppendLog(L"视频停止...\r\n");
			}
		}
		break;
	case WM_SIZE:
		ResizeControls(hWnd);
		break;
	case WM_DESTROY:
		running = false;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// 注册窗口类
ATOM MyRegisterClass(HINSTANCE hInstance, LPCWSTR szWindowClass)
{
	WNDCLASSEXW wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
						  WndProc, 0, 0, hInstance,
						  LoadIcon(nullptr, IDI_APPLICATION),
						  LoadCursor(nullptr, IDC_ARROW),
						  (HBRUSH)(COLOR_WINDOW + 1),
						  nullptr, szWindowClass,
						  LoadIcon(nullptr, IDI_APPLICATION) };
	return RegisterClassExW(&wcex);
}

// WinMain
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
	hInst = hInstance;
	LPCWSTR szWindowClass = L"Win32VideoApp";
	LPCWSTR szTitle = L"Win32 视频日志示例";

	MyRegisterClass(hInstance, szWindowClass);

	HWND hWnd = CreateWindowW(szWindowClass, szTitle,
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		800, 600, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd) return FALSE;

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	// 初始化控件
	InitControls(hInstance, hWnd);

	// 打开摄像头
	if (!cap.open(0))
	{
		MessageBox(hWnd, L"无法打开摄像头！", L"错误", MB_OK);
		return -1;
	}

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	cap.release();
	return (int)msg.wParam;
}