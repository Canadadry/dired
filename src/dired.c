#include <ncurses.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#define MAX_ENTRIES 1024
#define NAME_MAX_LEN 1024
#define PATH_MAX_LEN 1024

/* =======================
   Touches applicatives
   ======================= */

typedef enum {
    APP_KEY_NONE = 0,
    APP_KEY_QUIT = 'q',
    APP_KEY_RENAME_LOWER = 'r',
    APP_KEY_RENAME_UPPER = 'R',
    APP_KEY_VALIDATE = '\n',
    APP_KEY_ESCAPE = 27,
    APP_KEY_BACKSPACE_ASCII = 8,
    APP_KEY_BACKSPACE_DEL = 127,

} AppKey;

/* =======================
   Structures
   ======================= */

typedef struct {
    char name[NAME_MAX_LEN + 1];
    struct stat st;
} Entry;

/* =======================
   État global
   ======================= */

static Entry entries[MAX_ENTRIES];
static int entry_count = 0;
static int selected = 0;

static char current_path[PATH_MAX_LEN];

static int rename_mode = 0;
static char rename_buf[NAME_MAX_LEN + 1];
static size_t rename_len = 0;

/* =======================
   Utilitaires
   ======================= */

static int is_protected_name(const char *name)
{
    return (!strcmp(name, ".") || !strcmp(name, ".."));
}

static void load_directory(const char *path)
{
    DIR *dir;
    struct dirent *de;
    char fullpath[PATH_MAX_LEN];

    entry_count = 0;
    dir = opendir(path);
    if (!dir)
        return;

    while ((de = readdir(dir)) && entry_count < MAX_ENTRIES) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, de->d_name);
        if (lstat(fullpath, &entries[entry_count].st) == 0) {
            strncpy(entries[entry_count].name, de->d_name, NAME_MAX_LEN);
            entries[entry_count].name[NAME_MAX_LEN] = '\0';
            entry_count++;
        }
    }

    closedir(dir);
}

static void mode_to_str(mode_t m, char *out)
{
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

/* =======================
   Affichage
   ======================= */

static void draw(void)
{
    clear();
    mvprintw(0, 0, "Path: %s", current_path);

    for (int i = 0; i < entry_count; i++) {
        char perms[11];
        mode_to_str(entries[i].st.st_mode, perms);

        if (i == selected)
            attron(A_REVERSE);

        if (rename_mode && i == selected) {
            mvprintw(i + 2, 0, "%s %8ld %s",
                     perms,
                     (long)entries[i].st.st_size,
                     rename_buf);
        } else {
            mvprintw(i + 2, 0, "%s %8ld %s",
                     perms,
                     (long)entries[i].st.st_size,
                     entries[i].name);
        }

        if (i == selected)
            attroff(A_REVERSE);
    }

    refresh();
}

/* =======================
   Navigation fichiers
   ======================= */

static void open_file_with_vim(const char *filename)
{
    endwin();

    pid_t pid = fork();
    if (pid == 0) {
        execlp("vim", "vim", filename, (char *)NULL);
        _exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
}

static void enter_selected(void)
{
    if (S_ISDIR(entries[selected].st.st_mode)) {
        if (is_protected_name(entries[selected].name))
            return;

        chdir(entries[selected].name);
        getcwd(current_path, sizeof(current_path));
        load_directory(current_path);
    } else if (S_ISREG(entries[selected].st.st_mode)) {
        open_file_with_vim(entries[selected].name);
        load_directory(current_path);
    }
}

static void go_parent(void)
{
    chdir("..");
    getcwd(current_path, sizeof(current_path));
    load_directory(current_path);
}

/* =======================
   Mode rename
   ======================= */

static void start_rename(void)
{
    if (is_protected_name(entries[selected].name))
        return;

    rename_mode = 1;
    strncpy(rename_buf, entries[selected].name, NAME_MAX_LEN);
    rename_buf[NAME_MAX_LEN] = '\0';
    rename_len = strlen(rename_buf);
    curs_set(1);
}

static void cancel_rename(void)
{
    rename_mode = 0;
    curs_set(0);
}

static void validate_rename(void)
{
    char oldpath[PATH_MAX_LEN];
    char newpath[PATH_MAX_LEN];

    snprintf(oldpath, sizeof(oldpath), "%s/%s",
             current_path, entries[selected].name);
    snprintf(newpath, sizeof(newpath), "%s/%s",
             current_path, rename_buf);

    if (strcmp(oldpath, newpath) != 0) {
        if (rename(oldpath, newpath) != 0) {
            mvprintw(LINES - 1, 0, "Rename error: %s", strerror(errno));
            getch();
        }
    }

    cancel_rename();
    load_directory(current_path);
}

/* =======================
   Suppression
   ======================= */

static void delete_selected(void)
{
    if (is_protected_name(entries[selected].name))
        return;

    mvprintw(LINES - 1, 0,
             "Delete '%s' ? [y/N] ",
             entries[selected].name);
    clrtoeol();
    refresh();

    int ch = getch();
    if (ch != 'y' && ch != 'Y')
        return;

    char fullpath[PATH_MAX_LEN];
    snprintf(fullpath, sizeof(fullpath), "%s/%s",
             current_path, entries[selected].name);

    int rc;
    if (S_ISDIR(entries[selected].st.st_mode))
        rc = rmdir(fullpath);
    else
        rc = unlink(fullpath);

    if (rc != 0) {
        mvprintw(LINES - 1, 0, "Delete error: %s", strerror(errno));
        getch();
    }

    if (selected >= entry_count - 1 && selected > 0)
        selected--;

    load_directory(current_path);
}

/* =======================
   Main
   ======================= */

int main(void)
{
    int ch;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    getcwd(current_path, sizeof(current_path));
    load_directory(current_path);

    while (1) {
        draw();
        ch = getch();

        if (rename_mode) {
            if (ch == APP_KEY_ESCAPE) {
                cancel_rename();
            } else if (ch == KEY_BACKSPACE || ch == KEY_DC || ch == 127) {
                if (rename_len > 0)
                    rename_buf[--rename_len] = '\0';
            } else if (ch == APP_KEY_VALIDATE) {
                validate_rename();
            } else if (isprint(ch) && rename_len < NAME_MAX_LEN) {
                rename_buf[rename_len++] = (char)ch;
                rename_buf[rename_len] = '\0';
            }
            continue;
        }

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
        case APP_KEY_VALIDATE:
            enter_selected();
            break;

        case KEY_LEFT:
            go_parent();
            break;

        case KEY_BACKSPACE:
        case KEY_DC:
        case APP_KEY_BACKSPACE_ASCII:
        case APP_KEY_BACKSPACE_DEL:
            delete_selected();
            break;

        case APP_KEY_RENAME_LOWER:
        case APP_KEY_RENAME_UPPER:
            start_rename();
            break;

        case APP_KEY_QUIT:
            endwin();
            return EXIT_SUCCESS;
        default:
            endwin();
            printf("unhandeled %d\n",ch);
            return EXIT_FAILURE;
        }
    }
}
