#pragma once
#include <string>

class FileLogger {
public:
	FileLogger(const std::string& filename);
	~FileLogger();

	void Log(const std::string& text);

private:
	struct Impl;
	Impl* pImpl; // Pimpl模式，隐藏实现细节
};
