#pragma once
#include <cstdio>

// Enable with: cmake -DHERMES_DEBUG=ON
#ifdef HERMES_DEBUG
#  define DBG(fmt, ...) \
     std::fprintf(stderr, "[dbg] " fmt "\n", ##__VA_ARGS__)
#else
#  define DBG(fmt, ...) do {} while (0)
#endif
