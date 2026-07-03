// Minimal Boost.Test -> GoogleTest shim.
//
// The liquibook reference tests are copied verbatim from upstream (which uses
// Boost.Test), but this project builds with GoogleTest and has no Boost. Upstream
// only ever uses BOOST_CHECK / BOOST_CHECK_EQUAL / BOOST_AUTO_TEST_CASE, so this
// header maps exactly those onto GoogleTest and lets the .cpp files stay otherwise
// untouched. Each translated .cpp defines BOOST_TEST_SUITE_NAME before including
// this header (so cases from different files land in distinct GoogleTest suites,
// avoiding name collisions).
#pragma once

#include <gtest/gtest.h>
#include <iostream>   // upstream helpers use std::cout for failure diagnostics

#ifndef BOOST_TEST_SUITE_NAME
#define BOOST_TEST_SUITE_NAME LiquibookRef
#endif

#define BOOST_CHECK(cond)       EXPECT_TRUE(cond)
#define BOOST_CHECK_EQUAL(a, b) EXPECT_EQ((a), (b))

// Indirection so BOOST_TEST_SUITE_NAME is expanded (not pasted literally) before
// GoogleTest's TEST() token-pastes the suite and case names together.
#define LIQUI_GTEST_CASE_(suite, name) TEST(suite, name)
#define BOOST_AUTO_TEST_CASE(name)     LIQUI_GTEST_CASE_(BOOST_TEST_SUITE_NAME, name)
