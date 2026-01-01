#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

static void disable_flow_control(void) {
    struct termios tio;
    tcgetattr(STDIN_FILENO, &tio);
    tio.c_iflag &= ~(IXON | IXOFF);
    tcsetattr(STDIN_FILENO, TCSANOW, &tio);
}

#define MAX_LINES 256
#define MAX_COLS  256

static char buffer[MAX_LINES][MAX_COLS];
static int line_count = 0;

static int cx = 0;
static int cy = 0;

static char filename[256];

static void load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[MAX_COLS];
    while (fgets(line, sizeof(line), f) && line_count < MAX_LINES) {
        line[strcspn(line, "\n")] = '\0';
        strncpy(buffer[line_count], line, MAX_COLS - 1);
        line_count++;
    }

    fclose(f);
}

static void save_file(void) {
    FILE *f = fopen(filename, "w");
    if (!f)
        return;

    for (int i = 0; i < line_count; i++) {
        fputs(buffer[i], f);
        fputc('\n', f);
    }

    fclose(f);
}

static void draw(void) {
    clear();

    for (int i = 0; i < line_count; i++) {
        mvprintw(i, 0, "%s", buffer[i]);
    }

    mvprintw(LINES - 1, 0,
             "Ctrl-S: save | Ctrl-Q: quit");

    move(cy, cx);
    refresh();
}

static void insert_char(int ch) {
    char *line = buffer[cy];
    int len = strlen(line);

    if (len >= MAX_COLS - 1 || cx > len)
        return;

    memmove(&line[cx + 1], &line[cx], len - cx + 1);
    line[cx] = ch;
    cx++;
}

static void backspace(void) {
    char *line = buffer[cy];
    int len = strlen(line);

    if (cx == 0 || len == 0)
        return;

    memmove(&line[cx - 1], &line[cx], len - cx + 1);
    cx--;
}

int main(int argc, char **argv) {
    int ch;

    if (argc < 2) {
        write(2, "usage: editor <file>\n", 21);
        return 1;
    }

    strncpy(filename, argv[1], sizeof(filename) - 1);
    load_file(filename);

    if (line_count == 0)
        line_count = 1;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    disable_flow_control();

    while (1) {
        draw();
        ch = getch();

        if (ch == CTRL('q')) {
            break;
        } else if (ch == CTRL('s')) {
            save_file();
        } else if (ch == KEY_LEFT) {
            if (cx > 0) cx--;
        } else if (ch == KEY_RIGHT) {
            if (cx < (int)strlen(buffer[cy])) cx++;
        } else if (ch == KEY_UP) {
            if (cy > 0) cy--;
            if (cx > (int)strlen(buffer[cy]))
                cx = strlen(buffer[cy]);
        } else if (ch == KEY_DOWN) {
            if (cy < line_count - 1) cy++;
            if (cx > (int)strlen(buffer[cy]))
                cx = strlen(buffer[cy]);
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            backspace();
        } else if (ch >= 32 && ch <= 126) {
            insert_char(ch);
        }
    }

    endwin();
    return 0;
}
