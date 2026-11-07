#pragma once
#include <cwctype>
#include <string>

// Dia alternates between "site / title" and a URL while the pointer moves over the field.
inline std::wstring SiteOf(std::wstring text) {
    while (!text.empty() && iswspace(text.back())) text.pop_back();
    while (!text.empty() && iswspace(text.front())) text.erase(0, 1);
    if (auto title = text.find(L" / "); title != std::wstring::npos) text.resize(title);
    if (text.find_first_of(L" \t\r\n") == std::wstring::npos) {
        auto scheme = text.find(L"://");
        bool address = scheme != std::wstring::npos || text.find_first_of(L".:") != std::wstring::npos;
        if (scheme != std::wstring::npos) text.erase(0, scheme + 3);
        else if (text.rfind(L"//", 0) == 0) text.erase(0, 2), address = true;
        if (address) {
            text = text.substr(0, text.find_first_of(L"/?#"));
            if (auto at = text.find(L'@'); at != std::wstring::npos) text.erase(0, at + 1);
        }
    }
    if (text.rfind(L"www.", 0) == 0) text.erase(0, 4);
    return text;
}
