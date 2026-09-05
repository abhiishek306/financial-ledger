#pragma once

#include <cstdio>
#include <cstdlib>

// Minimal assertion macro for the test executables: avoids pulling in a full
// test framework dependency so tests build offline without network access.
#define LEDGER_CHECK(cond)                                                                  \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__);  \
            std::exit(1);                                                                   \
        }                                                                                    \
    } while (0)
