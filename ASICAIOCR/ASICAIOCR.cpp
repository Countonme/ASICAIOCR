#include <windows.h>
#include <commctrl.h>
#include <thread>
#include <atomic>
#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include <opencv2/opencv.hpp>
#include <windowsx.h>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "opencv_world4120d.lib") // 改成你实际的 OpenCV lib

// 全局控件
HWND hwndVideo, hwndLog;
HWND hwndBtnStart, hwndBtnGray, hwndBtnBinary, hwndBtnInvert, hwndBtnGaussian, hwndBtnCanny, hwndBtnOCR;
std::atomic<bool> running{ false };

enum ProcessMode { ORIGIN = 0, GRAY, BINARY, INVERT, GAUSSIAN, CANNY };
std::atomic<ProcessMode> processMode{ ORIGIN };

// ROI
struct ROI {
	RECT rect;
	std::wstring name;
};
std::vector<ROI> roiList;
RECT roiRect = { 0,0,0,0 };
bool roiDrawing = false;
POINT ptStart = { 0,0 };

// 窗口布局常量
const int VIDEO_WIDTH = 640;
const int VIDEO_HEIGHT = 480;
const int RIGHT_WIDTH = 200;
const int LOG_HEIGHT = 150;
const int BTN_HEIGHT = 40;
const int MARGIN = 10;

// 函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AppendLog(const std::wstring& msg);
void VideoThread(HWND hwnd);
BOOL InitControls(HINSTANCE hInstance, HWND hWnd);
void DrawButton(HWND hwnd, HDC hdc);
void ResizeControls(HWND hwnd);
void SaveROITemplate(HWND hwnd);
void LoadROITemplate(HWND hwnd);
std::wstring InputBox(HWND hwnd, const std::wstring& prompt);

// ================= 程序入口 =================
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

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}

// ================= 控件初始化 =================
BOOL InitControls(HINSTANCE hInstance, HWND hWnd)
{
	hwndVideo = CreateWindow(L"STATIC", L"",
		WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
		0, 0, VIDEO_WIDTH, VIDEO_HEIGHT,
		hWnd, nullptr, hInstance, nullptr);

	hwndBtnStart = CreateWindow(L"BUTTON", L"Start/Stop",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)101, hInstance, nullptr);

	hwndBtnGray = CreateWindow(L"BUTTON", L"灰度",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)102, hInstance, nullptr);

	hwndBtnBinary = CreateWindow(L"BUTTON", L"二值化",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)103, hInstance, nullptr);

	hwndBtnInvert = CreateWindow(L"BUTTON", L"反色",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)104, hInstance, nullptr);

	hwndBtnGaussian = CreateWindow(L"BUTTON", L"高斯模糊",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)105, hInstance, nullptr);

	hwndBtnCanny = CreateWindow(L"BUTTON", L"边缘检测",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)106, hInstance, nullptr);

	hwndBtnOCR = CreateWindow(L"BUTTON", L"OCR识别",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)107, hInstance, nullptr);

	hwndLog = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | ES_READONLY,
		0, 0, 0, 0,
		hWnd, nullptr, hInstance, nullptr);

	HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Consolas");
	SendMessage(hwndLog, WM_SETFONT, (WPARAM)hFont, TRUE);

	for (HWND btn : { hwndBtnStart, hwndBtnGray, hwndBtnBinary, hwndBtnInvert, hwndBtnGaussian, hwndBtnCanny, hwndBtnOCR })
		SetWindowLong(btn, GWL_STYLE, GetWindowLong(btn, GWL_STYLE) | BS_OWNERDRAW);

	return TRUE;
}

// ================= 控件布局调整 =================
void ResizeControls(HWND hwnd)
{
	RECT rcClient;
	GetClientRect(hwnd, &rcClient);
	int width = rcClient.right;
	int height = rcClient.bottom;

	MoveWindow(hwndVideo, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, TRUE);

	int xBtn = width - RIGHT_WIDTH + MARGIN;
	int y = MARGIN;
	int btnW = RIGHT_WIDTH - 2 * MARGIN;

	MoveWindow(hwndBtnStart, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnGray, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnBinary, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnInvert, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnGaussian, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnCanny, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnOCR, xBtn, y, btnW, BTN_HEIGHT, TRUE);

	MoveWindow(hwndLog, 0, height - LOG_HEIGHT, width, LOG_HEIGHT, TRUE);
}

