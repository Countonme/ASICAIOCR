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

//#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "opencv_world4120d.lib") // 改成你实际的 OpenCV lib

// 全局控件
HWND hwndVideo, hwndLog;
HWND hwndBtnStart,
hwndBtnGray,// 灰度按钮
hwndBtnBinary,// 二值化按钮
hwndBtnInvert,// 反色按钮
hwndBtnGaussian,// 高斯模糊按钮
hwndBtnCanny,// 图像处理按钮
hwndBtnOCR, //OCR 按钮
hwndBtnBrightUp,// 亮度按钮
hwndBtnBrightDown,// 亮度按钮
hwndBtnContrastUp,// 亮度对比度按钮
hwndBtnContrastDown,// 亮度对比度按钮
hwndBtnFlipH,// 水平翻转按钮
hwndBtnFlipV;// 垂直翻转按钮

std::atomic<bool> running{ false };

enum ProcessMode { ORIGIN = 0, GRAY, BINARY, INVERT, GAUSSIAN, CANNY };
std::atomic<ProcessMode> processMode{ ORIGIN };
double g_alpha = 1.0; // 对比度
int g_beta = 0;       // 亮度
bool g_flipH = false;
bool g_flipV = false;
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
const int LOG_HEIGHT = 180;
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

// ====== 新增 ======
// 获取当前 ROI 内的图像
std::vector<cv::Mat> GetROIImages(const cv::Mat& frame) {
	std::vector<cv::Mat> result;
	for (auto& roi : roiList) {
		cv::Rect r(roi.rect.left, roi.rect.top,
			roi.rect.right - roi.rect.left,
			roi.rect.bottom - roi.rect.top);
		r &= cv::Rect(0, 0, frame.cols, frame.rows);
		if (r.width > 0 && r.height > 0)
			result.push_back(frame(r).clone());
	}
	return result;
}

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

	RECT rc = { 0,0,1024,768 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hwnd = CreateWindowEx(0, L"VideoApp", L"自动化 AI OCR ",
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

	hwndBtnFlipH = CreateWindow(L"BUTTON", L"水平翻转",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)108, hInstance, nullptr);

	hwndBtnFlipV = CreateWindow(L"BUTTON", L"垂直翻转",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)109, hInstance, nullptr);

	hwndBtnBrightUp = CreateWindow(L"BUTTON", L"亮度+",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)110, hInstance, nullptr);

	hwndBtnBrightDown = CreateWindow(L"BUTTON", L"亮度-",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
		0, 0, 0, 0,
		hWnd, (HMENU)111, hInstance, nullptr);

	hwndLog = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | ES_READONLY,
		0, 0, 0, 0,
		hWnd, nullptr, hInstance, nullptr);

	HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Consolas");
	SendMessage(hwndLog, WM_SETFONT, (WPARAM)hFont, TRUE);

	for (HWND btn : {
		hwndBtnStart,
			hwndBtnGray,
			hwndBtnBinary,
			hwndBtnInvert,
			hwndBtnGaussian,
			hwndBtnCanny,
			hwndBtnOCR,
			hwndBtnFlipH,
			hwndBtnFlipV,
			hwndBtnBrightUp,
			hwndBtnBrightDown})
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

	MoveWindow(hwndVideo, 2, 2, VIDEO_WIDTH, VIDEO_HEIGHT, TRUE);

	int xBtn = width - RIGHT_WIDTH + MARGIN;
	int y = MARGIN;
	int btnW = RIGHT_WIDTH - 2 * MARGIN;

	MoveWindow(hwndBtnStart, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnGray, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnBinary, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnInvert, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnGaussian, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnCanny, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnOCR, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnFlipH, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnFlipV, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnBrightUp, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnBrightDown, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnContrastUp, xBtn, y, btnW, BTN_HEIGHT, TRUE); y += 50;
	MoveWindow(hwndBtnContrastDown, xBtn, y, btnW, BTN_HEIGHT, TRUE);

	MoveWindow(hwndLog, 0, height - LOG_HEIGHT, width, LOG_HEIGHT, TRUE);
}

