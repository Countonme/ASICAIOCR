#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <opencv2/opencv.hpp>

struct ROI {
	RECT rect;
	std::wstring name;
};

class ROIManager {
public:
	ROIManager() : roiDrawing(false) {}

	// 绘制 ROI
	void DrawROIs(cv::Mat& frame) {
		for (auto& roi : roiList) {
			cv::rectangle(frame,
				cv::Point(roi.rect.left, roi.rect.top),
				cv::Point(roi.rect.right, roi.rect.bottom),
				cv::Scalar(0, 0, 255), 2);
			cv::putText(frame, std::string(roi.name.begin(), roi.name.end()),
				cv::Point(roi.rect.left, roi.rect.top - 5),
				cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
		}
		// 绘制当前正在绘制的矩形
		if (roiDrawing) {
			cv::rectangle(frame,
				cv::Point(roiRect.left, roiRect.top),
				cv::Point(roiRect.right, roiRect.bottom),
				cv::Scalar(255, 0, 0), 1, cv::LINE_8);
		}
	}

	// 鼠标左键按下开始绘制
	void OnLButtonDown(POINT pt) {
		roiDrawing = true;
		ptStart = pt;
		roiRect = { pt.x, pt.y, pt.x, pt.y };
	}

	// 鼠标移动更新矩形
	void OnMouseMove(POINT pt) {
		if (roiDrawing) {
			roiRect.left = std::min(ptStart.x, pt.x);
			roiRect.top = std::min(ptStart.y, pt.y);
			roiRect.right = std::max(ptStart.x, pt.x);
			roiRect.bottom = std::max(ptStart.y, pt.y);
		}
	}

	// 鼠标左键释放完成绘制
	void OnLButtonUp(HWND hwnd) {
		if (roiDrawing) {
			roiDrawing = false;
			std::wstring name = InputBox(hwnd, L"请输入 ROI 名称:");
			if (!name.empty()) {
				ROI roi;
				roi.rect = roiRect;
				roi.name = name;
				roiList.push_back(roi);
			}
		}
	}

	// 鼠标右键删除 ROI
	void OnRButtonDown(HWND hwnd, POINT pt) {
		for (size_t i = 0; i < roiList.size(); ++i) {
			if (PtInRect(&roiList[i].rect, pt)) {
				if (MessageBox(hwnd,
					(L"删除 ROI: " + roiList[i].name).c_str(),
					L"确认删除", MB_YESNO) == IDYES) {
					roiList.erase(roiList.begin() + i);
				}
				break;
			}
		}
	}

	// 保存 ROI 模板
	void SaveTemplate(HWND hwnd) {
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
		}
	}

	// 加载 ROI 模板
	void LoadTemplate(HWND hwnd) {
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
		}
	}

	// 获取 ROI 内的图像内容
	std::vector<cv::Mat> ExtractROIs(const cv::Mat& frame) {
		std::vector<cv::Mat> crops;
		for (auto& roi : roiList) {
			cv::Rect r(roi.rect.left, roi.rect.top,
				roi.rect.right - roi.rect.left,
				roi.rect.bottom - roi.rect.top);
			r &= cv::Rect(0, 0, frame.cols, frame.rows);
			if (r.width > 0 && r.height > 0)
				crops.push_back(frame(r).clone());
		}
		return crops;
	}

private:
	std::vector<ROI> roiList;
	RECT roiRect;
	bool roiDrawing;
	POINT ptStart;

	// 简单输入框
	std::wstring InputBox(HWND hwnd, const std::wstring& prompt) {
		wchar_t input[128] = L"";
		if (DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(101), hwnd,
			[](HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)->INT_PTR {
				switch (msg) {
				case WM_INITDIALOG:
					SetDlgItemText(hDlg, 1001, (LPCWSTR)lParam);
					return TRUE;
				case WM_COMMAND:
					if (LOWORD(wParam) == IDOK) {
						EndDialog(hDlg, 1);
						return TRUE;
					}
					if (LOWORD(wParam) == IDCANCEL) {
						EndDialog(hDlg, 0);
						return TRUE;
					}
				}
				return FALSE;
			}, (LPARAM)prompt.c_str()))
		{
			GetDlgItemText(hwnd, 1001, input, 128);
		}
		return input;
	}
};