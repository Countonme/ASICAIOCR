// LoadLibraryWithDetailedError.cpp
//#include "LoadLibraryWithDetailedError.h"
#include <Windows.h>
#include <iostream>
#include <sstream>
#include <string>

// 将 UTF-8 std::string 转为 std::wstring
std::wstring StringToWString(const std::string& str) {
	if (str.empty()) return std::wstring();
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	if (size_needed <= 0) return std::wstring();
	std::wstring wstr(size_needed - 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
	return wstr;
}

// 将 std::wstring 转为 UTF-8 std::string
std::string WStringToUtf8(const std::wstring& wstr) {
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (size_needed <= 0) return std::string();
	std::string str(size_needed - 1, 0); // -1 排除末尾 \0
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, nullptr, nullptr);
	return str;
}

// 获取错误码对应的系统描述信息（宽字符）
std::wstring GetErrorString(DWORD error) {
	LPWSTR buffer = nullptr;
	DWORD size = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&buffer,
		0,
		nullptr
	);

	std::wstring message;
	if (buffer) {
		message = buffer;
		LocalFree(buffer);
	}
	else {
		message = L"系统未提供错误描述。";
	}

	// 去除末尾换行符
	while (!message.empty() && (message.back() == L'\n' || message.back() == L'\r')) {
		message.pop_back();
	}

	return message;
}

// 主函数：带详细错误提示的 LoadLibrary
HMODULE LoadLibraryWithDetailedError(const std::string& dllPath) {
	// 尝试加载 DLL
	HMODULE hDll = LoadLibraryA(dllPath.c_str());
	if (hDll != nullptr) {
		return hDll; // ✅ 成功
	}

	// 获取错误码
	DWORD err = GetLastError();

	// 转换路径和错误信息为宽字符
	std::wstring wPath = StringToWString(dllPath);
	std::wstring systemMsg = GetErrorString(err);

	// 构建弹窗显示的详细错误信息
	std::wstringstream ss;
	ss << L"无法加载指定的 DLL 文件：\n\n"
		<< wPath << L"\n\n"
		<< L"错误码: " << err << L"\n"
		<< L"系统提示: " << systemMsg << L"\n\n";

	// 根据常见错误码添加诊断建议
	switch (err) {
	case 126: // ERROR_MOD_NOT_FOUND
		ss << L"🔍 可能原因：\n"
			<< L"• DLL 文件本身或其依赖的组件缺失\n"
			<< L"• 缺少 Visual C++ 运行库（如 vcruntime140.dll）\n"
			<< L"• 请安装 Microsoft Visual C++ Redistributable";
		break;

	case 193: // ERROR_BAD_EXE_FORMAT
		ss << L"🔍 可能原因：\n"
			<< L"• 位数不匹配！\n"
			<< L"• 你的程序是 64位，但 DLL 是 32位（或反之）\n"
			<< L"• 请确认 DLL 是否为 64位（PE32+）";
		break;

	case 1114: // ERROR_DLL_INIT_FAILED
		ss << L"🔍 可能原因：\n"
			<< L"• DLL 初始化失败（DllMain 返回 FALSE）\n"
			<< L"• 依赖项加载失败导致初始化中断";
		break;

	default:
		ss << L"🔍 请检查：\n"
			<< L"• 文件路径是否正确\n"
			<< L"• DLL 是否存在且可读\n"
			<< L"• 是否有依赖项缺失";
		break;
	}

	// 弹出错误对话框（带中文提示）
	MessageBoxW(
		nullptr,
		ss.str().c_str(),
		L"DLL 加载失败",
		MB_OK | MB_ICONERROR
	);

	// 控制台输出（UTF-8 编码，兼容大多数终端）
	std::string narrowMsg = WStringToUtf8(systemMsg);
	if (narrowMsg.empty()) {
		narrowMsg = "Failed to convert error message.";
	}

	std::cerr << "❌ 加载 DLL 失败: " << dllPath
		<< " | 错误码: " << err
		<< " | 描述: " << narrowMsg
		<< std::endl;

	return nullptr;
}