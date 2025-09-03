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
#include "MvCameraControl.h"
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <mutex>

//MES
#include "SajectWrapper.h"
//弹窗
#include "ModernToast.h"
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "opencv_world4120d.lib") // 改成你实际的 OpenCV lib

#include <iostream>

// 全局声明 系统弹窗
ModernToast* g_pToast = nullptr;
// OCR 引擎
tesseract::TessBaseAPI tess;
// 全局定义 ->处理后的帧，用于 OCR
cv::Mat g_lastProcessedFrame;
// 用于保护 roiList 的访问
std::mutex g_roiMutex;

std::atomic<bool> running{ false };
//Saject MES
SajectConnect mes;
enum ProcessMode { ORIGIN = 0, GRAY, BINARY, INVERT, GAUSSIAN, CANNY };
std::atomic<ProcessMode> processMode{ ORIGIN };
double g_alpha = 1.0; // 对比度
int g_beta = 0;       // 亮度
bool g_flipH = false;
bool g_flipV = false;

cv::Mat g_lastFrame;
std::mutex g_frameMutex;

// ROI 结构体
struct ROI {
	RECT rect;
	std::wstring name;
};
/// <summary>
/// ROI 列表
/// </summary>
std::vector<ROI> roiList;
RECT roiRect = { 0,0,0,0 };
bool roiDrawing = false;
POINT ptStart = { 0,0 };

/// <summary>
/// UTF-8 转 wchar_t（Windows 专用）
/// </summary>
/// <param name="str"></param>
/// <returns></returns>
std::wstring Utf8ToWstring(const std::string& str)
{
	if (str.empty()) return L"";
	int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), NULL, 0);
	std::wstring wstr(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
		(int)str.size(), &wstr[0], sizeNeeded);
	return wstr;
}
// 窗口布局常量
const int VIDEO_WIDTH = 640;
const int VIDEO_HEIGHT = 480;
const int RIGHT_WIDTH = 200;
const int LOG_HEIGHT = 180;
const int BTN_HEIGHT = 40;
const int MARGIN = 10;

// 全局控件
HWND hwndVideo, //视频流
hwndVideo1,     //图像处理窗口
hwndLog;        //日志窗口

/// <summary>
/// 函数声明
/// </summary>
/// <param name="">HWND</param>
/// <param name="">UINT</param>
/// <param name="">WPARAM</param>
/// <param name="">LPARAM</param>
/// <returns></returns>
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
/// <summary>
/// 系统日志
/// </summary>
/// <param name="msg"></param>
void AppendLog(const std::wstring& msg);
/// <summary>
/// OpenCV 本地相机
/// </summary>
/// <param name="hwnd"></param>
void VideoThreadOpenCV(HWND hwnd);
/// <summary>
///海康相机
/// </summary>
/// <param name="hwnd"></param>
void VideoThreadHiK(HWND hwnd);
/// <summary>
/// 初始化
/// </summary>
/// <param name="hInstance"></param>
/// <param name="hWnd"></param>
/// <returns></returns>
BOOL InitControls(HINSTANCE hInstance, HWND hWnd);
/// <summary>
/// 绘制按钮
/// </summary>
/// <param name="hwnd"></param>
/// <param name="hdc"></param>
void DrawButton(HWND hwnd, HDC hdc);
/// <summary>
/// 系统大小改变
/// </summary>
/// <param name="hwnd"></param>
void ResizeControls(HWND hwnd);
/// <summary>
/// 保存ROI
/// </summary>
/// <param name="hwnd"></param>
void SaveROITemplate(HWND hwnd);
/// <summary>
/// 加载ROI
/// </summary>
/// <param name="hwnd"></param>
void LoadROITemplate(HWND hwnd);

/// <summary>
/// OCR 处理模块
/// </summary>
//void OCRWith();
/// <summary>
/// 清空日志
/// </summary>
//void ClearLog();

