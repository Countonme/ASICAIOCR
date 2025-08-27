#include "FileLogger.h"
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <Windows.h>

struct FileLogger::Impl {
	std::ofstream logFile;
	std::mutex mtx;
};

FileLogger::FileLogger(const std::string& filename) {
	pImpl = new Impl();
	pImpl->logFile.open(filename, std::ios::out | std::ios::app);
}

FileLogger::~FileLogger() {
	if (pImpl->logFile.is_open())
		pImpl->logFile.close();
	delete pImpl;
}

void FileLogger::Log(const std::string& text) {
	std::lock_guard<std::mutex> lock(pImpl->mtx);

	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm;
	localtime_s(&tm, &t);

	std::stringstream ss;
	ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " " << text << "\n";

	if (pImpl->logFile.is_open()) {
		pImpl->logFile << ss.str();
		pImpl->logFile.flush();
	}

	OutputDebugStringA(ss.str().c_str());
}