// ModernToast.cpp
#include "ModernToast.h"
#include <shellapi.h>
#include <uxtheme.h> // 可选：用于DwmExtendFrameIntoClientArea（需要链接 comctl32）
#pragma comment(lib, "uxtheme.lib")

// 静态成员
ATOM ModernToast::RegisterWindowClass(HINSTANCE hInst) {
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"ModernToastWindow";
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr; // 自绘
	wc.hIcon = LoadIcon(nullptr, IDI_INFORMATION);
	wc.hIconSm = LoadIcon(nullptr, IDI_INFORMATION);

	return RegisterClassExW(&wc);
}

LRESULT CALLBACK ModernToast::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	ModernToast* pThis = nullptr;

	if (msg == WM_NCCREATE) {
		pThis = static_cast<ModernToast*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
	}
	else {
		pThis = (ModernToast*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
	}

	if (pThis && msg == WM_CREATE) {
		// 创建关闭按钮（X）
		CreateWindowW(L"BUTTON", L"✕",
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
			0, 0, 30, 30,
			hwnd, (HMENU)1, pThis->hInstance, nullptr);
		return 0;
	}
	else if (pThis && msg == WM_DRAWITEM && wParam == 1) {
		LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
		FillRect(pdis->hDC, &pdis->rcItem, GetSysColorBrush(COLOR_BTNFACE));
		SetBkMode(pdis->hDC, TRANSPARENT);
		DrawTextW(pdis->hDC, L"✕", -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}
	else if (pThis && msg == WM_COMMAND && LOWORD(wParam) == 1) {
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
		return 0;
	}
	else if (msg == WM_CLOSE) {
		if (pThis) pThis->Close();
		return 0;
	}
	else if (msg == WM_PAINT) {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		pThis->DrawContent(hdc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	else if (msg == WM_LBUTTONDOWN) {
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

ModernToast::ModernToast(HINSTANCE hInst)
	: hInstance(hInst), hwnd(nullptr), durationMs(3000), position(TOP_RIGHT), style(INFO) {
	static bool registered = false;
	if (!registered) {
		RegisterWindowClass(hInst);
		registered = true;
	}
	SetStyleColors();
}

ModernToast::~ModernToast() {
	Close();
	if (hBgBrush) DeleteObject(hBgBrush);
}

ModernToast& ModernToast::SetMessage(const std::wstring& msg) {
	message = msg;
	return *this;
}

ModernToast& ModernToast::SetTitle(const std::wstring& title) {
	this->title = title;
	return *this;
}

ModernToast& ModernToast::SetDuration(int ms) {
	durationMs = ms;
	return *this;
}

ModernToast& ModernToast::SetPosition(Position pos) {
	position = pos;
	return *this;
}

ModernToast& ModernToast::SetStyle(Style style) {
	this->style = style;
	SetStyleColors();
	return *this;
}

ModernToast& ModernToast::SetCustomColors(COLORREF bg, COLORREF text, COLORREF topBar) {
	bgColor = bg;
	textColor = text;
	this->topBarColor = topBar;
	if (hBgBrush) DeleteObject(hBgBrush);
	hBgBrush = CreateSolidBrush(bgColor);
	return *this;
}

void ModernToast::SetStyleColors() {
	switch (style) {
	case INFO:
		topBarColor = RGB(33, 150, 243);  // 蓝色
		bgColor = RGB(240, 248, 255);    // 浅蓝背景
		textColor = RGB(30, 30, 30);
		break;
	case WARNING:
		topBarColor = RGB(255, 193, 7);  // 橙色
		bgColor = RGB(255, 248, 220);
		textColor = RGB(50, 50, 50);
		break;
	case ERROR_MSG:
		topBarColor = RGB(244, 67, 54);  // 红色
		bgColor = RGB(255, 235, 238);
		textColor = RGB(70, 0, 0);
		break;
	case SUCCESS:
		topBarColor = RGB(76, 175, 80);  // 绿色
		bgColor = RGB(232, 245, 233);
		textColor = RGB(0, 50, 0);
		break;
	}
	if (hBgBrush) DeleteObject(hBgBrush);
	hBgBrush = CreateSolidBrush(bgColor);
}

void ModernToast::DrawContent(HDC hdc) {
	RECT rect;
	GetClientRect(hwnd, &rect);

	// 绘制顶部条
	HBRUSH hTopBar = CreateSolidBrush(topBarColor);
	RECT topRect = { 0, 0, rect.right, 8 };
	FillRect(hdc, &topRect, hTopBar);
	DeleteObject(hTopBar);

	// 绘制背景
	FillRect(hdc, &rect, hBgBrush);

	// 绘制图标（可扩展）
	HICON hIcon = nullptr;
	switch (style) {
	case INFO:    hIcon = LoadIcon(nullptr, IDI_INFORMATION); break;
	case WARNING: hIcon = LoadIcon(nullptr, IDI_WARNING); break;
	case ERROR_MSG:   hIcon = LoadIcon(nullptr, IDI_ERROR); break;
	case SUCCESS: hIcon = LoadIcon(nullptr, IDI_INFORMATION); break;
	}
	if (hIcon) {
		DrawIconEx(hdc, 15, (rect.bottom - 32) / 2, hIcon, 32, 32, 0, nullptr, DI_NORMAL);
	}

	// 绘制文字
	std::wstring text = title.empty() ? message : (title + L"\n" + message);
	SetTextColor(hdc, textColor);
	SetBkMode(hdc, TRANSPARENT);
	RECT textRect = { 60, 10, rect.right - 40, rect.bottom - 10 };
	DrawTextW(hdc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
}

void ModernToast::Show() {
	if (IsVisible()) Close();

	const int width = 350;
	const int height = 120;

	hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		L"ModernToastWindow",
		L"Toast",
		WS_POPUP | WS_BORDER,
		0, 0, width, height,
		nullptr, nullptr, hInstance,
		this
	);

	if (!hwnd) return;

	// 计算位置
	int sx = GetSystemMetrics(SM_CXSCREEN);
	int sy = GetSystemMetrics(SM_CYSCREEN);
	int x = 0, y = 0;

	switch (position) {
	case CENTER:
		x = (sx - width) / 2;
		y = (sy - height) / 2;
		break;
	case BOTTOM_LEFT:
		x = 20;
		y = sy - height - 20;
		break;
	case TOP_RIGHT:
		x = sx - width - 20;
		y = 80; // 偏移一些，避免任务栏
		break;
	}

	// 滑入动画（可选）
	SetWindowPos(hwnd, HWND_TOPMOST, x, sy, width, height, SWP_NOACTIVATE);
	AnimateWindow(hwnd, 300, AW_SLIDE | AW_ACTIVATE);

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	// 自动关闭
	if (durationMs > 0) {
		std::thread([this]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
			if (IsVisible()) {
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
			}
			}).detach();
	}
}

void ModernToast::Close() {
	if (hwnd) {
		AnimateWindow(hwnd, 200, AW_SLIDE | AW_HIDE);
		Sleep(200);
		DestroyWindow(hwnd);
		hwnd = nullptr;
	}
}