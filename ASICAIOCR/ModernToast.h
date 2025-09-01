#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include <chrono>

class ModernToast {
public:
	enum Position {
		CENTER,
		BOTTOM_LEFT,
		TOP_RIGHT
	};

	enum Style {
		INFO,
		WARNING,
		ERROR_MSG,
		SUCCESS
	};

private:
	HWND hwnd;
	HINSTANCE hInstance;
	std::wstring message;
	std::wstring title;
	int durationMs;
	Position position;
	Style style;

	// 主题颜色
	COLORREF bgColor, textColor, topBarColor;
	HBRUSH hBgBrush = nullptr;

	static ATOM RegisterWindowClass(HINSTANCE hInst);
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void SetStyleColors();

	// 绘图
	void DrawContent(HDC hdc);

public:
	ModernToast(HINSTANCE hInst);
	~ModernToast();

	ModernToast& SetMessage(const std::wstring& msg);
	ModernToast& SetTitle(const std::wstring& title);
	ModernToast& SetDuration(int ms); // 0 表示不自动关闭
	ModernToast& SetPosition(Position pos);
	ModernToast& SetStyle(Style style);
	ModernToast& SetCustomColors(COLORREF bg, COLORREF text, COLORREF topBar);

	void Show();
	void Close();
	bool IsVisible() const { return hwnd != nullptr; }
};