#include <windows.h>
#include <commctrl.h>
#include <thread>
#include <atomic>
#include <string>
#include <fstream>
#include <opencv2/opencv.hpp>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "opencv_world4120d.lib") // 改成你实际的 OpenCV lib

HWND hwndVideo, hwndLog;
HWND hwndBtnStart, hwndBtnGray, hwndBtnBinary, hwndBtnInvert, hwndBtnGaussian, hwndBtnCanny, hwndBtnOCR;
std::atomic<bool> running{ false };

enum ProcessMode { ORIGIN = 0, GRAY, BINARY, INVERT, GAUSSIAN, CANNY };
std::atomic<ProcessMode> processMode{ ORIGIN };

// ROI
RECT roiRect = { 0,0,0,0 };
bool roiDrawing = false;
POINT ptStart = { 0,0 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AppendLog(const std::wstring& msg);
void VideoThread(HWND hwnd);
BOOL InitControls(HINSTANCE hInstance, HWND hWnd);
void DrawButton(HWND hwnd, HDC hdc);
void SaveROI();
void LoadROI();

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icex);

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"VideoApp";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(48, 197, 195)); // 松石绿

	RegisterClass(&wc);

	RECT rc = { 0,0,1000,700 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowEx(0, L"VideoApp", L"视频处理示例",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInstance, nullptr);

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	LoadROI();

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	SaveROI();
	return 0;
}

BOOL InitControls(HINSTANCE hInstance, HWND hWnd)
{
	RECT rcClient;
	GetClientRect(hWnd, &rcClient);

	int width = rcClient.right;
	int height = rcClient.bottom;
	int rightWidth = 200;
	int logHeight = 150;
	int btnHeight = 40;
	int margin = 10;

	hwndVideo = CreateWindow(L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
		0, 0, width - rightWidth, height - logHeight,
		hWnd, nullptr, hInstance, nullptr);

	hwndBtnStart = CreateWindow(L"BUTTON", L"Start/Stop",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, margin, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)101, hInstance, nullptr);

	hwndBtnGray = CreateWindow(L"BUTTON", L"灰度",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 60, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)102, hInstance, nullptr);

	hwndBtnBinary = CreateWindow(L"BUTTON", L"二值化",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 110, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)103, hInstance, nullptr);

	hwndBtnInvert = CreateWindow(L"BUTTON", L"反色",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 160, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)104, hInstance, nullptr);

	hwndBtnGaussian = CreateWindow(L"BUTTON", L"高斯模糊",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 210, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)105, hInstance, nullptr);

	hwndBtnCanny = CreateWindow(L"BUTTON", L"边缘检测",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 260, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)106, hInstance, nullptr);

	hwndBtnOCR = CreateWindow(L"BUTTON", L"OCR识别",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		width - rightWidth + margin, 310, rightWidth - 2 * margin, btnHeight,
		hWnd, (HMENU)107, hInstance, nullptr);

	hwndLog = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | ES_READONLY,
		0, height - logHeight, width, logHeight,
		hWnd, nullptr, hInstance, nullptr);

	HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Consolas");
	SendMessage(hwndLog, WM_SETFONT, (WPARAM)hFont, TRUE);

	// 美化按钮背景
	for (HWND btn : { hwndBtnStart, hwndBtnGray, hwndBtnBinary, hwndBtnInvert, hwndBtnGaussian, hwndBtnCanny, hwndBtnOCR })
	{
		SetWindowLong(btn, GWL_STYLE, GetWindowLong(btn, GWL_STYLE) | BS_OWNERDRAW);
	}

	return TRUE;
}