// ================= 按钮绘制美化 =================
void DrawButton(HWND hwnd, HDC hdc)
{
	RECT rc; GetClientRect(hwnd, &rc);
	//按钮背景颜色
	HBRUSH brush = CreateSolidBrush(RGB(148, 200, 150));
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
		// 亮度/对比度
		frame.convertTo(frame, -1, g_alpha, g_beta);

		// 翻转
		if (g_flipH) cv::flip(frame, frame, 1);
		if (g_flipV) cv::flip(frame, frame, 0);

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

		// 绘制正在绘制的 ROI
		if (roiDrawing) {
			cv::rectangle(frame, cv::Point(roiRect.left, roiRect.top),
				cv::Point(roiRect.right, roiRect.bottom),
				cv::Scalar(255, 0, 0), 2);
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

// ================= 清空 ROI =================
void ClearROITemplate(HWND hwnd)
{
	roiList.clear();          // 清空所有 ROI
	AppendLog(L"已清空所有 ROI\r\n");
	InvalidateRect(hwndVideo, nullptr, TRUE); // 立即刷新视频窗口，去掉残留矩形
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
		HMENU hEditMenu = CreatePopupMenu(); // 新的“编辑”菜单
		HMENU hHelpMenu = CreatePopupMenu(); // 新的“帮助”菜单

		// 文件菜单
		AppendMenu(hFileMenu, MF_STRING, 201, L"保存 ROI 模板...");
		AppendMenu(hFileMenu, MF_STRING, 202, L"加载 ROI 模板...");
		AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hFileMenu, MF_STRING, 205, L"退出(&X)");
		AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"文件(&F)");

		// 编辑菜单
		AppendMenu(hEditMenu, MF_STRING, 301, L"撤销(&U)");
		AppendMenu(hEditMenu, MF_STRING, 302, L"重做(&R)");
		AppendMenu(hEditMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hEditMenu, MF_STRING, 303, L"复制(&C)");
		AppendMenu(hEditMenu, MF_STRING, 304, L"粘贴(&P)");
		AppendMenu(hEditMenu, MF_STRING, 305, L"清除All ROI (&D)");
		AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"编辑(&E)");

		// 帮助菜单
		AppendMenu(hHelpMenu, MF_STRING, 401, L"关于(&A)..."); // 常用的“关于”对话框
		AppendMenu(hHelpMenu, MF_STRING, 402, L"帮助主题(&H)...");
		AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, L"帮助(&H)");

		SetMenu(hwnd, hMenu);
	} break;
	break;

	case WM_SIZE:
		ResizeControls(hwnd);
		break;

	case WM_COMMAND:
	{
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
		case 107: // OCR
			AppendLog(L"OCR识别按钮点击\r\n");
			// 这里可以调用 Tesseract OCR
			break;
		case 108: g_flipH = !g_flipH; break; // 水平翻转
		case 109: g_flipV = !g_flipV; break; // 垂直翻转
		case 110: g_beta += 20; break;      // 亮度+
		case 111: g_beta -= 20; break;      // 亮度-
		case 112: g_alpha *= 1.1; break;    // 对比度+
		case 113: g_alpha *= 0.9; break;    // 对比度-

		case 201: SaveROITemplate(hwnd); break;
		case 202: LoadROITemplate(hwnd); break;
		case 205: PostQuitMessage(0); break;
		case 301: /* 撤销逻辑 */ break;
		case 302: /* 重做逻辑 */ break;
		case 303: /* 复制逻辑 */ break;
		case 304: /* 粘贴逻辑 */ break;
		case 305:ClearROITemplate(hwnd); break;
		}
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
		MapWindowPoints(nullptr, hwnd, (LPPOINT)&rcVideo, 2);

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

	case WM_RBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rcVideo; GetWindowRect(hwndVideo, &rcVideo);
		MapWindowPoints(nullptr, hwnd, (LPPOINT)&rcVideo, 2);

		// 判断是否点击在已有 ROI 内
		auto it = std::find_if(roiList.begin(), roiList.end(),
			[pt](const ROI& r) {
				return pt.x >= r.rect.left && pt.x <= r.rect.right &&
					pt.y >= r.rect.top && pt.y <= r.rect.bottom;
			});
		if (it != roiList.end()) {
			AppendLog(L"删除 ROI: " + it->name + L"\r\n");
			roiList.erase(it);
		}
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