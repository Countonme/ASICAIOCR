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

#include <iomanip>
#include <filesystem>
#include <windows.h>
//MES
#include "SajectWrapper.h"
//弹窗
#include "ModernToast.h"
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "opencv_world4120d.lib") // 改成你实际的 OpenCV lib

#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
//Zxing 条码识别
#include <ZXing/ReadBarcode.h>
#include <ZXing/TextUtfEncoding.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/DecodeHints.h>
// 单词拼写检查
#include "WordSpellChecker.h"
/// <summary>
/// 全局声明 系统弹窗
/// </summary>
ModernToast* g_pToast = nullptr;
// OCR 引擎
tesseract::TessBaseAPI tess;
/// <summary>
///  全局定义：处理后的帧，用于 OCR
/// </summary>
cv::Mat g_lastProcessedFrame;
/// <summary>
///  全局变量：用于保护 roiList 的访问
/// </summary>
std::mutex g_roiMutex;
/// <summary>
/// 全局变量：控制是否显示参考线
/// </summary>
bool g_showReferenceLines = false;
/// <summary>
/// 全局变量：控制目标是否要绘制ROI
/// </summary>
bool g_showROI = false;
// 全局变量:在函数开头或者全局声明 自定义词典
WordSpellChecker g_spellChecker("dictionary.txt");

HMENU hMenu = nullptr; // 全局
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
/// UTF-8 转 wchar_t（Windows 专用）  C++17后的写法
/// </summary>
/// <param name="str"></param>
/// <returns></returns>
// wstring -> UTF-8
inline std::string wstringToUtf8(const std::wstring& wstr)
{
	if (wstr.empty()) return {};
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
	return strTo;
}

