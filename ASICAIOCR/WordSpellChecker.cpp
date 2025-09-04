#include "WordSpellChecker.h"
#include <fstream>
#include <algorithm>
#include <regex>

/*------------------- 私有函数 -------------------*/

std::string WordSpellChecker::normalizeWord(const std::string& inputWord) {
	std::string lower = inputWord;
	std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
	return lower;
}

int WordSpellChecker::calculateEditDistance(const std::string& wordA, const std::string& wordB) {
	size_t m = wordA.size(), n = wordB.size();
	std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));
	for (size_t i = 0; i <= m; ++i) dp[i][0] = i;
	for (size_t j = 0; j <= n; ++j) dp[0][j] = j;
	for (size_t i = 1; i <= m; ++i) {
		for (size_t j = 1; j <= n; ++j) {
			if (wordA[i - 1] == wordB[j - 1]) dp[i][j] = dp[i - 1][j - 1];
			else dp[i][j] = 1 + std::min({ dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1] });
		}
	}
	return dp[m][n];
}

/*------------------- 公有函数 -------------------*/

WordSpellChecker::WordSpellChecker(const std::string& dictionaryPath) {
	std::ifstream fin(dictionaryPath);
	std::string word;
	while (fin >> word) {
		localDictionary.insert(normalizeWord(word));
	}
}

std::vector<std::string> WordSpellChecker::extractWords(const std::string& text) {
	std::vector<std::string> words;
	std::regex word_regex("[a-zA-Z]+"); // 匹配字母单词
	auto words_begin = std::sregex_iterator(text.begin(), text.end(), word_regex);
	auto words_end = std::sregex_iterator();
	for (auto i = words_begin; i != words_end; ++i) {
		words.push_back(i->str());
	}
	return words;
}

bool WordSpellChecker::isWordCorrect(const std::string& word) {
	return localDictionary.count(normalizeWord(word)) > 0;
}

std::vector<std::string> WordSpellChecker::getSuggestions(const std::string& word, int maxDistance) {
	std::vector<std::string> candidates;
	std::string lw = normalizeWord(word);
	for (const auto& w : localDictionary) {
		if (calculateEditDistance(lw, w) <= maxDistance) {
			candidates.push_back(w);
		}
	}
	return candidates;
}