/// <summary>
/// 创建按钮与Layout
/// </summary>
/// <param name="hwnd"></param>
void CreateMenuandLayout(HWND hwnd);

/// <summary>
/// 按钮事件处理
/// </summary>
/// <param name="hwnd"></param>
/// <param name="msg"></param>
/// <param name="wParam"></param>
/// <param name="lParam"></param>
/// <returns></returns>
int ButtonEventWithForWM_COMMAND(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/// <summary>
/// 输入框
/// </summary>
/// <param name="hwnd"></param>
/// <param name="prompt"></param>
/// <returns></returns>
std::wstring InputBox(HWND hwnd, const std::wstring& prompt);

///获取当前 ROI 内的图像
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
/// <summary>
/// ================= 程序入口 =================
/// </summary>
/// <param name="hInstance"></param>
/// <param name=""></param>
/// <param name=""></param>
/// <param name="nCmdShow"></param>
/// <returns></returns>
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
	// 初始化OCR 模组
	// 后台线程初始化 OCR
	std::thread([]() {
		if (tess.Init("C:/Program Files/Tesseract-OCR/tessdata", "eng+chi_sim")) {
			MessageBoxW(nullptr, L"Tesseract 初始化失败\r\n", L"OCR 初始化失败", MB_OK | MB_ICONINFORMATION);
		}
		// OCR 识别 ROI 区域
		tess.SetPageSegMode(tesseract::PSM_SINGLE_BLOCK); // 单行/单块文字模式，可改成 PSM_AUTO
		tess.SetVariable("tessedit_char_blacklist", "|"); // 可选：去掉干扰字符
		}).detach();
	//tess.SetVariable("tessedit_char_whitelist",
	//	"0123456789.VA=⎓");  // 同时允许 = 和 ⎓

	//tess.SetVariable("tessedit_char_whitelist",
	//	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
	//	":.~"
	//	"⎓⎍"); // 添加直流符号

	//// 1. 获取当前可执行文件的完整路径
	//wchar_t exePath[MAX_PATH] = { 0 };
	//GetModuleFileNameW(nullptr, exePath, MAX_PATH);

	//// 2. 转成 std::wstring 方便操作
	//std::wstring pathW(exePath);

	//// 3. 可选：只获取目录（不含文件名）
	//// std::wstring dir = pathW.substr(0, pathW.find_last_of(L"\\/"));

	//// 4. 弹出消息框显示路径
	//MessageBoxW(
	//	nullptr,
	//	L"",                    // 显示完整路径
	//	L"程序执行路径",                   // 标题
	//	MB_OK | MB_ICONINFORMATION
	//);
	//// 初始化（加载 DLL）
	//if (!mes.Initialize("SajetConnect.dll")) {  // 注意：字符串前加 L 表示宽字符
	//	// 弹出错误对话框
	//	MessageBoxW(nullptr, L"初始化 SajectConnect 失败！", L"错误", MB_OK | MB_ICONERROR);
	//	return -1;
	//}

	//// 调用流程
	//if (!mes.SajetTransStart()) {
	//	MessageBoxW(nullptr, L"SajetTransStart 失败！", L"错误", MB_OK | MB_ICONERROR);
	//	return -1;
	//}
	//INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
	//InitCommonControlsEx(&icex);

	WNDCLASS wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"VideoApp";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(48, 197, 195)); // 松石绿

	WNDCLASS wc2 = {};
	wc2.lpfnWndProc = DefWindowProc; // 或者你自己写的 WndProc
	wc2.hInstance = hInstance;
	wc2.lpszClassName = L"STATICVideo";
	wc2.hbrBackground = CreateSolidBrush(RGB(148, 197, 195)); // 松石绿
	wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);
	RegisterClass(&wc2);
	//默认窗体大小
	RECT rc = { 0,0,1288,960 };
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
	hwndVideo = CreateWindow(L"STATIC", L"视频流",
		WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
		0, 0, VIDEO_WIDTH, VIDEO_HEIGHT,
		hWnd, nullptr, hInstance, nullptr);

	hwndVideo1 = CreateWindow(L"STATICVideo", L"图像比对",
		WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
		VIDEO_WIDTH + 4, 2, VIDEO_WIDTH, VIDEO_HEIGHT,
		hWnd, nullptr, hInstance, nullptr);

	hwndLog = CreateWindow(L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | ES_READONLY,
		0, 0, 0, 0,
		hWnd, nullptr, hInstance, nullptr);

	HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Consolas");
	SendMessage(hwndLog, WM_SETFONT, (WPARAM)hFont, TRUE);

	return TRUE;
}