// UTF-8 -> wstring
inline std::wstring utf8ToWstring(const std::string& str)
{
	if (str.empty()) return {};
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

/// <summary>
/// 获取所有 ROI 区域的 OpenCV Rect
/// </summary>
/// <param name="frame"></param>
/// <returns></returns>
std::vector<cv::Rect> GetROIRects(const cv::Mat& frame)
{
	std::vector<cv::Rect> result;
	for (const auto& roi : roiList)
	{
		int x = roi.rect.left;
		int y = roi.rect.top;
		int w = roi.rect.right - roi.rect.left;
		int h = roi.rect.bottom - roi.rect.top;

		// 防止越界
		cv::Rect r(x, y, w, h);
		r &= cv::Rect(0, 0, frame.cols, frame.rows);
		if (r.width > 0 && r.height > 0)
			result.push_back(r);
	}
	return result;
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
void OCRWith();
/// <summary>
/// 清空日志
/// </summary>
void ClearLog();

/// <summary>
/// 创建按钮与Layout
/// </summary>
/// <param name="hwnd"></param>
void CreateMenuandLayout(HWND hwnd);

/// <summary>
/// 绘制目标ROI 区域
/// </summary>
/// <param name="roiRects"></param>
/// <param name="frameCopy"></param>
/// <returns></returns>
cv::Mat DrawTheTargetROI(std::vector<cv::Rect>  roiRects, cv::Mat frameCopy);
/// <summary>
/// 按钮事件处理
/// </summary>
/// <param name="hwnd"></param>
/// <param name="msg"></param>
/// <param name="wParam"></param>
/// <param name="lParam"></param>
/// <returns></returns>
int ButtonEventWithForWM_COMMAND(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/**
 * @brief 自动保存图像，按日期_序号.png 命名
 * @param img 要保存的图像
 * @param saveDir 保存目录（默认 SavedImages）
 * @return 是否成功
 */
bool SaveVideo1ImageAuto(const cv::Mat& img);

/**
 * 在图像上绘制错词及候选词
 * frame       : 要绘制的图像
 * wrongWord   : 错词
 * suggestions : 候选词
 * x, y       : 绘制起点坐标
 */
void DrawWrongWordOnFrame(cv::Mat& frame, const std::wstring& wrongWord, const std::vector<std::wstring>& suggestions, int x, int y);
/// <summary>
/// 输入框
/// </summary>
/// <param name="hwnd"></param>
/// <param name="prompt"></param>
/// <returns></returns>
std::wstring InputBox(HWND hwndParent, const std::wstring& prompt);

/// <summary>
/// 二维识别OpenCV
/// </summary>
/// <param name="img"></param>

cv::Mat detectAndDrawQRCode(cv::Mat& img);

/// <summary>
/// 条码识别ZXing
/// </summary>
/// <param name="img"></param>
/// <returns></returns>
cv::Mat detectAndDrawQRCodeZXing(cv::Mat& img);

/// <summary>
/// 输入框
/// </summary>
/// <param name="hwndParent"></param>
/// <param name="prompt"></param>
/// <returns></returns>
std::wstring InputBox(HWND hwndParent, const std::wstring& prompt)
{
	static std::wstring result; // 静态保存内容，方便重复访问

	// 如果已经创建过窗口，就直接返回
	static HWND hwndDlg = nullptr;
	static HWND hwndEdit = nullptr;
	static HWND hwndBtn = nullptr;

	if (!hwndDlg)
	{
		hwndDlg = CreateWindowExW(
			WS_EX_DLGMODALFRAME,
			L"STATIC",           // 临时类名
			prompt.c_str(),
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT, CW_USEDEFAULT, 400, 150,
			hwndParent,
			NULL,
			GetModuleHandle(NULL),
			NULL
		);

		hwndEdit = CreateWindowExW(
			0, L"EDIT", L"",
			WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
			20, 50, 360, 25,
			hwndDlg,
			(HMENU)1,
			GetModuleHandle(NULL),
			NULL
		);

		hwndBtn = CreateWindowExW(
			0, L"BUTTON", L"OK",
			WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
			150, 90, 80, 30,
			hwndDlg,
			(HMENU)2,
			GetModuleHandle(NULL),
			NULL
		);

		ShowWindow(hwndDlg, SW_SHOW);
		UpdateWindow(hwndDlg);
	}

	// 这里不要 DestroyWindow
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_COMMAND && msg.hwnd == hwndBtn)
		{
			wchar_t buffer[256] = { 0 };
			GetWindowTextW(hwndEdit, buffer, 256);
			result = buffer;
			// 不关闭窗口
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return result; // 返回当前输入框内容
}

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
	wc2.hbrBackground = CreateSolidBrush(RGB(156, 39, 176)); // 紫色
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
		case ORIGIN:
			// 原图，什么都不做
			g_alpha = 1.0; // 对比度
			g_beta = 0;       // 亮度
			g_flipH = false;
			g_flipV = false;
			break;
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

		// 执行 resize
		GetClientRect(hwndVideo, &rc);
		cv::resize(frame, resized, cv::Size(rc.right - rc.left, rc.bottom - rc.top));

		// ========== 绘制参考线 ==========
		if (g_showReferenceLines) {
			int width = resized.cols;
			int height = resized.rows;

			cv::line(resized,
				cv::Point(width / 2, 0),
				cv::Point(width / 2, height),
				cv::Scalar(0, 0, 255), // BGR: 红色
				2);

			cv::line(resized,
				cv::Point(0, height / 2),
				cv::Point(width, height / 2),
				cv::Scalar(0, 0, 255), // BGR: 红色
				2);
		}
		// ========== 绘制参考线 结束 ==========
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
		AppendLog(L"未发现海康工业相机\r\n,正在嘗試打開本地相機");
		std::thread(VideoThreadOpenCV, hwndVideo).detach();
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
		case 0:
			// 原图，什么都不做
			break;
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

		// 获取窗口客户区大小
		RECT rc; GetClientRect(hwnd, &rc);
		int winW = rc.right - rc.left;
		int winH = rc.bottom - rc.top;

		// 缩放显示
		cv::Mat display;
		cv::resize(frame, display, cv::Size(winW, winH));
		cv::cvtColor(display, display, cv::COLOR_BGR2BGRA);

		// 保存处理后的图像给 OCR 用
		{
			std::lock_guard<std::mutex> lock(g_frameMutex);
			g_lastProcessedFrame = display.clone(); // ✅ OCR 用
		}
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

		// ========== 绘制参考线 ==========
		if (g_showReferenceLines) {
			int width = display.cols;
			int height = display.rows;

			cv::line(display,
				cv::Point(width / 2, 0),
				cv::Point(width / 2, height),
				cv::Scalar(0, 0, 255), // BGR: 红色
				2);

			cv::line(display,
				cv::Point(0, height / 2),
				cv::Point(width, height / 2),
				cv::Scalar(0, 0, 255), // BGR: 红色
				2);
		}
		// ========== 绘制参考线 结束 ==========

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
/// OCR 处理并只显示当前 ROI 区域到 hwndVideo1
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
	//frameCopy = detectAndDrawQRCodeZXing(frameCopy);
	// 获取所有 ROI 区域的 OpenCV Rect
	std::vector<cv::Rect> roiRects = GetROIRects(frameCopy);
	if (roiRects.empty()) {
		// 如果没有 ROI，则使用全图
		roiRects.push_back(cv::Rect(0, 0, frameCopy.cols, frameCopy.rows));
	}

	// 遍历每个 ROI 进行 OCR 识别
	for (size_t i = 0; i < roiRects.size(); ++i) {
		cv::Rect roiRect = roiRects[i];
		cv::Mat roi = frameCopy(roiRect); // 提取 ROI
		// ========== 绘制所有原始的 ROI 区域 判断g_showROI是否等True ==========
		// 使用一个不同的颜色（藍色）来绘制原始ROI，以便与文字区域区分开
		if (g_showROI) {
			DrawTheTargetROI(roiRects, frameCopy);
		}
		detectAndDrawQRCodeZXing(roi);
		// 灰度化
		cv::Mat gray;
		cv::cvtColor(roi, gray, cv::COLOR_BGR2GRAY);

		// 自适应二值化
		cv::adaptiveThreshold(gray, gray, 255,
			cv::ADAPTIVE_THRESH_GAUSSIAN_C,
			cv::THRESH_BINARY, 25, 15);
		if (cv::mean(gray)[0] > 127) cv::bitwise_not(gray, gray);

		// OCR
		tess.SetImage(gray.data, gray.cols, gray.rows, 1, gray.step);
		tess.Recognize(0);

		//tesseract::ResultIterator* ri = tess.GetIterator();
		//tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;

		AppendLog(L"ROI " + std::to_wstring(i + 1) + L" OCR结果:\r\n");

		tesseract::ResultIterator* ri = tess.GetIterator();
		tesseract::PageIteratorLevel level = tesseract::RIL_WORD;

		if (ri != nullptr) {
			do {
				const char* word_utf8 = ri->GetUTF8Text(level);
				float conf = ri->Confidence(level); // 置信度，可以用来做过滤
				if (word_utf8) {
					std::string word_str(word_utf8);
					std::wstring wword = utf8ToWstring(word_str);

					// 检查拼写
					bool correct = g_spellChecker.isWordCorrect(word_str);

					int x1, y1, x2, y2;
					if (ri->BoundingBox(level, &x1, &y1, &x2, &y2)) {
						// 将相对 roi 的坐标转换为全局坐标
						x1 += roiRect.x;
						y1 += roiRect.y;
						x2 += roiRect.x;
						y2 += roiRect.y;

						// 边界修正
						x1 = std::max(0, std::min(x1, frameCopy.cols - 1));
						y1 = std::max(0, std::min(y1, frameCopy.rows - 1));
						x2 = std::max(0, std::min(x2, frameCopy.cols - 1));
						y2 = std::max(0, std::min(y2, frameCopy.rows - 1));

						if (x1 < x2 && y1 < y2) {
							// 绘制矩形：绿色=正确，红色=错误
							cv::Scalar color = correct ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
							cv::rectangle(frameCopy, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

							// 在框上方绘制文字
							int baseline = 0;
							cv::Size textSize = cv::getTextSize(word_str, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
							cv::Point textOrg(x1, y1 - 5);

							cv::putText(frameCopy, word_str, textOrg, cv::FONT_HERSHEY_SIMPLEX,
								0.5, color, 1, cv::LINE_AA);

							// 如果错误，附加候选词
							if (!correct) {
								auto suggestions = g_spellChecker.getSuggestions(word_str, 1);
								if (!suggestions.empty()) {
									std::string sug = "-> " + suggestions[0]; // 只显示第一个建议
									cv::putText(frameCopy, sug, cv::Point(x1, y2 + 15),
										cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1, cv::LINE_AA);
								}
							}
						}
					}
					delete[] word_utf8; // 释放 tesseract 分配的字符串
				}
			} while (ri->Next(level));
		}
	}

	// 显示处理过的 frameCopy 到 STATICVideo
// 显示处理过的 frameCopy 到 STATICVideo
	RECT rc;
	GetClientRect(hwndVideo1, &rc);
	cv::Mat resized;
	cv::resize(frameCopy, resized, cv::Size(rc.right - rc.left, rc.bottom - rc.top));
	cv::cvtColor(resized, resized, cv::COLOR_BGR2BGRA);

	// ========== 添加当前时间到右下角 ==========
	// 获取当前时间
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	std::tm tm_snapshot;
	localtime_s(&tm_snapshot, &now_time); // 使用 localtime_s 保证线程安全

	// 格式化时间字符串：YYYY-MM-DD HH:MM:SS
	char timeStr[64];
	std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tm_snapshot);

	// 文本参数
	cv::Scalar textColor = cv::Scalar(0, 255, 0, 255); // 绿色 (B, G, R, A)
	double fontScale = 0.6;
	int thickness = 1;
	int fontFace = cv::FONT_HERSHEY_SIMPLEX;

	// 获取文本尺寸
	cv::Size textSize = cv::getTextSize(timeStr, fontFace, fontScale, thickness, nullptr);

	// 设置文本位置（右下角，留出边距）
	int margin = 10;
	int x = resized.cols - textSize.width - margin;
	int y = resized.rows - margin; // 文本基线在底部，所以用 rows - margin

	// 绘制半透明背景（可选，提高可读性）
	cv::Rect bgRect(x - 5, y - textSize.height - 5, textSize.width + 10, textSize.height + 10);
	if (bgRect.x >= 0 && bgRect.y >= 0 && bgRect.br().x <= resized.cols && bgRect.br().y <= resized.rows) {
		cv::Mat roi = resized(bgRect);
		cv::Mat overlay;
		cv::addWeighted(roi, 0.7, cv::Mat(roi.size(), roi.type(), cv::Scalar(0, 0, 0, 255)), 0.3, 0, overlay);
		overlay.copyTo(roi);
	}

	// 绘制文字
	cv::putText(resized, timeStr, cv::Point(x, y), fontFace, fontScale, textColor, thickness, cv::LINE_AA);
	// ========== 时间绘制结束 ==========

	// ========== 自动保存图像 ==========
	SaveVideo1ImageAuto(resized);  //
	// 显示到窗口
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
				std::wstring customName;
				// 弹出输入框让用户输入 ROI 名称
			/*	std::wstring customName = InputBox(hwnd, L"请输入 ROI 名称：");
				if (customName.empty()) {*/
				// 如果用户没输入，就用默认名字
				customName = L"ROI" + std::to_wstring(roiList.size() + 1);
				//}
				roi.name = customName;
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
	case 100:
	{
		processMode = ORIGIN;
		AppendLog(L"原图模式\r\n");
		break;
	}
	case 101: // Start/Stop
	{
		running = !running;
		AppendLog(running ? L"视频开始...\r\n" : L"视频停止...\r\n");
		ModifyMenu(hMenu, 101, MF_BYCOMMAND | MF_STRING, 101, running ? L"⏹ 停止(&F5)" : L"▶️ 启动(&F5)");
		DrawMenuBar(hwnd); // 刷新菜单栏
		if (running)
			//	std::thread(VideoThreadOpenCV, hwndVideo).detach();
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
	case 114:
	{//二维码
		break;
	}
	case 115:
	{
		// 切换参考线的显示状态
		g_showReferenceLines = !g_showReferenceLines;

		break;
	}
	case 116:
	{  //目标ROI显示切换
		g_showROI = !g_showROI;
		break;
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

	hMenu = CreateMenu();

	HMENU hFileMenu = CreatePopupMenu();
	HMENU hEditMenu = CreatePopupMenu(); // 新的“编辑”菜单
	HMENU hHelpMenu = CreatePopupMenu(); // 新的“帮助”菜单
	HMENU hFunctionMenu = CreatePopupMenu();//功能菜单
	HMENU hStartMenu = CreatePopupMenu();//启动菜单
	// 文件菜单
	AppendMenu(hFileMenu, MF_STRING, 201, L"💾 保存 ROI 模板...");
	AppendMenu(hFileMenu, MF_STRING, 202, L"📂 加载 ROI 模板...");
	AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hFileMenu, MF_STRING, 205, L"🚪 退出(&X)");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"📁 文件(&F)");

	// 编辑菜单
	AppendMenu(hEditMenu, MF_STRING, 301, L"↩️ 撤销(&U)");
	AppendMenu(hEditMenu, MF_STRING, 302, L"↪️ 重做(&R)");
	AppendMenu(hEditMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(hEditMenu, MF_STRING, 303, L"📋 复制(&C)");
	AppendMenu(hEditMenu, MF_STRING, 304, L"📌 粘贴(&P)");
	AppendMenu(hEditMenu, MF_STRING, 305, L"🗑️ 清除All ROI (&D)");
	AppendMenu(hEditMenu, MF_STRING, 306, L"📝 清除All日志");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hEditMenu, L"✏️ 编辑(&E)");

	AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); // 分隔一下
	AppendMenu(hMenu, MF_STRING, 101, L"▶️ 启动(&F5)");
	AppendMenu(hMenu, MF_STRING, 107, L"📄 OCR (&O)"); // 这个重复了，建议删除或改为子菜单

	// 功能菜单
// --- 图像处理模式 ---
	AppendMenu(hFunctionMenu, MF_STRING, 100, L"🖼️ 原圖(&O)");      // O for Original
	AppendMenu(hFunctionMenu, MF_STRING, 102, L"🔳 灰度(&G)");      // G for Gray
	AppendMenu(hFunctionMenu, MF_STRING, 103, L"⚖️ 二值化(&B)");    // B for Binary
	AppendMenu(hFunctionMenu, MF_STRING, 104, L"🔄 反色(&I)");      // I for Invert
	AppendMenu(hFunctionMenu, MF_STRING, 105, L"🌀 高斯模糊(&U)");  // U for Blur (模糊 U? 不理想，但 B 已被占用)
	AppendMenu(hFunctionMenu, MF_STRING, 106, L"🔍 边缘检测(&E)");  // E for Edge

	// --- 分隔符 ---
	AppendMenu(hFunctionMenu, MF_SEPARATOR, 0, NULL);

	// --- 翻转 ---
	AppendMenu(hFunctionMenu, MF_STRING, 108, L"↔️ 水平翻转(&F)");  // F for Flip
	AppendMenu(hFunctionMenu, MF_STRING, 109, L"↕️ 垂直翻转(&V)");  // V for Vertical

	// --- 分隔符 ---
	AppendMenu(hFunctionMenu, MF_SEPARATOR, 0, NULL);

	// --- 亮度与对比度 ---
	AppendMenu(hFunctionMenu, MF_STRING, 110, L"🔆 亮度 + (&L)");   // L for Lightness/Brightness
	AppendMenu(hFunctionMenu, MF_STRING, 111, L"🔅 亮度 -(&M)");    // M for Minus (或 B-)
	AppendMenu(hFunctionMenu, MF_STRING, 112, L"📈 对比度 +(&C)");  // C for Contrast
	AppendMenu(hFunctionMenu, MF_STRING, 113, L"📉 对比度 -(&T)");  // T for Too much? 不理想，但 C- 需要字母

	// --- 分隔符 ---
	AppendMenu(hFunctionMenu, MF_SEPARATOR, 0, NULL);

	// --- 工具 ---
	AppendMenu(hFunctionMenu, MF_STRING, 107, L"🔤 OCR(&R)");       // R for Recognition
	AppendMenu(hFunctionMenu, MF_STRING, 114, L"🧩 二维码读取(&Q)");  // Q for QR Code
	AppendMenu(hFunctionMenu, MF_STRING, 115, L"➕ 参考线(&N)");     // N for Normal Line / Guide
	AppendMenu(hFunctionMenu, MF_STRING, 116, L"🔲 目标ROI显示(&D)"); // D for Display ROI / Region

	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFunctionMenu, L"⚙️ 图像处理功能(&U)");

	// 帮助菜单
	AppendMenu(hHelpMenu, MF_STRING, 401, L"ℹ️ 关于(&A)...");
	AppendMenu(hHelpMenu, MF_STRING, 402, L"❓ 帮助主题(&H)...");
	AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelpMenu, L"❓ 帮助(&H)");

	SetMenu(hwnd, hMenu);
}

