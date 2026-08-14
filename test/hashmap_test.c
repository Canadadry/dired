#include "minitest.h"
#include "../src/hashmap.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    int tag;
} TestVal;

CREATE_HASHMAP(TestVal)
WRITE_HASHMAP_IMPL(TestVal)

static TestValHashMap make_map(void)
{
    return (TestValHashMap){ .data = array_create_TestValHashMapCell(std_allocator()) };
}

static void free_map(TestValHashMap *m)
{
    if (m->data.alloc.free_fn)
        m->data.alloc.free_fn(m->data.alloc.userdata, m->data.data);
    m->data.data = NULL;
    m->data.len = 0;
    m->data.capacity = 0;
}

static void test_insert_several_keys_below_root_all_findable(void)
{
    const char *keys[] = {
        "/alpha", "/bravo", "/charlie", "/delta",
        "/echo", "/foxtrot", "/golf", "/hotel",
    };
    int n = (int)(sizeof(keys) / sizeof(keys[0]));

    TestValHashMap m = make_map();

    for (int i = 0; i < n; i++) {
        TestVal *v = TestVal_upsert(&m, keys[i], UpsertActionCreate);
        if (!v) {
            TEST_ERRORF(keys[i], "insert returned NULL");
            free_map(&m);
            return;
        }
        v->tag = i;
    }

    for (int i = 0; i < n; i++) {
        TestVal *v = TestVal_upsert(&m, keys[i], UpsertActionUpdate);
        if (!v) {
            TEST_ERRORF(keys[i], "lookup returned NULL after insert");
            continue;
        }
        if (v->tag != i) {
            TEST_ERRORF(keys[i], "lookup tag = %d, want %d", v->tag, i);
        }
    }

    if (m.data.len != n + 1) {
        TEST_ERRORF("array length", "len = %d, want %d (root + %d keys)", m.data.len, n + 1, n);
    }

    free_map(&m);
}

static void test_single_key_round_trip(void)
{
    TestValHashMap m = make_map();

    TestVal *inserted = TestVal_upsert(&m, "/only", UpsertActionCreate);
    if (!inserted) {
        TEST_ERRORF("insert", "returned NULL");
        free_map(&m);
        return;
    }
    inserted->tag = 42;

    TestVal *found = TestVal_upsert(&m, "/only", UpsertActionUpdate);
    if (!found || found->tag != 42) {
        TEST_ERRORF("lookup", "found = %p, tag = %d, want 42", (void *)found, found ? found->tag : -1);
    }

    TestVal *missing = TestVal_upsert(&m, "/nope", UpsertActionUpdate);
    if (missing != NULL) {
        TEST_ERRORF("lookup missing", "expected NULL for absent key");
    }

    free_map(&m);
}

static void test_first_inserted_key_reachable_as_child_of_later_keys(void)
{
    TestValHashMap m = make_map();

    TestVal *first = TestVal_upsert(&m, "/first", UpsertActionCreate);
    first->tag = 0;

    const char *more_keys[] = { "/second", "/third", "/fourth", "/fifth", "/sixth" };
    for (int i = 0; i < 5; i++) {
        TestVal *v = TestVal_upsert(&m, more_keys[i], UpsertActionCreate);
        if (!v) {
            TEST_ERRORF(more_keys[i], "insert returned NULL");
            free_map(&m);
            return;
        }
        v->tag = i + 1;
    }

    TestVal *found_first = TestVal_upsert(&m, "/first", UpsertActionUpdate);
    if (!found_first || found_first->tag != 0) {
        TEST_ERRORF("/first", "first-ever-inserted key not findable after later inserts");
    }

    for (int i = 0; i < 5; i++) {
        TestVal *v = TestVal_upsert(&m, more_keys[i], UpsertActionUpdate);
        if (!v || v->tag != i + 1) {
            TEST_ERRORF(more_keys[i], "not findable or wrong tag after sibling inserts");
        }
    }

    free_map(&m);
}

