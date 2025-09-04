// SajectWrapper.cpp
#include "SajectWrapper.h"
#include <iostream>
#include <io.h>      // _access
#include <fcntl.h>   // _O_RDONLY 等

#include <locale>
#include <codecvt>

#include "LoadLibraryWithDetailedError.h"
//文件判断是否存在
bool FileExists(const std::string& path) {
	return _access(path.c_str(), 0) == 0;
}

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

bool SajectConnect::Initialize(const std::string& dllPath) {
	// 清理旧状态
	Cleanup();

	// 使用
	if (!FileExists(dllPath)) {
		std::cerr << "DLL 文件不存在: " << dllPath << std::endl;

		std::wstring msg = L"文件不存在: " + utf8ToWstring(dllPath);
		MessageBoxW(nullptr, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);

		return false;
	}
	// 加载 DLL
	m_hDll = LoadLibraryWithDetailedError("SajetConnect.dll");
	if (!m_hDll) {
		return false; // 自动弹窗 + 输出错误
	}

	// 获取函数地址
	m_pStart = (PF_SAJET_TRANS_START)GetProcAddress(m_hDll, "SajetTransStart");
	m_pData = (PF_SAJET_TRANS_DATA)GetProcAddress(m_hDll, "SajetTransData");
	m_pClose = (PF_SAJET_TRANS_CLOSE)GetProcAddress(m_hDll, "SajetTransClose");

	if (!m_pStart || !m_pData || !m_pClose) {
		std::cerr << "GetProcAddress 失败！可能函数名错误或 DLL 不兼容。" << std::endl;
		Cleanup();
		return false;
	}

	return true;
}

void SajectConnect::Cleanup() {
	if (m_hDll) {
		FreeLibrary(m_hDll);
		m_hDll = nullptr;
	}
	m_pStart = nullptr;
	m_pData = nullptr;
	m_pClose = nullptr;
}

bool SajectConnect::SajetTransStart() {
	if (m_pStart) {
		return m_pStart();
	}
	return false;
}

bool SajectConnect::SajetTransData(int f_iCommandNo, char* f_pData, int& f_pLen) {
	if (m_pData) {
		return m_pData(f_iCommandNo, f_pData, &f_pLen);  // 注意：传 f_pLen 的地址
	}
	return false;
}

bool SajectConnect::SajetTransClose() {
	if (m_pClose) {
		return m_pClose();
	}
	return false;
}