// ================= 控件布局调整 =================
void ResizeControls(HWND hwnd)
{
	RECT rcClient;
	GetClientRect(hwnd, &rcClient);
	int width = rcClient.right;
	int height = rcClient.bottom;

	MoveWindow(hwndVideo, 1, 2, VIDEO_WIDTH, VIDEO_HEIGHT, TRUE);
	MoveWindow(hwndVideo1, VIDEO_WIDTH + 4, 2, VIDEO_WIDTH, VIDEO_HEIGHT, TRUE);

	int xBtn = width - RIGHT_WIDTH + MARGIN;
	int y = MARGIN;
	int btnW = RIGHT_WIDTH - 2 * MARGIN;

	MoveWindow(hwndLog, 0, height - LOG_HEIGHT, width, LOG_HEIGHT, TRUE);
}

/// <summary>
///  ================= 按钮绘制美化 =================
/// </summary>
/// <param name="hwnd">hwnd</param>
/// <param name="hdc">hdc</param>
void DrawButton(HWND hwnd, HDC hdc)
{
	RECT rc; GetClientRect(hwnd, &rc);
	//按钮背景颜色
	//LITEON 松石绿 主题
	HBRUSH brush = CreateSolidBrush(RGB(148, 200, 150));
	FillRect(hdc, &rc, brush);
	DeleteObject(brush);
	SetBkMode(hdc, TRANSPARENT);

	wchar_t text[128];
	GetWindowText(hwnd, text, 128);
	SetTextColor(hdc, RGB(255, 255, 255));
	DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/// <summary>
///  ================= 日志输出 =================
/// </summary>
/// <param name="msg"></param>
void AppendLog(const std::wstring& msg)
{
	int len = GetWindowTextLength(hwndLog);
	SendMessage(hwndLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
	SendMessage(hwndLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());
}

/// <summary>
/// 清空日志
/// </summary>
void ClearLog()
{
	SetWindowText(hwndLog, L"");
}

/// <summary>
///  ================= 基于视频Opencv  =================
/// </summary>
/// <param name="hwnd"></param>
void VideoThreadOpenCV(HWND hwnd)
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

		std::lock_guard<std::mutex> lock(g_frameMutex);
		g_lastFrame = frame.clone();

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

		// 保存处理后的图像给 OCR 用
	// ... 在处理完 frame 之后 ...

// 将原始帧和处理后的帧同时安全地保存到共享变量
		{
			//std::lock_guard<std::mutex> lock(g_frameMutex);
			g_lastProcessedFrame = frame.clone();  // 保存给 OCR 使用
		} // 锁在此处自动释放

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

/// <summary>
/// ###########海康USB 相机##############
/// </summary>
/// <param name="hwnd"></param>

void VideoThreadHiK(HWND hwnd)
{
	int nRet = MV_OK;
	void* handle = nullptr;
	MV_CC_DEVICE_INFO_LIST deviceList = { 0 };

	// 枚举设备
	nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &deviceList);
	if (MV_OK != nRet || deviceList.nDeviceNum == 0) {
		AppendLog(L"未发现海康工业相机\r\n");
		return;
	}

	// 创建句柄并打开设备
	nRet = MV_CC_CreateHandle(&handle, deviceList.pDeviceInfo[0]);
	if (MV_OK != nRet) { AppendLog(L"创建句柄失败\r\n"); return; }
	nRet = MV_CC_OpenDevice(handle);
	if (MV_OK != nRet) { AppendLog(L"打开相机失败\r\n"); return; }

	MV_CC_StartGrabbing(handle);

	HDC hdcWindow = GetDC(hwnd);
	cv::Mat frame;

	while (running)
	{
		MV_FRAME_OUT stOutFrame = { 0 };
		nRet = MV_CC_GetImageBuffer(handle, &stOutFrame, 1000);
		if (nRet != MV_OK) continue;

		// 转为 OpenCV Mat
		cv::Mat img(stOutFrame.stFrameInfo.nHeight,
			stOutFrame.stFrameInfo.nWidth,
			CV_8UC1, stOutFrame.pBufAddr);
		cv::cvtColor(img, frame, cv::COLOR_BayerBG2BGR);

		// 图像处理
		if (g_flipH) cv::flip(frame, frame, 1);
		if (g_flipV) cv::flip(frame, frame, 0);

		switch (processMode)
		{
		case 1: cv::cvtColor(frame, frame, cv::COLOR_BGR2GRAY); break;
		case 2: cv::threshold(frame, frame, 128, 255, cv::THRESH_BINARY); break;
		case 3: cv::bitwise_not(frame, frame); break;
		case 4: cv::GaussianBlur(frame, frame, cv::Size(5, 5), 0); break;
		case 5: {
			cv::Mat gray, edges; cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
			cv::Canny(gray, edges, 50, 150); cv::cvtColor(edges, frame, cv::COLOR_GRAY2BGR);
		} break;
		}

		frame.convertTo(frame, -1, g_alpha, g_beta);

		// 保存处理后的图像给 OCR 用
		{
			std::lock_guard<std::mutex> lock(g_frameMutex);
			g_lastProcessedFrame = frame.clone(); // ✅ OCR 用
		}

		// 获取窗口客户区大小
		RECT rc; GetClientRect(hwnd, &rc);
		int winW = rc.right - rc.left;
		int winH = rc.bottom - rc.top;

		// 缩放显示
		cv::Mat display;
		cv::resize(frame, display, cv::Size(winW, winH));
		cv::cvtColor(display, display, cv::COLOR_BGR2BGRA);

		auto drawRect = [&](cv::Mat& img, const RECT& r, const cv::Scalar& color) {
			int left = int(std::min(r.left, r.right));
			int right = int(std::max(r.left, r.right));
			int top = int(std::min(r.top, r.bottom));
			int bottom = int(std::max(r.top, r.bottom));
			cv::rectangle(img, cv::Point(left, top), cv::Point(right, bottom), color, 2);
			};

		// 绘制所有 ROI
		{
			std::lock_guard<std::mutex> lock(g_roiMutex);
			for (auto& roi : roiList) {
				drawRect(display, roi.rect, cv::Scalar(0, 0, 255));

				std::wstring name = roi.name;
				std::string text(name.begin(), name.end());
				int left = int(std::min(roi.rect.left, roi.rect.right));
				int top = int(std::min(roi.rect.top, roi.rect.bottom));
				cv::putText(display, text, cv::Point(left, top - 5),
					cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
			}

			// 绘制正在绘制的 ROI
			if (roiDrawing) {
				drawRect(display, roiRect, cv::Scalar(255, 0, 0));
			}
		}

		// 显示到窗口
		BITMAPINFO bi = { sizeof(BITMAPINFOHEADER), display.cols, -display.rows, 1, 32, BI_RGB };
		StretchDIBits(hdcWindow, 0, 0, display.cols, display.rows,
			0, 0, display.cols, display.rows,
			display.data, &bi, DIB_RGB_COLORS, SRCCOPY);

		MV_CC_FreeImageBuffer(handle, &stOutFrame);
	}

	MV_CC_StopGrabbing(handle);
	MV_CC_CloseDevice(handle);
	MV_CC_DestroyHandle(handle);
	ReleaseDC(hwnd, hdcWindow);
}

/// <summary>
/// ================= 清空 ROI =================
/// </summary>
/// <param name="hwnd"></param>
void ClearROITemplate(HWND hwnd)
{
	roiList.clear();          // 清空所有 ROI
	AppendLog(L"已清空所有 ROI\r\n");
	InvalidateRect(hwndVideo, nullptr, TRUE); // 立即刷新视频窗口，去掉残留矩形
}

/// <summary>
/// ================= ROI 保存/加载 =================
/// </summary>
/// <param name="hwnd"></param>
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
/// <summary>
/// 加载ROI Temp
/// </summary>
/// <param name="hwnd"></param>
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
/// <summary>
/// OCR 处理
/// </summary>
void OCRWith()
{
	AppendLog(L"OCR识别按钮点击\r\n");

	cv::Mat frameCopy;
	{
		std::lock_guard<std::mutex> lock(g_frameMutex);
		if (g_lastProcessedFrame.empty()) {
			AppendLog(L"没有可用的帧进行OCR\r\n");
			return;
		}
		frameCopy = g_lastProcessedFrame.clone();
	}

	// ---------------- 全图 OCR ----------------
	cv::Mat gray;
	cv::cvtColor(frameCopy, gray, cv::COLOR_BGR2GRAY);
	cv::adaptiveThreshold(gray, gray, 255,
		cv::ADAPTIVE_THRESH_GAUSSIAN_C,
		cv::THRESH_BINARY, 25, 15);
	if (cv::mean(gray)[0] > 127) cv::bitwise_not(gray, gray);

	tess.SetImage(gray.data, gray.cols, gray.rows, 1, gray.step);
	tess.Recognize(0);

	tesseract::ResultIterator* ri = tess.GetIterator();
	tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;

	if (ri != nullptr) {
		do {
			const char* line = ri->GetUTF8Text(level);
			if (line) {
				AppendLog(Utf8ToWstring(line) + L"\r\n");
			}

			int x1, y1, x2, y2;
			if (ri->BoundingBox(level, &x1, &y1, &x2, &y2)) {
				x1 = std::max(0, x1); y1 = std::max(0, y1);
				x2 = std::min(frameCopy.cols - 1, x2);
				y2 = std::min(frameCopy.rows - 1, y2);
				cv::rectangle(frameCopy, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 255, 0), 2);
			}
		} while (ri->Next(level));
	}

	// ---------------- 全图二维码识别 ----------------
	cv::QRCodeDetector qrDecoder;
	std::vector<cv::Point> qrPoints;
	std::string qrData = qrDecoder.detectAndDecode(frameCopy, qrPoints);

	if (!qrData.empty() && qrPoints.size() == 4) {
		AppendLog(L"全图 QRCode结果: " + Utf8ToWstring(qrData) + L"\r\n");

		std::vector<std::vector<cv::Point>> contours;
		contours.push_back(qrPoints);
		cv::polylines(frameCopy, contours, true, cv::Scalar(0, 0, 255), 2);

		cv::putText(frameCopy, qrData, qrPoints[0] - cv::Point(0, 10),
			cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);
	}

	// ---------------- 显示全图到 STATICVideo ----------------
	RECT rc;
	GetClientRect(hwndVideo1, &rc);
	cv::Mat resized;
	cv::resize(frameCopy, resized, cv::Size(rc.right - rc.left, rc.bottom - rc.top));
	cv::cvtColor(resized, resized, cv::COLOR_BGR2BGRA);

	HDC hdc = GetDC(hwndVideo1);
	BITMAPINFO bi = { sizeof(BITMAPINFOHEADER), resized.cols, -resized.rows, 1, 32, BI_RGB };
	StretchDIBits(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
		0, 0, resized.cols, resized.rows,
		resized.data, &bi, DIB_RGB_COLORS, SRCCOPY);
	ReleaseDC(hwndVideo1, hdc);
}

/// <summary>
/// ================= 窗口过程 =================
/// </summary>
/// <param name="hwnd"></param>
/// <param name="msg"></param>
/// <param name="wParam"></param>
/// <param name="lParam"></param>
/// <returns></returns>
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		/// <summary>
		/// 创建按钮与Layout
		/// </summary>
		/// <param name="hwnd"></param>
		CreateMenuandLayout(hwnd);
		break;
	}

	case WM_SIZE:
	{
		ResizeControls(hwnd);
		break;
	}

	case WM_COMMAND:
	{
		return ButtonEventWithForWM_COMMAND(hwnd, msg, wParam, lParam);
	}

	case WM_DRAWITEM:
	{
		LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
		DrawButton(lpdis->hwndItem, lpdis->hDC);
		break;
	}

	case WM_LBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rcVideo;
		GetWindowRect(hwndVideo, &rcVideo);
		MapWindowPoints(nullptr, hwnd, (LPPOINT)&rcVideo, 2);

		if (PtInRect(&rcVideo, pt)) {
			roiDrawing = true;
			ptStart = pt;
			roiRect = { pt.x, pt.y, pt.x, pt.y };
		}
		break;
	}

	case WM_MOUSEMOVE:
	{
		if (roiDrawing) {
			int x = GET_X_LPARAM(lParam);
			int y = GET_Y_LPARAM(lParam);
			roiRect.left = std::min<int>(ptStart.x, x);
			roiRect.top = std::min<int>(ptStart.y, y);
			roiRect.right = std::max<int>(ptStart.x, x);
			roiRect.bottom = std::max<int>(ptStart.y, y);
			//InvalidateRect(hwndVideo, nullptr, FALSE); // 刷新绘制
		}
		break;
	}

	case WM_LBUTTONUP:
	{
		if (roiDrawing) {
			roiDrawing = false;
			if (roiRect.right - roiRect.left > 10 && roiRect.bottom - roiRect.top > 10) {
				ROI roi;
				roi.rect = roiRect;
				roi.name = L"ROI" + std::to_wstring(roiList.size() + 1);
				roiList.push_back(roi);
				AppendLog(L"新建 ROI: " + roi.name + L"\r\n");
			}
		}
		break;
	}

	case WM_RBUTTONDOWN:
	{
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rcVideo;
		GetWindowRect(hwndVideo, &rcVideo);

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
		break;
	}

	case WM_DESTROY:
	{
		running = false;
		PostQuitMessage(0);
		break;
	}
	default:
	{
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	}
	return 0;
}