static void test_delete_childless_cell(void)
{
    TestValHashMap m = make_map();

    const char *keys[] = { "/a", "/b", "/c" };
    for (int i = 0; i < 3; i++) {
        TestVal *v = TestVal_upsert(&m, keys[i], UpsertActionCreate);
        v->tag = i;
    }
    int len_before = m.data.len;

    TestVal_upsert(&m, "/c", UpsertActionDelete);

    if (m.data.len != len_before - 1) {
        TEST_ERRORF("array length", "len = %d, want %d", m.data.len, len_before - 1);
    }

    if (TestVal_upsert(&m, "/c", UpsertActionUpdate) != NULL) {
        TEST_ERRORF("/c", "deleted key still findable");
    }

    TestVal *a = TestVal_upsert(&m, "/a", UpsertActionUpdate);
    TestVal *b = TestVal_upsert(&m, "/b", UpsertActionUpdate);
    if (!a || a->tag != 0) TEST_ERRORF("/a", "not findable or wrong tag after unrelated delete");
    if (!b || b->tag != 1) TEST_ERRORF("/b", "not findable or wrong tag after unrelated delete");

    free_map(&m);
}

static void test_delete_cell_with_children_keeps_survivors_findable(void)
{
    TestValHashMap m = make_map();

    const char *keys[] = {
        "/alpha", "/bravo", "/charlie", "/delta",
        "/echo", "/foxtrot", "/golf", "/hotel",
        "/india", "/juliet", "/kilo", "/lima",
    };
    int n = (int)(sizeof(keys) / sizeof(keys[0]));
    for (int i = 0; i < n; i++) {
        TestVal *v = TestVal_upsert(&m, keys[i], UpsertActionCreate);
        v->tag = i;
    }
    int len_before = m.data.len;

    TestVal_upsert(&m, keys[0], UpsertActionDelete);

    if (m.data.len != len_before - 1) {
        TEST_ERRORF("array length", "len = %d, want %d after deleting a cell with children", m.data.len, len_before - 1);
    }

    if (TestVal_upsert(&m, keys[0], UpsertActionUpdate) != NULL) {
        TEST_ERRORF(keys[0], "deleted key still findable");
    }

    for (int i = 1; i < n; i++) {
        TestVal *v = TestVal_upsert(&m, keys[i], UpsertActionUpdate);
        if (!v || v->tag != i) {
            TEST_ERRORF(keys[i], "surviving key not findable or wrong tag after deleting an ancestor cell");
        }
    }

    for (int i = 0; i < m.data.len; i++) {
        for (int k = 0; k < 4; k++) {
            int child = m.data.data[i].children[k];
            if (child != 0 && (child < 0 || child >= m.data.len)) {
                TEST_ERRORF("children pointer", "cell %d child[%d] = %d out of range [0,%d)", i, k, child, m.data.len);
            }
        }
    }

    free_map(&m);
}

static void test_repeated_insert_delete_does_not_grow_unboundedly(void)
{
    TestValHashMap m = make_map();

    char key[32];
    for (int round = 0; round < 50; round++) {
        snprintf(key, sizeof(key), "/churn-%d", round);
        TestVal *v = TestVal_upsert(&m, key, UpsertActionCreate);
        v->tag = round;
        TestVal_upsert(&m, key, UpsertActionDelete);
    }

    if (m.data.len != 1) {
        TEST_ERRORF("array length", "len = %d, want 1 (root only) after balanced insert/delete churn", m.data.len);
    }

    free_map(&m);
}

void test_hashmap(void)
{
    test_insert_several_keys_below_root_all_findable();
    test_single_key_round_trip();
    test_first_inserted_key_reachable_as_child_of_later_keys();
    test_delete_childless_cell();
    test_delete_cell_with_children_keeps_survivors_findable();
    test_repeated_insert_delete_does_not_grow_unboundedly();
}
