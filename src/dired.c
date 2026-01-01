#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

static struct termios orig_termios;

static void disable_raw_mode(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void enable_raw_mode(void)
{
    struct termios raw;

    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void cls(void)
{
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
}

static void move_cursor(int row, int col)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row + 1, col + 1);
    write(STDOUT_FILENO, buf, len);
}

int main(void)
{
    int x = 0, y = 0;
    char c;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "stdin/stdout is not a tty\n");
        return 1;
    }

    enable_raw_mode();
    cls();

    while (read(STDIN_FILENO, &c, 1) == 1) {
        switch (c) {
            case 'q':
                disable_raw_mode();
                cls();
                return 0;
            case 'h':
                x--;
                break;
            case 'l':
                x++;
                break;
            case 'k':
                y--;
                break;
            case 'j':
                y++;
                break;
        }

        if (x < 0) x = 0;
        if (y < 0) y = 0;

        cls();
        move_cursor(y, x);
        write(STDOUT_FILENO, "@", 1);
    }

    return 0;
}
