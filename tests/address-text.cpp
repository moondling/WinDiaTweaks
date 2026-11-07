#include <cstdio>
#include <utility>
#include "../src/address_text.h"

int main() {
    const std::pair<const wchar_t*, const wchar_t*> cases[] = {
        {L"", L""},
        {L"  www.example.com / Page title\r\n", L"example.com"},
        {L"https://www.example.com/path?query=1#part", L"example.com"},
        {L"www.example.com/path?query=1#part", L"example.com"},
        {L"example.com?query=1", L"example.com"},
        {L"example.com#part", L"example.com"},
        {L"//www.example.com/path", L"example.com"},
        {L"http://localhost:3000/path", L"localhost:3000"},
        {L"localhost:3000/path", L"localhost:3000"},
        {L"https://[::1]:3000/path", L"[::1]:3000"},
        {L"https://user:pass@example.com/path", L"example.com"},
        {L"chrome://extensions/", L"extensions"},
        {L"about:blank", L"about:blank"},
        {L"Chat about example.com/path", L"Chat about example.com/path"},
        {L"Chat title / Conversation", L"Chat title"},
        {L"New Tab", L"New Tab"},
    };
    for (auto [input, expected] : cases) {
        auto actual = SiteOf(input);
        if (actual != expected) {
            std::fwprintf(stderr, L"SiteOf(%ls): expected %ls, got %ls\n", input, expected, actual.c_str());
            return 1;
        }
    }
    std::puts("16 address display cases passed");
}
