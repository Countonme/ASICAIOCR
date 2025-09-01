// SajectWrapper.h
#pragma once
#include <windows.h>
#include <string>

class SajectConnect {
public:
	// 初始化 DLL 并加载函数
	bool Initialize(const std::string& dllPath = "SajetConnect.dll");

	// 释放资源
	void Cleanup();

	// 对应 C# 的三个函数
	bool SajetTransStart();
	bool SajetTransData(int f_iCommandNo, char* f_pData, int& f_pLen);
	bool SajetTransClose();

private:
	HMODULE m_hDll = nullptr;

	// 函数指针类型定义
	typedef bool(__stdcall* PF_SAJET_TRANS_START)();
	typedef bool(__stdcall* PF_SAJET_TRANS_DATA)(int, char*, int*);
	typedef bool(__stdcall* PF_SAJET_TRANS_CLOSE)();

	// 函数指针
	PF_SAJET_TRANS_START m_pStart = nullptr;
	PF_SAJET_TRANS_DATA   m_pData = nullptr;
	PF_SAJET_TRANS_CLOSE  m_pClose = nullptr;
};