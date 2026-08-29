#include "test_framework.hpp"
#include <chrono>
#include <map>

int main() {
    using namespace femto::test;

    const auto& tests = TestRegistry::instance().tests();
    std::cout << "\033[1;36m[ Unit Test Runner ] Running " << tests.size() << " tests across compiler components...\033[0m\n";

    std::map<std::string, std::vector<TestCase>> grouped_tests;
    for (const auto& t : tests) {
        grouped_tests[t.suite].push_back(t);
    }

    size_t passed = 0;
    size_t failed = 0;
    auto start_all = std::chrono::high_resolution_clock::now();

    for (const auto& [suite, suite_tests] : grouped_tests) {
        std::cout << "\n  \033[1;35m● Suite: " << suite << "\033[0m (" << suite_tests.size() << " test cases)\n";
        for (const auto& test : suite_tests) {
            std::cout << "    - " << test.name << " ... " << std::flush;
            auto t0 = std::chrono::high_resolution_clock::now();
            try {
                test.func();
                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::cout << "\033[1;32mPASSED\033[0m \033[90m(" << ms << "ms)\033[0m\n";
                passed++;
            } catch (const std::exception& e) {
                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                std::cout << "\033[1;31mFAILED\033[0m \033[90m(" << ms << "ms)\033[0m\n";
                std::cout << "      \033[31mError: " << e.what() << "\033[0m\n";
                failed++;
            }
        }
    }

    auto end_all = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end_all - start_all).count();

    std::cout << "\n\033[1;36m[ Unit Test Summary ] " << passed << " passed, " << failed << " failed (" << total_ms << "ms total)\033[0m\n";
    return failed == 0 ? 0 : 1;
}