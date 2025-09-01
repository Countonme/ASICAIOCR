// LoadLibraryWithDetailedError.h
#pragma once
#include <string>

/**
 * @brief 安全加载 DLL，失败时弹出详细的错误信息（含错误码、描述、常见原因）
 *
 * @param dllPath DLL 文件路径（支持 UTF-8 编码，可含中文）
 * @return 成功返回 HMODULE，失败返回 nullptr
 *
 * @note 使用示例：
 *       HMODULE hDll = LoadLibraryWithDetailedError("MyDll.dll");
 *       if (!hDll) return false;
 */
HMODULE LoadLibraryWithDetailedError(const std::string& dllPath);