void DrawButton(HWND hwnd, HDC hdc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);

	FillRect(hdc, &rc, CreateSolidBrush(RGB(48, 197, 195))); // 松石绿
	SetBkMode(hdc, TRANSPARENT);

	wchar_t text[128];
	GetWindowText(hwnd, text, 128);
	SetTextColor(hdc, RGB(255, 255, 255));

	DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void AppendLog(const std::wstring& msg)
{
	int len = GetWindowTextLength(hwndLog);
	SendMessage(hwndLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
	SendMessage(hwndLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());
}

void VideoThread(HWND hwnd)
{
	cv::VideoCapture cap(0);
	if (!cap.isOpened())
	{
		AppendLog(L"无法打开相机\r\n");
		return;
	}

	cv::Mat frame;
	HDC hdc = GetDC(hwnd);

	while (running)
	{
		cap >> frame;
		if (frame.empty()) continue;

		// 根据模式处理
		switch (processMode.load())
		{
		case GRAY: cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY); cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR); break;
		case BINARY: cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY); cv::threshold(frame, frame, 128, 255, cv::THRESH_BINARY); cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR); break;
		case INVERT: cv::bitwise_not(frame, frame); break;
		case GAUSSIAN: cv::GaussianBlur(frame, frame, cv::Size(9, 9), 1.5); break;
		case CANNY: { cv::Mat gray, edges; cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY); cv::Canny(gray, edges, 50, 150); cv::cvtColor(edges, frame, cv::COLOR_GRAY2BGR); } break;
		}

		// 绘制 ROI 框
		if (roiRect.right > roiRect.left && roiRect.bottom > roiRect.top)
		{
			cv::rectangle(frame, cv::Point(roiRect.left, roiRect.top), cv::Point(roiRect.right, roiRect.bottom), cv::Scalar(0, 0, 255), 2);
		}

		cv::Mat resized;
		RECT rc; GetClientRect(hwndVideo, &rc);
		cv::resize(frame, resized, cv::Size(rc.right - rc.left, rc.bottom - rc.top));
		cv::cvtColor(resized, resized, cv::COLOR_BGR2BGRA);

		BITMAPINFO bi = { sizeof(BITMAPINFOHEADER), resized.cols, -resized.rows, 1, 32, BI_RGB };
		StretchDIBits(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
			0, 0, resized.cols, resized.rows,
			resized.data, &bi, DIB_RGB_COLORS, SRCCOPY);

		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
	ReleaseDC(hwnd, hdc);
}

void SaveROI()
{
	std::ofstream file("roi.txt");
	file << roiRect.left << " " << roiRect.top << " " << roiRect.right << " " << roiRect.bottom;
}

void LoadROI()
{
	std::ifstream file("roi.txt");
	if (file)
	{
		file >> roiRect.left >> roiRect.top >> roiRect.right >> roiRect.bottom;
	}
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
		InitControls((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), hwnd);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case 101: // Start/Stop
			running = !running;
			AppendLog(running ? L"视频开始...\r\n" : L"视频停止...\r\n");
			if (running) std::thread(VideoThread, hwndVideo).detach();
			break;
		case 102: processMode = GRAY; AppendLog(L"灰度模式\r\n"); break;
		case 103: processMode = BINARY; AppendLog(L"二值化模式\r\n"); break;
		case 104: processMode = INVERT; AppendLog(L"反色模式\r\n"); break;
		case 105: processMode = GAUSSIAN; AppendLog(L"高斯模糊模式\r\n"); break;
		case 106: processMode = CANNY; AppendLog(L"边缘检测模式\r\n"); break;
		case 107: AppendLog(L"OCR识别按钮点击\r\n"); break;
		}
		break;

	case WM_DRAWITEM:
	{
		LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
		DrawButton(lpdis->hwndItem, lpdis->hDC);
	}
	break;

	case WM_LBUTTONDOWN:
	{
		/*	int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);*/
			/*	roiDrawing = true;
				ptStart = { x, y };
				roiRect = { x, y, x, y };*/
	}
	break;

	case WM_MOUSEMOVE:
	{
		if (roiDrawing)
		{
			/*int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			roiRect.left = min(ptStart.x, x);
			roiRect.top = min(ptStart.y, y);
			roiRect.right = max(ptStart.x, x);
			roiRect.bottom = max(ptStart.y, y);*/
			InvalidateRect(hwndVideo, nullptr, TRUE);
		}
	}
	break;

	case WM_LBUTTONUP:
		roiDrawing = false;
		AppendLog(L"ROI 已设置\r\n");
		break;

	case WM_DESTROY:
		running = false;
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}