/// <summary>
/// 绘制目标ROI 区域
/// </summary>
/// <param name="roiRects"></param>
/// <param name="frameCopy"></param>
/// <returns></returns>
cv::Mat DrawTheTargetROI(std::vector<cv::Rect>  roiRects, cv::Mat frameCopy)
{
	for (const auto& rect : roiRects) {
		cv::rectangle(frameCopy,
			cv::Point(rect.x, rect.y),
			cv::Point(rect.x + rect.width, rect.y + rect.height),
			cv::Scalar(255, 0, 0), // BGR: 藍色
			2); // 线宽
	}
	return frameCopy;
}

/// <summary>
/// 条码识别OpenCV
/// </summary>
/// <param name="img"></param>

cv::Mat detectAndDrawQRCode(cv::Mat& img) {
	// 创建QR码检测器
	cv::QRCodeDetector qrDecoder = cv::QRCodeDetector();

	// 检测并解码QR码
	cv::Mat points; std::string decoded_info;
	decoded_info = qrDecoder.detectAndDecode(img, points);

	if (decoded_info.length() > 0) {
		// 打印解码信息
		std::cout << "Decoded Data : " << decoded_info << std::endl;

		// 绘制定位QR码的点
		for (int i = 0; i < points.rows; i++) {
			cv::line(img, cv::Point(points.at<double>(i, 0), points.at<double>(i, 1)),
				cv::Point(points.at<double>((i + 1) % 4, 0), points.at<double>((i + 1) % 4, 1)),
				cv::Scalar(255, 0, 0), 3);
		}
	}
	else {
		std::cerr << "No QR code could be detected or decoded." << std::endl;
	}
	return img;
}

