#include "minitest.h"
#include "../src/gitstatus.h"
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *name;
    mode_t mode;
} EntrySpec;

static void build_entries(const EntrySpec *specs, int count, Entry *out)
{
    for (int i = 0; i < count; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        strcpy(out[i].name, specs[i].name);
        out[i].st.st_mode = specs[i].mode;
    }
}

static void test_classify_git_status(void)
{
    typedef struct {
        const char *label;
        const char *porcelain;
        const char *prefix;
        EntrySpec specs[4];
        int entry_count;
        GitStatusTag expected[4];
    } Case;

    Case cases[] = {
        {"untracked file", "?? newfile.txt\n", "",
         {{"newfile.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_UNTRACKED}},

        {"ignored file", "!! build.o\n", "",
         {{"build.o", S_IFREG | 0644}}, 1,
         {GIT_STATUS_IGNORED}},

        {"modified file, unstaged", " M main.c\n", "",
         {{"main.c", S_IFREG | 0644}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"modified file, staged", "M  staged.c\n", "",
         {{"staged.c", S_IFREG | 0644}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"staged-new file", "A  staged_new.txt\n", "",
         {{"staged_new.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_UNTRACKED}},

        {"deleted file, unstaged", " D removed.txt\n", "",
         {{"removed.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_DELETED}},

        {"deleted file, staged", "D  removed_staged.txt\n", "",
         {{"removed_staged.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_DELETED}},

        {"conflicted file, both modified (UU)", "UU conflict.txt\n", "",
         {{"conflict.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_CONFLICTED}},

        {"conflicted file, both added (AA)", "AA conflict2.txt\n", "",
         {{"conflict2.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_CONFLICTED}},

        {"clean file, not mentioned in porcelain output at all", " M other.c\n", "",
         {{"clean.c", S_IFREG | 0644}}, 1,
         {GIT_STATUS_NONE}},

        {"directory with an untracked and a deleted descendant picks deleted",
         "?? proj/untracked.txt\nD  proj/removed.txt\n", "",
         {{"proj", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_DELETED}},

        {"tracked directory containing a wholly-untracked directory renders modified",
         "?? outer/inner/\n", "",
         {{"outer", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"directory picks conflicted over mixed lower-priority siblings",
         "UU proj/conflict.txt\n M proj/modified.txt\n?? proj/untracked.txt\n!! proj/ignored.log\n", "",
         {{"proj", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_CONFLICTED}},

        {"directory picks modified over untracked/ignored when no conflict",
         " M proj/modified.txt\n?? proj/untracked.txt\n!! proj/ignored.log\n", "",
         {{"proj", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"directory with only an untracked descendant renders modified, not untracked",
         "?? proj/untracked.txt\n!! proj/ignored.log\n", "",
         {{"proj", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"wholly-untracked directory reported as a single collapsed porcelain line",
         "?? newdir/\n", "",
         {{"newdir", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_UNTRACKED}},

        {"directory aggregates through a deeply nested descendant",
         " M sub/deep/nested/changed.c\n", "",
         {{"sub", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"file matches a porcelain path exactly, not by prefix",
         " M exact.txt.bak\n", "",
         {{"exact.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_NONE}},

        {"file matches a porcelain path exactly, positive case",
         " M exact.txt\n M exact.txt.bak\n", "",
         {{"exact.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"symlink with a name that is a literal prefix of a directory's porcelain "
         "path is not treated as a prefix match into that directory",
         "?? node_modules/pkg/file.js\n", "",
         {{"node", S_IFLNK | 0777}, {"node_modules", S_IFDIR | 0755}}, 2,
         {GIT_STATUS_NONE, GIT_STATUS_MODIFIED}},

        {"rename line classifies against the new path, not the old one",
         "R  old_name.txt -> renamed.txt\n", "",
         {{"renamed.txt", S_IFREG | 0644}, {"old_name.txt", S_IFREG | 0644}}, 2,
         {GIT_STATUS_MODIFIED, GIT_STATUS_NONE}},

        {"symlink is matched by its own exact path like any other entry",
         " M link_file\n", "",
         {{"link_file", S_IFLNK | 0777}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"ignored directory aggregates through its descendants",
         "!! build/output.o\n!! build/obj/thing.o\n", "",
         {{"build", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_IGNORED}},

        {"multiple sibling entries each classified independently",
         "?? untracked.txt\n M modified.c\n!! ignored.log\n", "",
         {{"untracked.txt", S_IFREG | 0644}, {"modified.c", S_IFREG | 0644},
          {"ignored.log", S_IFREG | 0644}, {"clean.h", S_IFREG | 0644}}, 4,
         {GIT_STATUS_UNTRACKED, GIT_STATUS_MODIFIED, GIT_STATUS_IGNORED, GIT_STATUS_NONE}},

        {"malformed line (too short) is skipped without matching anything",
         "??\nshort\n", "",
         {{"short", S_IFREG | 0644}}, 1,
         {GIT_STATUS_NONE}},

        {"last porcelain line with no trailing newline is still parsed",
         "?? trailing.txt", "",
         {{"trailing.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_UNTRACKED}},

        {"empty porcelain text classifies everything as clean",
         "", "",
         {{"anything.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_NONE}},

        {"non-empty prefix strips repo-root-relative paths before matching",
         "?? b/c/newfile.txt\n M b/c/changed.c\n", "b/c/",
         {{"newfile.txt", S_IFREG | 0644}, {"changed.c", S_IFREG | 0644}}, 2,
         {GIT_STATUS_UNTRACKED, GIT_STATUS_MODIFIED}},

        {"non-empty prefix strips down to a subdirectory entry name",
         " M b/c/d.md\n", "b/",
         {{"c", S_IFDIR | 0755}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"porcelain line outside the given prefix is dropped entirely",
         "?? other/elsewhere.txt\n M b/c/changed.c\n", "b/c/",
         {{"changed.c", S_IFREG | 0644}}, 1,
         {GIT_STATUS_MODIFIED}},

        {"same-named entry outside the prefix does not classify the one inside it",
         " M elsewhere/shared.txt\n", "b/c/",
         {{"shared.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_NONE}},

        {"same-name-in-two-places collision: only the in-prefix change classifies",
         " M b/c/shared.txt\n?? elsewhere/shared.txt\n", "b/c/",
         {{"shared.txt", S_IFREG | 0644}}, 1,
         {GIT_STATUS_MODIFIED}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        Entry entries[4];
        build_entries(cases[i].specs, cases[i].entry_count, entries);

        classify_git_status(cases[i].porcelain, cases[i].prefix, entries, cases[i].entry_count);

        for (int j = 0; j < cases[i].entry_count; j++) {
            if (entries[j].git_status != cases[i].expected[j]) {
                TEST_ERRORF(cases[i].label, "entries[%d] (%s) git_status = %d, want %d",
                            j, entries[j].name, entries[j].git_status, cases[i].expected[j]);
            }
        }
    }
}

void test_gitstatus(void)
{
    test_classify_git_status();
}
