#pragma once
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace femto::test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void register_test(std::string suite, std::string name, std::function<void()> func) {
        tests_.push_back({std::move(suite), std::move(name), std::move(func)});
    }

    const std::vector<TestCase>& tests() const { return tests_; }

private:
    std::vector<TestCase> tests_;
};

struct TestRegistrar {
    TestRegistrar(const std::string& suite, const std::string& name, std::function<void()> func) {
        TestRegistry::instance().register_test(suite, name, std::move(func));
    }
};

template <typename T>
void stringify(std::ostream& os, const T& val) {
    if constexpr (std::is_enum_v<T>) {
        os << static_cast<std::underlying_type_t<T>>(val);
    } else if constexpr (requires { os << val; }) {
        os << val;
    } else {
        os << "<unprintable>";
    }
}

#define TEST_CASE(suite, name) \
    static void test_##suite##_##name(); \
    static ::femto::test::TestRegistrar registrar_##suite##_##name(#suite, #name, test_##suite##_##name); \
    static void test_##suite##_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")"); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            throw std::runtime_error(std::string("Assertion failed (expected false): ") + #cond + " (" + __FILE__ + ":" + std::to_string(__LINE__) + ")"); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        const auto& _va = (a); \
        const auto& _vb = (b); \
        if (_va != _vb) { \
            std::ostringstream oss; \
            oss << "Assertion failed: " << #a << " == " << #b << " ("; \
            ::femto::test::stringify(oss, _va); \
            oss << " != "; \
            ::femto::test::stringify(oss, _vb); \
            oss << ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        const auto& _va = (a); \
        const auto& _vb = (b); \
        if (_va == _vb) { \
            std::ostringstream oss; \
            oss << "Assertion failed: " << #a << " != " << #b << " ("; \
            ::femto::test::stringify(oss, _va); \
            oss << " == "; \
            ::femto::test::stringify(oss, _vb); \
            oss << ") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

#define ASSERT_STREQ(a, b) \
    do { \
        if (std::string_view(a) != std::string_view(b)) { \
            std::ostringstream oss; \
            oss << "Assertion failed: " << #a << " == " << #b << " (\"" << (a) << "\" != \"" << (b) << "\") at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

} // namespace femto::test