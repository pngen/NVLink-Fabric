#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

namespace nvltest {
inline int failures = 0;
inline const char* current = "";
inline int checks = 0;
inline void report(bool ok, const char* expr, const char* file, int line) {
    ++checks;
    if (!ok) {
        std::printf("[FAIL]  %s (%s:%d) [%s]\n", expr, file, line, current ? current : "");
        ++failures;
    }
}
}  // namespace nvltest

#define CHECK(cond) ::nvltest::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) do { auto _va = (a); auto _vb = (b); ::nvltest::report((_va) == (_vb), #a " == " #b, __FILE__, __LINE__); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) { ::nvltest::report((cond), #cond " (" msg ")", __FILE__, __LINE__); } } while (0)

#define TEST_MAIN()     int main(int, char**) {         ::nvltest::current = __FILE__;         run_tests();         std::printf("== tests: %d checks, %d failures ==\n", ::nvltest::checks, ::nvltest::failures);         return ::nvltest::failures == 0 ? 0 : 1;     }
