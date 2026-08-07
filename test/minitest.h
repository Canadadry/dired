// minitest.h
#ifndef MINITEST_H
#define MINITEST_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

typedef struct {
    int left;
    int right;
    int expected;
} Case;

#ifdef MINITEST_IMPL
int mt_total = 0;
int mt_failed = 0;
#else
extern int mt_total;
extern int mt_failed;
#endif

void _test_errorf(const char *func, const char *name, const char *fmt, ...);
void _test_fatalf(const char *func, const char *name, const char *fmt, ...);
void test_done(void);

#ifdef MINITEST_IMPL

inline void _test_errorf(const char *func, const char *name, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] %s/%s: ", func, name);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    mt_failed++;
}

void _test_fatalf(const char *func, const char *name, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[FATAL] %s/%s: ", func, name);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

void test_done(void) {
    printf("\n---\nTotal: %d, Failed: %d\n", mt_total, mt_failed);
}

#endif // MINITEST_IMPL

#define TEST_ERRORF(name, fmt, ...) \
    _test_errorf(__func__, name, fmt, ##__VA_ARGS__)

#define TEST_FATALF(name, fmt, ...) \
    _test_fatalf(__func__, name, fmt, ##__VA_ARGS__)

#define TEST_GROUP(fn) do {               \
    if (mt_failed>0) { break;}            \
    fn();                                 \
    if (mt_failed == 0 ) {                \
        printf("[ OK ] %s\n", #fn);       \
    } else {                              \
       printf("[ FAILED ] %s\n", #fn);    \
    }                                     \
} while(0)

#endif // MINITEST_H
