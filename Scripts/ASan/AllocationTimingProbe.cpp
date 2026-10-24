#include <cstdio>

// Build twice with /MTd /Zi /Od /fsanitize=address /DSTATIC_ALLOCATION=0 or 1.
// Run with ASAN_OPTIONS=halt_on_error=1:alloc_dealloc_mismatch=1.
// Both variants are valid C++ and should exit successfully; this is a diagnostic,
// not an intentional allocation mismatch like the overflow in DetectionProbe.cpp.
#ifndef STATIC_ALLOCATION
#define STATIC_ALLOCATION 0
#endif

// Compare identical array allocation/deallocation before and after entering main.
#if STATIC_ALLOCATION
char* buffer = new char[1024];
#else
char* buffer = nullptr;
#endif

int main()
{
#if !STATIC_ALLOCATION
    buffer = new char[1024];
#endif
    buffer[0] = 42;
    std::printf("static=%d value=%d; deleting array\n", STATIC_ALLOCATION, buffer[0]);
    std::fflush(stdout);
    delete[] buffer;
    std::puts("array delete succeeded");
    return 0;
}