/// <summary>
/// 条码识别ZXing
/// </summary>
/// <param name="img"></param>
/// <returns></returns>
cv::Mat detectAndDrawQRCodeZXing(cv::Mat& img) {
	cv::Mat roiGray = img;
	if (!roiGray.isContinuous()) {
		roiGray = roiGray.clone();  // 保证连续内存
	}

	ZXing::ImageView zimg(roiGray.data, roiGray.cols, roiGray.rows, ZXing::ImageFormat::Lum);

	// 显示结果
	//cv::imshow("QR Code", img);
	//cv::waitKey(0);
	auto result = ZXing::ReadBarcode(zimg, ZXing::DecodeHints().setTryHarder(true));
	if (result.isValid()) {
		// 直接使用 result.text()，已经是 std::string
		std::string decoded_info = result.text();
		std::cout << "Decoded Data: " << decoded_info << std::endl;
		AppendLog(L"Decoded Data: " + utf8ToWstring(decoded_info));
		// 绘制边框
		auto points = result.position();
		if (!points.empty() && points.size() >= 4) {
			for (size_t i = 0; i < points.size(); i++) {
				cv::line(img,
					cv::Point(points[i].x, points[i].y),
					cv::Point(points[(i + 1) % points.size()].x, points[(i + 1) % points.size()].y),
					cv::Scalar(255, 0, 0), 3);
			}
		}
	}
	else {
		AppendLog(L"No QR code could be detected or decoded.");
		//std::cerr << "No QR code could be detected or decoded." << std::endl;
	}

	return img;
}
// 检查目录是否存在
bool MyDirectoryExists(const std::string& path) {
	DWORD attr = GetFileAttributesA(path.c_str());
	return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// 创建目录
bool MyCreateDirectoryIfNotExists(const std::string& path) {
	if (!MyDirectoryExists(path)) {
		BOOL result = CreateDirectoryA(path.c_str(), NULL);
		return result != 0;
	}
	return true;
}

// 检查文件是否存在
bool MyFileExists(const std::string& path) {
	DWORD attr = GetFileAttributesA(path.c_str());
	return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

/**
 * @brief 自动保存图像，按日期_序号.png 命名
 * @param img 要保存的图像
 * @param saveDir 保存目录（默认 SavedImages）
 * @return 是否成功
 */
bool SaveVideo1ImageAuto(const cv::Mat& img) {
	const std::string& saveDir = "SavedImages";
	if (img.empty()) {
		OutputDebugStringA("SaveVideo1ImageAuto: 图像为空，无法保存。\n");
		return false;
	}

	// 创建保存目录
	if (!MyCreateDirectoryIfNotExists(saveDir)) {
		OutputDebugStringA("SaveVideo1ImageAuto: 创建目录失败。\n");
		return false;
	}

	// 获取当前日期：YYYYMMDD
	SYSTEMTIME st;
	GetLocalTime(&st);
	std::ostringstream dateStream;
	dateStream << st.wYear
		<< std::setfill('0') << std::setw(2) << st.wMonth
		<< std::setfill('0') << std::setw(2) << st.wDay;
	std::string dateStr = dateStream.str();

	// 查找当天已存在的文件数量，避免覆盖
	int fileCount = 1;
	std::string filename;
	do {
		std::ostringstream filenameStream;
		filenameStream << saveDir << "/" << dateStr << "_"
			<< std::setfill('0') << std::setw(3) << fileCount << ".png";
		filename = filenameStream.str();
		fileCount++;
	} while (MyFileExists(filename) && fileCount <= 1000);

	// 保存图像
	bool success = cv::imwrite(filename, img);
	if (success) {
		std::string msg = "图像已自动保存: " + filename + "\n";
		OutputDebugStringA(msg.c_str());
	}
	else {
		OutputDebugStringA("SaveVideo1ImageAuto: 保存图像失败！\n");
	}

	return success;
}

/**
 * 在图像上绘制错词及候选词
 * frame       : 要绘制的图像
 * wrongWord   : 错词
 * suggestions : 候选词
 * x, y       : 绘制起点坐标
 */
void DrawWrongWordOnFrame(cv::Mat& frame, const std::wstring& wrongWord, const std::vector<std::wstring>& suggestions, int x, int y)
{
	// 将 wstring 转成 string（UTF-8）
	std::string wStrUtf8(wrongWord.begin(), wrongWord.end());
	std::string sugStrUtf8;
	for (const auto& s : suggestions) {
		sugStrUtf8 += std::string(s.begin(), s.end()) + " ";
	}

	// 绘制错词 (红色)
	cv::putText(frame, wStrUtf8, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 255), 2);

	// 绘制候选词 (蓝色)
	cv::putText(frame, "-> " + sugStrUtf8, cv::Point(x + 200, y), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 0, 0), 1);
}