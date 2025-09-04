#pragma once
#include <string>
#include <vector>
#include <unordered_set>

/**
 * @brief 本地单词拼写检查器
 *
 * 功能：
 * 1. 从本地字典加载单词
 * 2. 将 OCR 输出的一行文本拆分成单词
 * 3. 单词拼写检查
 * 4. 提供拼写纠正候选
 */
class WordSpellChecker {
private:
	std::unordered_set<std::string> localDictionary; ///< 存储字典单词集合

	/**
	 * @brief 将单词转为小写
	 * @param inputWord 输入单词
	 * @return 小写单词
	 */
	std::string normalizeWord(const std::string& inputWord);

	/**
	 * @brief 计算两个字符串的编辑距离（Levenshtein Distance）
	 * @param wordA 第一个字符串
	 * @param wordB 第二个字符串
	 * @return 编辑距离
	 */
	int calculateEditDistance(const std::string& wordA, const std::string& wordB);

public:
	/**
	 * @brief 构造函数，从字典文件加载单词
	 * @param dictionaryPath 字典文件路径，每行一个单词
	 */
	WordSpellChecker(const std::string& dictionaryPath);

	/**
	 * @brief 将文本拆分为单词
	 * @param text 输入文本（OCR 输出的一行）
	 * @return 拆分后的单词列表
	 */
	std::vector<std::string> extractWords(const std::string& text);

	/**
	 * @brief 检查单词是否在字典中
	 * @param word 待检查单词
	 * @return true 存在，false 不存在
	 */
	bool isWordCorrect(const std::string& word);

	/**
	 * @brief 为拼写错误的单词生成候选单词列表
	 * @param word 待纠正单词
	 * @param maxDistance 最大编辑距离，默认2
	 * @return 候选单词列表
	 */
	std::vector<std::string> getSuggestions(const std::string& word, int maxDistance = 2);
};