// ================= 按钮绘制美化 =================
void DrawButton(HWND hwnd, HDC hdc)
{
	RECT rc; GetClientRect(hwnd, &rc);
	HBRUSH brush = CreateSolidBrush(RGB(48, 197, 195));
	FillRect(hdc, &rc, brush);
	DeleteObject(brush);
	SetBkMode(hdc, TRANSPARENT);

	wchar_t text[128];
	GetWindowText(hwnd, text, 128);
	SetTextColor(hdc, RGB(255, 255, 255));
	DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ================= 日志输出 =================
void AppendLog(const std::wstring& msg)
{
	int len = GetWindowTextLength(hwndLog);
	SendMessage(hwndLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
	SendMessage(hwndLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());
}

// ================= 视频线程 =================
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

		switch (processMode.load())
		{
		case GRAY: cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY); cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR); break;
		case BINARY: cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY); cv::threshold(frame, frame, 128, 255, cv::THRESH_BINARY); cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR); break;
		case INVERT: cv::bitwise_not(frame, frame); break;
		case GAUSSIAN: cv::GaussianBlur(frame, frame, cv::Size(9, 9), 1.5); break;
		case CANNY: { cv::Mat gray, edges; cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY); cv::Canny(gray, edges, 50, 150); cv::cvtColor(edges, frame, cv::COLOR_GRAY2BGR); } break;
		}

		// 绘制所有 ROI
		for (auto& roi : roiList) {
			cv::rectangle(frame, cv::Point(roi.rect.left, roi.rect.top),
				cv::Point(roi.rect.right, roi.rect.bottom),
				cv::Scalar(0, 0, 255), 2);
			cv::putText(frame, std::string(roi.name.begin(), roi.name.end()),
				cv::Point(roi.rect.left, roi.rect.top - 5),
				cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
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

// ================= ROI 保存/加载 =================
void SaveROITemplate(HWND hwnd) {
	wchar_t filename[MAX_PATH] = L"roi_template.txt";
	OPENFILENAME ofn = { sizeof(ofn) };
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = L"ROI 文件 (*.txt)\0*.txt\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"txt";
	ofn.Flags = OFN_OVERWRITEPROMPT;
	if (GetSaveFileName(&ofn)) {
		std::wofstream file(filename);
		for (auto& roi : roiList) {
			file << roi.name << L" "
				<< roi.rect.left << L" " << roi.rect.top << L" "
				<< roi.rect.right << L" " << roi.rect.bottom << L"\n";
		}
		AppendLog(L"ROI 模板已保存\r\n");
	}
}

void LoadROITemplate(HWND hwnd) {
	wchar_t filename[MAX_PATH] = L"";
	OPENFILENAME ofn = { sizeof(ofn) };
	ofn.hwndOwner = hwnd;
	ofn.lpstrFilter = L"ROI 文件 (*.txt)\0*.txt\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"txt";
	ofn.Flags = OFN_FILEMUSTEXIST;
	if (GetOpenFileName(&ofn)) {
		std::wifstream file(filename);
		roiList.clear();
		std::wstring name;
		ROI roi;
		while (file >> name >> roi.rect.left >> roi.rect.top >> roi.rect.right >> roi.rect.bottom) {
			roi.name = name;
			roiList.push_back(roi);
		}
		AppendLog(L"ROI 模板加载成功\r\n");
	}
}

// ================= 输入框 =================
std::wstring InputBox(HWND hwnd, const std::wstring& prompt)
{
	wchar_t input[128] = L"";
	if (DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(101), hwnd,
		[](HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)->INT_PTR {
			switch (msg) {
			case WM_INITDIALOG:
				SetDlgItemText(hDlg, 1001, (LPCWSTR)lParam);
				return TRUE;
			case WM_COMMAND:
				if (LOWORD(wParam) == IDOK) {
					EndDialog(hDlg, 1);
					return TRUE;
				}
				if (LOWORD(wParam) == IDCANCEL) {
					EndDialog(hDlg, 0);
					return TRUE;
				}
			}
			return FALSE;
		}, (LPARAM)prompt.c_str()))
	{
		GetDlgItemText(hwnd, 1001, input, 128);
	}
	return input;
}

// ================= 窗口过程 =================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		InitControls((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), hwnd);

		HMENU hMenu = CreateMenu();
		HMENU hFileMenu = CreatePopupMenu();
		AppendMenu(hFileMenu, MF_STRING, 201, L"保存 ROI 模板...");
		AppendMenu(hFileMenu, MF_STRING, 202, L"加载 ROI 模板...");
		AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"文件");
		SetMenu(hwnd, hMenu);
	}
	break;

	case WM_SIZE:
		ResizeControls(hwnd);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case 101: running = !running; AppendLog(running ? L"视频开始...\r\n" : L"视频停止...\r\n"); if (running) std::thread(VideoThread, hwndVideo).detach(); break;
		case 102: processMode = GRAY; AppendLog(L"灰度模式\r\n"); break;
		case 103: processMode = BINARY; AppendLog(L"二值化模式\r\n"); break;
		case 104: processMode = INVERT; AppendLog(L"反色模式\r\n"); break;
		case 105: processMode = GAUSSIAN; AppendLog(L"高斯模糊模式\r\n"); break;
		case 106: processMode = CANNY; AppendLog(L"边缘检测模式\r\n"); break;
		case 107: AppendLog(L"OCR识别按钮点击\r\n"); break;
		case 201: SaveROITemplate(hwnd); break;
		case 202: LoadROITemplate(hwnd); break;
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
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rcVideo; GetWindowRect(hwndVideo, &rcVideo);
		ScreenToClient(hwnd, (LPPOINT)&rcVideo);
		ScreenToClient(hwnd, ((LPPOINT)&rcVideo) + 1);

		if (PtInRect(&rcVideo, pt)) {
			roiDrawing = true;
			ptStart = pt;
			roiRect = { pt.x, pt.y, pt.x, pt.y };
		}
	}
	break;

	case WM_MOUSEMOVE:
		if (roiDrawing) {
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			roiRect.left = std::min<int>(ptStart.x, x);
			roiRect.top = std::min<int>(ptStart.y, y);
			roiRect.right = std::max<int>(ptStart.x, x);
			roiRect.bottom = std::max<int>(ptStart.y, y);
		}
		break;

	case WM_LBUTTONUP:
		if (roiDrawing) {
			roiDrawing = false;
			ROI roi; roi.rect = roiRect;
			roi.name = L"ROI" + std::to_wstring(roiList.size() + 1);
			roiList.push_back(roi);
			AppendLog(L"添加 ROI: " + roi.name + L"\r\n");
		}
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