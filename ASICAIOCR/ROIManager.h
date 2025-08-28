#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>

struct ROI {
	RECT rect;
	std::wstring name;
};

class ROIManager {
public:
	ROIManager();

	// 绘制 ROI
	void DrawROIs(cv::Mat& frame);

	// 鼠标事件处理
	void OnLButtonDown(const POINT& pt);
	void OnMouseMove(const POINT& pt);
	void OnLButtonUp(HWND hwnd);
	void OnRButtonDown(HWND hwnd, const POINT& pt);

	// 保存/加载 ROI 模板
	void SaveTemplate(HWND hwnd);
	void LoadTemplate(HWND hwnd);

	// 获取 ROI 内图像内容
	std::vector<cv::Mat> ExtractROIs(const cv::Mat& frame);

private:
	std::vector<ROI> roiList;
	RECT roiRect;
	bool roiDrawing;
	POINT ptStart;

	std::wstring InputBox(HWND hwnd, const std::wstring& prompt);
};
