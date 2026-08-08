#define MINITEST_IMPL
#include "minitest.h"
#include "tests.h"

int main(void) {
    TEST_GROUP(test_helpers);
    TEST_GROUP(test_update);
    TEST_GROUP(test_view);

    test_done();
    return 0;
}
