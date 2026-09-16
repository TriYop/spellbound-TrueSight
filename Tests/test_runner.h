#pragma once
#include <cstdio>
#include <cstdlib>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(expr) \
    do { \
        if (expr) { \
            ++g_passed; \
        } else { \
            ++g_failed; \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); \
        } \
    } while(0)

#define CHECK_MSG(expr, msg) \
    do { \
        if (expr) { \
            ++g_passed; \
        } else { \
            ++g_failed; \
            std::fprintf(stderr, "FAIL  %s:%d  %s  (%s)\n", __FILE__, __LINE__, #expr, msg); \
        } \
    } while(0)

#define TEST_SUMMARY() \
    do { \
        std::printf("\n%d passed, %d failed\n", g_passed, g_failed); \
        if (g_failed > 0) std::exit(1); \
    } while(0)
