#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define MAX_ENTRIES 1024
#define NAME_MAX 1024
#define PATH_MAX 1024

typedef struct {
    char name[NAME_MAX + 1];
    struct stat st;
} Entry;

static Entry entries[MAX_ENTRIES];
static int entry_count = 0;
static int selected = 0;
static char current_path[PATH_MAX];

static void load_directory(const char *path) {
    DIR *dir;
    struct dirent *de;
    char fullpath[PATH_MAX];

    entry_count = 0;
    selected = 0;

    dir = opendir(path);
    if (!dir)
        return;

    while ((de = readdir(dir)) && entry_count < MAX_ENTRIES) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        if (lstat(fullpath, &entries[entry_count].st) == 0) {
            strncpy(entries[entry_count].name, de->d_name, NAME_MAX);
            entry_count++;
        }
    }

    closedir(dir);
}

static void mode_to_str(mode_t m, char *out) {
    out[0] = S_ISDIR(m) ? 'd' : '-';
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void draw(void) {
    clear();
    mvprintw(0, 0, "Path: %s", current_path);

    for (int i = 0; i < entry_count; i++) {
        char perms[11];
        mode_to_str(entries[i].st.st_mode, perms);

        if (i == selected)
            attron(A_REVERSE);

        mvprintw(i + 2, 0, "%s %8ld %s",
                 perms,
                 (long)entries[i].st.st_size,
                 entries[i].name);

        if (i == selected)
            attroff(A_REVERSE);
    }

    refresh();
}

static void enter_directory(void) {
    if (!S_ISDIR(entries[selected].st.st_mode))
        return;

    if (strcmp(entries[selected].name, ".") == 0)
        return;

    if (strcmp(entries[selected].name, "..") == 0) {
        chdir("..");
    } else {
        chdir(entries[selected].name);
    }

    getcwd(current_path, sizeof(current_path));
    load_directory(current_path);
}

static void go_parent(void) {
    chdir("..");
    getcwd(current_path, sizeof(current_path));
    load_directory(current_path);
}

int main(void) {
    int ch;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    getcwd(current_path, sizeof(current_path));
    load_directory(current_path);

    while (1) {
        draw();
        ch = getch();

        switch (ch) {
        case KEY_UP:
            if (selected > 0)
                selected--;
            break;

        case KEY_DOWN:
            if (selected < entry_count - 1)
                selected++;
            break;

        case KEY_RIGHT:
            enter_directory();
            break;

        case KEY_LEFT:
            go_parent();
            break;

        case 'q':
            endwin();
            return 0;
        }
    }
}
