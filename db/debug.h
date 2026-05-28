#include <stdio.h>

#ifdef DEBUG
#define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_LOG(...)
#endif

#define LOG(...) fprintf(stderr, __VA_ARGS__)

