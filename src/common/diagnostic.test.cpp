#include <gtest/gtest.h>
#include "common/diagnostic.h"

using namespace femto;

TEST(DiagnosticTest, ErrorCounting) {
    std::string source = "hello world";
    DiagnosticEngine diag("test.femto", source);

    EXPECT_FALSE(diag.has_errors());
    EXPECT_EQ(diag.error_count(), 0u);

    diag.error({1, 1, 0}, "test error");
    EXPECT_TRUE(diag.has_errors());
    EXPECT_EQ(diag.error_count(), 1u);

    diag.warning({1, 5, 4}, "test warning");
    EXPECT_EQ(diag.error_count(), 1u);

    diag.error({2, 1, 11}, "another error");
    EXPECT_EQ(diag.error_count(), 2u);
}

TEST(DiagnosticTest, MultipleDiagnostics) {
    std::string source = "line one\nline two\nline three";
    DiagnosticEngine diag("test.femto", source);

    diag.error({1, 5, 4}, "first error");
    diag.warning({2, 3, 12}, "a warning");
    diag.note({3, 1, 21}, "a note");

    EXPECT_EQ(diag.diagnostics().size(), 3u);
}

TEST(DiagnosticTest, GetLine) {
    std::string source = "first line\nsecond line\nthird line";
    DiagnosticEngine diag("test.femto", source);

    // The format_message function uses get_line internally
    diag.error({2, 5, 16}, "error on line 2");
    EXPECT_EQ(diag.diagnostics().size(), 1u);
}
