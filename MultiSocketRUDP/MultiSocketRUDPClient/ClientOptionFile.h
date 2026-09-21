#pragma once
#include <cstdio>
#include <cwctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Bounded reader for the client's existing :SECTION { KEY = VALUE } format.
// Parse numbers before narrowing so negative values and overflow are rejected.
class ClientOptionFile
{
public:
	bool Load(const std::wstring& path)
	{
		FILE* file{};
		if (_wfopen_s(&file, path.c_str(), L"rt, ccs=UNICODE") != 0 || file == nullptr) return false;
		std::unique_ptr<FILE, decltype(&fclose)> guard(file, &fclose);
		text.clear();
		for (wint_t value; (value = fgetwc(file)) != WEOF;)
		{
			if (text.size() >= 2047 || value == 0) return false;
			text.push_back(static_cast<wchar_t>(value));
		}
		if (ferror(file) || text.empty()) return false;
		// Remove comments before looking up sections or keys.
		bool quoted = false;
		for (size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] == L'"') quoted = not quoted;
			if (quoted || text[index] != L'/' || index + 1 >= text.size()) continue;
			if (text[index + 1] == L'/')
			{
				while (index < text.size() && text[index] != L'\n') text[index++] = L' ';
			}
			else if (text[index + 1] == L'*')
			{
				const auto end = text.find(L"*/", index + 2);
				if (end == std::wstring::npos) return false;
				while (index <= end + 1) text[index++] = L' ';
				--index;
			}
		}
		return not quoted;
	}

	std::optional<std::wstring> GetValue(const std::wstring& section, const std::wstring& key) const
	{
		const auto marker = text.find(L":" + section);
		if (marker == std::wstring::npos) return std::nullopt;
		size_t begin = marker + section.size() + 1;
		while (begin < text.size() && iswspace(text[begin])) ++begin;
		if (begin == text.size() || text[begin] != L'{') return std::nullopt;
		const auto end = text.find(L'}', ++begin);
		if (end == std::wstring::npos) return std::nullopt;
		while (begin < end)
		{
			while (begin < end && iswspace(text[begin])) ++begin;
			const auto nameBegin = begin;
			while (begin < end && (iswalnum(text[begin]) || text[begin] == L'_')) ++begin;
			const auto name = std::wstring_view(text).substr(nameBegin, begin - nameBegin);
			while (begin < end && iswspace(text[begin])) ++begin;
			if (begin == end || text[begin++] != L'=') return std::nullopt;
			while (begin < end && iswspace(text[begin])) ++begin;
			const auto valueBegin = begin;
			if (begin < end && text[begin] == L'"')
			{
				++begin;
				while (begin < end && text[begin] != L'"') ++begin;
				if (begin == end) return std::nullopt;
				++begin;
			}
			else while (begin < end && not iswspace(text[begin])) ++begin;
			if (name == key) return text.substr(valueBegin, begin - valueBegin);
		}
		return std::nullopt;
	}

	bool GetNumber(const std::wstring& section, const std::wstring& key,
		unsigned int minimum, unsigned int maximum, unsigned int& result) const
	{
		const auto value = GetValue(section, key);
		if (not value || value->empty()) return false;
		unsigned int parsed = 0;
		for (const auto digit : *value)
		{
			if (digit < L'0' || digit > L'9') return false;
			const auto part = static_cast<unsigned int>(digit - L'0');
			if (part > maximum || parsed > (maximum - part) / 10) return false;
			parsed = parsed * 10 + part;
		}
		if (parsed < minimum) return false;
		result = parsed;
		return true;
	}
private:
	std::wstring text;
};