/// <summary>
/// 按钮事件处理
/// </summary>
/// <param name="hwnd"></param>
/// <param name="msg"></param>
/// <param name="wParam"></param>
/// <param name="lParam"></param>
/// <returns></returns>
int ButtonEventWithForWM_COMMAND(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (LOWORD(wParam))
	{
	case 101: // Start/Stop
	{
		running = !running;
		AppendLog(running ? L"视频开始...\r\n" : L"视频停止...\r\n");
		if (running)

			//std::thread(VideoThreadOpenCV, hwndVideo).detach();
			std::thread(VideoThreadHiK, hwndVideo).detach();
		break;
	}

	case 102:
	{
		processMode = GRAY;
		AppendLog(L"灰度模式\r\n");
		break;
	}

	case 103:
	{
		processMode = BINARY;
		AppendLog(L"二值化模式\r\n");
		break;
	}

	case 104:
	{
		processMode = INVERT;
		AppendLog(L"反色模式\r\n");
		break;
	}

	case 105:
	{
		processMode = GAUSSIAN;
		AppendLog(L"高斯模糊模式\r\n");
		break;
	}
	case 106:
	{
		processMode = CANNY;
		AppendLog(L"边缘检测模式\r\n");
		break;
	}

	case 107: // OCR + 全图二维码识别
	{
		std::thread([]() {
			OCRWith();
			}).detach();
		break;
	}

	case 108:
	{
		g_flipH = !g_flipH;
		break;
	}
	case 109:
	{
		g_flipV = !g_flipV;
		break;
	}
	case 110:
	{
		g_beta += 20;
		break;      // 亮度+
	}
	case 111:
	{
		g_beta -= 20;
		break;      // 亮度-
	}
	case 112:
	{
		g_alpha *= 1.1;
		break;      // 对比度+
	}

	case 113:
	{
		g_alpha *= 0.9;
		break;      // 对比度-
	}

	case 201:
	{
		SaveROITemplate(hwnd);
		break;
	}

	case 202:
	{
		LoadROITemplate(hwnd);
		break;
	}

	case 205:
	{
		PostQuitMessage(0);
		break;
	}

	case 301:
		/* 撤销逻辑 */
	{
		break;
	}

	case 302:
		/* 重做逻辑 */
	{
		break;
	}

	case 303:
		/* 复制逻辑 */
	{
		break;
	}

	case 304:
		/* 粘贴逻辑 */
	{
		break;
	}

	case 305:
	{
		ClearROITemplate(hwnd);
		break;
	}

	case 306:
	{
		ClearLog();
		break;
	}

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

/// <summary>
/// 创建按钮与Layout
/// </summary>
/// <param name="hwnd"></param>
void CreateMenuandLayout(HWND hwnd)
{
	InitControls((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), hwnd);

	HMENU hMenu = CreateMenu();
	HMENU hFileMenu = CreatePopupMenu();
	HMENU hEditMenu = CreatePopupMenu(); // 新的“编辑”菜单
	HMENU hHelpMenu = CreatePopupMenu(); // 新的“帮助”菜单
	HMENU hFunctionMenu = CreatePopupMenu();//功能菜单
	HMENU hStartMenu = CreatePopupMenu();//启动菜单
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
	AppendMenu(hEditMenu, MF_STRING, 306, L"清除All日志");

	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"编辑(&E)");
	//相机启动菜单
	AppendMenu(hMenu, MF_POPUP, 101, L"启动(&F5)");
	AppendMenu(hMenu, MF_STRING, 107, L"OCR(&O)");
	//功能菜单
	AppendMenu(hFunctionMenu, MF_STRING, 102, L"灰度(&A)");
	AppendMenu(hFunctionMenu, MF_STRING, 103, L"二值化(&B)");
	AppendMenu(hFunctionMenu, MF_STRING, 104, L"反色(&C)");
	AppendMenu(hFunctionMenu, MF_STRING, 105, L"高斯模糊(&D)");
	AppendMenu(hFunctionMenu, MF_STRING, 106, L"边缘检测(&E)");
	AppendMenu(hFunctionMenu, MF_STRING, 107, L"OCR(&O)");
	AppendMenu(hFunctionMenu, MF_STRING, 108, L"水平翻转(&G)");
	AppendMenu(hFunctionMenu, MF_STRING, 109, L"垂直翻转(&H)");
	AppendMenu(hFunctionMenu, MF_STRING, 110, L"亮度 + (&I)");
	AppendMenu(hFunctionMenu, MF_STRING, 111, L"亮度 -(&J)");
	AppendMenu(hFunctionMenu, MF_STRING, 112, L"对比度 +(&K)");
	AppendMenu(hFunctionMenu, MF_STRING, 113, L"对比度 -(&L)");
	AppendMenu(hFunctionMenu, MF_STRING, 114, L"二维码读取(&Q&R)");

	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFunctionMenu, L"图像处理功能(&U)");

	// 帮助菜单
	AppendMenu(hHelpMenu, MF_STRING, 401, L"关于(&A)..."); // 常用的“关于”对话框
	AppendMenu(hHelpMenu, MF_STRING, 402, L"帮助主题(&H)...");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, L"帮助(&H)");

	SetMenu(hwnd, hMenu);
}