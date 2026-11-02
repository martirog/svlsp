// cmake/cxx20_probe.cpp
//
// Compiled by tools/find-cxx20.sh (to pick a compiler) and by
// CMakeLists.txt (to reject one at configure time). Uses the C++20 language
// and library features svlsp and lsp-framework rely on, so a compiler that
// accepts -std=c++20 but ships an incomplete library (GCC < 10, Clang on an
// old libstdc++) fails here instead of halfway through the build.

#include <algorithm>
#include <charconv>
#include <concepts>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

template <typename T>
concept Integral = std::integral<T>;

template <Integral T>
T twice(T v) requires(sizeof(T) >= 1) { return v * 2; }

struct Options { int a = 0; bool b = false; };

int main()
{
    Options o{.a = 1, .b = true};
    std::string s = "probe";
    std::string_view sv = s;
    std::map<std::string, int> m{{"k", 1}};
    std::vector<int> v{1, 2, 3};
    std::erase_if(v, [](int x) { return x == 2; });
    std::span<const int> sp(v);
    int parsed = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), parsed);
    bool ok = s.starts_with("pro") && sv.ends_with("be") && m.contains("k")
              && std::ranges::equal(sp, std::vector<int>{1, 3})
              && !std::filesystem::path("a/b").filename().empty();
    return ok && o.b ? twice(o.a) - 2 : 1;
}
