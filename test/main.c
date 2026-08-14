#define MINITEST_IMPL
#include "minitest.h"
#include "tests.h"

int main(void) {
    TEST_GROUP(test_helpers);
    TEST_GROUP(test_update);
    TEST_GROUP(test_view);
    TEST_GROUP(test_loaddir);
    TEST_GROUP(test_trash);
    TEST_GROUP(test_archive);
    TEST_GROUP(test_pager);
    TEST_GROUP(test_gitstatus);
    TEST_GROUP(test_hashmap);
    TEST_GROUP(test_history);

    test_done();
    return 0;
}
