#include "model.h"
#include "msg.h"
#include "cmd.h"
#include "update.h"
#include "view.h"
#include "helpers.h"
#include "loaddir.h"
#include "trash.h"
#include "archive.h"
#include "../vendor/termbox2.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define PREVIEW_SNIFF_LEN 512
#define BUILD_TIMESTAMP __DATE__ " " __TIME__
#define PREVIEW_CONFIG_MAX_BYTES 16384

static PreviewRule g_preview_rules[PREVIEW_RULE_MAX];
static int g_preview_rule_count = 0;

/* main() is the only impure code in the program: the only place that calls
 * a tb_* function, and the only place that touches the filesystem or spawns
 * a process. Everything it decides is delegated to the pure update()/view()
 * core; everything it does is one Cmd at a time, translated from update()'s
 * output. */

static Msg msg_failed(const char *fmt, ...)
{
    Msg msg = { .type = MSG_OP_FAILED };

    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.error, sizeof(msg.error), fmt, args);
    va_end(args);

    return msg;
}

static void report_exec_failure(const char *prog)
{
    char msg[256];
    int len = snprintf(msg, sizeof(msg), "%s: %s\n", prog, strerror(errno));
    if (len > 0) {
        size_t wlen = (size_t)len < sizeof(msg) ? (size_t)len : sizeof(msg) - 1;
        write(STDERR_FILENO, msg, wlen);
    }
}

static Msg execute_rename(const char *from, const char *to)
{
    if (rename(from, to) != 0)
        return msg_failed("rename: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_create_file(const char *path)
{
    FILE *f = fopen(path, "wx");
    if (!f)
        return msg_failed("create file: %s", strerror(errno));
    fclose(f);
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_create_dir(const char *path)
{
    if (mkdir(path, 0755) != 0)
        return msg_failed("create directory: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

/* Runs argv[0] via fork+execvp (never a shell, so a filename can't inject a
 * command), capturing stderr into errbuf so a real OS error can be folded
 * into the MSG_OP_FAILED message. Returns 0 on a zero exit status. */
static int run_argv(char *const argv[], char *errbuf, size_t errbuf_len)
{
    int errpipe[2];
    if (pipe(errpipe) != 0)
        return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(errpipe[0]);
        dup2(errpipe[1], STDERR_FILENO);
        close(errpipe[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }

        execvp(argv[0], argv);
        report_exec_failure(argv[0]);
        _exit(EXIT_FAILURE);
    }

    close(errpipe[1]);
    if (errbuf && errbuf_len > 0) {
        ssize_t n = read(errpipe[0], errbuf, errbuf_len - 1);
        errbuf[n > 0 ? n : 0] = '\0';
        size_t len = strlen(errbuf);
        if (len > 0 && errbuf[len - 1] == '\n')
            errbuf[len - 1] = '\0';
    }
    close(errpipe[0]);

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int run_capture(char *const argv[], char *outbuf, size_t outbuf_len,
                        char *errbuf, size_t errbuf_len)
{
    int outpipe[2];
    int errpipe[2];
    if (pipe(outpipe) != 0)
        return -1;
    if (pipe(errpipe) != 0) {
        close(outpipe[0]);
        close(outpipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(outpipe[0]);
        dup2(outpipe[1], STDOUT_FILENO);
        close(outpipe[1]);

        close(errpipe[0]);
        dup2(errpipe[1], STDERR_FILENO);
        close(errpipe[1]);

        int devnull_in = open("/dev/null", O_RDONLY);
        if (devnull_in >= 0) {
            dup2(devnull_in, STDIN_FILENO);
            close(devnull_in);
        }

        execvp(argv[0], argv);
        report_exec_failure(argv[0]);
        _exit(EXIT_FAILURE);
    }

    close(outpipe[1]);
    close(errpipe[1]);

    size_t out_total = 0;
    if (outbuf && outbuf_len > 0) {
        ssize_t n;
        while (out_total < outbuf_len - 1 &&
               (n = read(outpipe[0], outbuf + out_total, outbuf_len - 1 - out_total)) > 0)
            out_total += (size_t)n;
        outbuf[out_total] = '\0';
    }
    close(outpipe[0]);

    if (errbuf && errbuf_len > 0) {
        ssize_t n = read(errpipe[0], errbuf, errbuf_len - 1);
        errbuf[n > 0 ? n : 0] = '\0';
        size_t len = strlen(errbuf);
        if (len > 0 && errbuf[len - 1] == '\n')
            errbuf[len - 1] = '\0';
    }
    close(errpipe[0]);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return (int)out_total;
}

static Msg execute_delete(const char *path, int is_dir)
{
    if (is_dir) {
        char errbuf[256] = { 0 };
        char *argv[] = { "rm", "-rf", (char *)path, NULL };
        if (run_argv(argv, errbuf, sizeof(errbuf)) != 0)
            return msg_failed("delete: %s", errbuf[0] ? errbuf : "failed");
        return (Msg){ .type = MSG_OP_SUCCEEDED };
    }

    if (unlink(path) != 0)
        return msg_failed("delete: %s", strerror(errno));
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_copy(const char *src, const char *dst)
{
    char errbuf[256] = { 0 };
    char *argv[] = { "cp", "-r", (char *)src, (char *)dst, NULL };
    if (run_argv(argv, errbuf, sizeof(errbuf)) != 0) {
        char *rm_argv[] = { "rm", "-rf", (char *)dst, NULL };
        run_argv(rm_argv, NULL, 0);
        return msg_failed("copy: %s", errbuf[0] ? errbuf : "failed");
    }
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_move(const char *src, const char *dst)
{
    char errbuf[256] = { 0 };
    char *argv[] = { "mv", (char *)src, (char *)dst, NULL };
    if (run_argv(argv, errbuf, sizeof(errbuf)) != 0)
        return msg_failed("move: %s", errbuf[0] ? errbuf : "failed");
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_launch_editor(const char *path)
{
    tb_shutdown();

    pid_t pid = fork();
    if (pid == 0) {
        execlp("vim", "vim", path, (char *)NULL);
        _exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

    tb_init();
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

/* Runs argv1 piped into argv2 (e.g. a hex dump piped into a pager), never
 * via a shell. Waits on both children so quitting the reader early can't
 * leave the writer as a zombie once it hits SIGPIPE. */
static void run_piped(char *const argv1[], char *const argv2[], const char *cwd)
{
    int fd[2];
    if (pipe(fd) != 0)
        return;

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        if (cwd)
            chdir(cwd);
        execvp(argv1[0], argv1);
        _exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        /* Without this, more's default exit-on-eof quits the instant it
         * hits EOF, which for content shorter than one screen is before it
         * even finishes drawing - the pager exits and dired repaints over
         * it before the user sees anything. */
        setenv("POSIXLY_CORRECT", "1", 1);
        execvp(argv2[0], argv2);
        _exit(EXIT_FAILURE);
    }

    close(fd[0]);
    close(fd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

static Msg execute_preview(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return msg_failed("preview: %s", strerror(errno));

    unsigned char sniff[PREVIEW_SNIFF_LEN];
    size_t n = fread(sniff, 1, sizeof(sniff), f);
    fclose(f);

    tb_shutdown();

    if (is_binary_content(sniff, n)) {
        char *dump_argv[] = { "hexdump", "-C", (char *)path, NULL };
        char *pager_argv[] = { "more", NULL };
        run_piped(dump_argv, pager_argv, NULL);
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            /* See run_piped()'s POSIXLY_CORRECT comment: without it, more
             * exits immediately on short files instead of pausing. */
            setenv("POSIXLY_CORRECT", "1", 1);
            execlp("more", "more", path, (char *)NULL);
            _exit(EXIT_FAILURE);
        } else {
            wait(NULL);
        }
    }

    tb_init();
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

#define ARCHIVE_LISTING_BUF_LEN (1 << 20)

static Msg execute_list_archive(const char *path, ArchiveFormat format,
                                 const char *display_name, int source_is_tmp)
{
    static char output[ARCHIVE_LISTING_BUF_LEN];
    static ArchiveMember members[MAX_ENTRIES];

    char errbuf[256] = { 0 };
    char *tar_argv[] = { "tar", "-tvf", (char *)path, NULL };
    char *unzip_argv[] = { "unzip", "-l", (char *)path, NULL };
    char *const *argv = (format == ARCHIVE_ZIP) ? unzip_argv : tar_argv;

    if (run_capture(argv, output, sizeof(output), errbuf, sizeof(errbuf)) < 0) {
        if (source_is_tmp)
            remove(path);
        return msg_failed("list archive: %s", errbuf[0] ? errbuf : "failed");
    }

    int count = (format == ARCHIVE_ZIP)
        ? parse_zip_listing(output, members, MAX_ENTRIES)
        : parse_tar_listing(output, members, MAX_ENTRIES);

    Msg msg = { .type = MSG_ARCHIVE_LISTED };
    msg.archive_listed.members = members;
    msg.archive_listed.member_count = count;
    msg.archive_listed.format = format;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(msg.archive_listed.path, sizeof(msg.archive_listed.path), "%s", path);
    snprintf(msg.archive_listed.display_name, sizeof(msg.archive_listed.display_name), "%s", display_name);
#pragma GCC diagnostic pop
    msg.archive_listed.source_is_tmp = source_is_tmp;
    return msg;
}

static int run_capture_to_fd(char *const argv[], int out_fd, char *errbuf, size_t errbuf_len)
{
    int errpipe[2];
    if (pipe(errpipe) != 0)
        return -1;

    pid_t pid = fork();
    if (pid == 0) {
        dup2(out_fd, STDOUT_FILENO);
        close(errpipe[0]);
        dup2(errpipe[1], STDERR_FILENO);
        close(errpipe[1]);

        int devnull_in = open("/dev/null", O_RDONLY);
        if (devnull_in >= 0) {
            dup2(devnull_in, STDIN_FILENO);
            close(devnull_in);
        }

        execvp(argv[0], argv);
        report_exec_failure(argv[0]);
        _exit(EXIT_FAILURE);
    }

    close(errpipe[1]);
    if (errbuf && errbuf_len > 0) {
        ssize_t n = read(errpipe[0], errbuf, errbuf_len - 1);
        errbuf[n > 0 ? n : 0] = '\0';
        size_t len = strlen(errbuf);
        if (len > 0 && errbuf[len - 1] == '\n')
            errbuf[len - 1] = '\0';
    }
    close(errpipe[0]);

    int status;
    waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int extract_member_to_fd(int fd, const char *archive_path, ArchiveFormat format,
                                 const char *member_path, char *errbuf, size_t errbuf_len)
{
    char *tar_argv[] = { "tar", "-xOf", (char *)archive_path, (char *)member_path, NULL };
    char *unzip_argv[] = { "unzip", "-p", (char *)archive_path, (char *)member_path, NULL };
    char *const *argv = (format == ARCHIVE_ZIP) ? unzip_argv : tar_argv;

    return run_capture_to_fd(argv, fd, errbuf, errbuf_len);
}

static int extract_member_to_tmp(const char *archive_path, ArchiveFormat format, const char *member_path,
                                  char *tmp_path, size_t tmp_path_size, char *errbuf, size_t errbuf_len)
{
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0')
        tmpdir = "/tmp";

    snprintf(tmp_path, tmp_path_size, "%s/dired-archive-XXXXXX", tmpdir);

    int fd = mkstemp(tmp_path);
    if (fd < 0) {
        snprintf(errbuf, errbuf_len, "%s", strerror(errno));
        return -1;
    }

    int rc = extract_member_to_fd(fd, archive_path, format, member_path, errbuf, errbuf_len);
    close(fd);

    if (rc != 0) {
        remove(tmp_path);
        return -1;
    }

    return 0;
}

static int extract_member_to_path(const char *archive_path, ArchiveFormat format, const char *member_path,
                                   const char *dest_path, char *errbuf, size_t errbuf_len)
{
    int fd = open(dest_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        snprintf(errbuf, errbuf_len, "%s", strerror(errno));
        return -1;
    }

    int rc = extract_member_to_fd(fd, archive_path, format, member_path, errbuf, errbuf_len);
    close(fd);

    if (rc != 0) {
        remove(dest_path);
        return -1;
    }

    return 0;
}

static Msg execute_extract_member(const char *archive_path, ArchiveFormat format, const char *member_path)
{
    char tmp_path[PATH_MAX_LEN];
    char errbuf[256] = { 0 };

    if (extract_member_to_tmp(archive_path, format, member_path, tmp_path, sizeof(tmp_path),
                               errbuf, sizeof(errbuf)) != 0)
        return msg_failed("extract: %s", errbuf[0] ? errbuf : "failed");

    Msg msg = { .type = MSG_MEMBER_EXTRACTED };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(msg.member_extracted.tmp_path, sizeof(msg.member_extracted.tmp_path), "%s", tmp_path);
    snprintf(msg.member_extracted.member_path, sizeof(msg.member_extracted.member_path), "%s", member_path);
#pragma GCC diagnostic pop
    return msg;
}

static Msg execute_extract_member_to(const char *archive_path, ArchiveFormat format,
                                      const char *member_path, const char *dest_path)
{
    char errbuf[256] = { 0 };

    if (extract_member_to_path(archive_path, format, member_path, dest_path, errbuf, sizeof(errbuf)) != 0)
        return msg_failed("extract: %s", errbuf[0] ? errbuf : "failed");

    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_open_archive_member(const char *archive_path, ArchiveFormat format,
                                        const char *member_path, int preview)
{
    char tmp_path[PATH_MAX_LEN];
    char errbuf[256] = { 0 };

    if (extract_member_to_tmp(archive_path, format, member_path, tmp_path, sizeof(tmp_path),
                               errbuf, sizeof(errbuf)) != 0)
        return msg_failed("extract: %s", errbuf[0] ? errbuf : "failed");

    chmod(tmp_path, 0400);

    Msg result = preview ? execute_preview(tmp_path) : execute_launch_editor(tmp_path);
    remove(tmp_path);
    return result;
}

static void walk_glob_matches(const char *abs_dir, const char *rel_prefix,
                               FilterType filter_type, const char *pattern,
                               Entry *out_entries, int *out_count, int *out_truncated)
{
    if (*out_count >= MAX_ENTRIES) {
        *out_truncated = 1;
        return;
    }

    DIR *dir = opendir(abs_dir);
    if (!dir)
        return;

    struct dirent *de;
    while ((de = readdir(dir))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        char abs_child[PATH_MAX_LEN];
        snprintf(abs_child, sizeof(abs_child), "%s/%s", abs_dir, de->d_name);

        char rel_child[PATH_MAX_LEN];
        if (rel_prefix[0] == '\0')
            snprintf(rel_child, sizeof(rel_child), "%s", de->d_name);
        else
            snprintf(rel_child, sizeof(rel_child), "%s/%s", rel_prefix, de->d_name);

        struct stat st;
        if (lstat(abs_child, &st) != 0)
            continue;

        if (filter_matches(rel_child, filter_type, pattern)) {
            if (*out_count >= MAX_ENTRIES) {
                *out_truncated = 1;
                break;
            }
            strncpy(out_entries[*out_count].name, rel_child, NAME_MAX_LEN);
            out_entries[*out_count].name[NAME_MAX_LEN] = '\0';
            out_entries[*out_count].st = st;
            (*out_count)++;
        }

        if (S_ISDIR(st.st_mode)) {
            walk_glob_matches(abs_child, rel_child, filter_type, pattern,
                               out_entries, out_count, out_truncated);
            if (*out_count >= MAX_ENTRIES) {
                *out_truncated = 1;
                break;
            }
        }
    }
    closedir(dir);
}

static Msg execute_build_glob(const char *cwd, GlobType glob_type, const char *pattern)
{
    static Entry scratch[MAX_ENTRIES];

    int count = 0;
    int truncated = 0;
    FilterType filter_type = (glob_type == GLOB_REGEX) ? FILTER_REGEX : FILTER_PLAIN;

    walk_glob_matches(cwd, "", filter_type, pattern, scratch, &count, &truncated);

    Msg msg = { .type = MSG_GLOB_BUILT };
    msg.glob_built.entries = scratch;
    msg.glob_built.entry_count = count;
    msg.glob_built.truncated = truncated;
    return msg;
}

static Msg execute_run_cmd(const char *cwd, const char *cmd_text, const char *selected_path)
{
    tb_shutdown();

    if (selected_path[0] != '\0')
        setenv("FILE", selected_path, 1);
    else
        unsetenv("FILE");

    char *sh_argv[] = { "/bin/sh", "-c", (char *)cmd_text, NULL };
    char *pager_argv[] = { "more", NULL };
    run_piped(sh_argv, pager_argv, cwd);

    tb_init();
    return (Msg){ .type = MSG_OP_SUCCEEDED };
}

static Msg execute_cmd(const Cmd *cmd)
{
    switch (cmd->type) {
    case CMD_LOAD_DIR:      return load_directory(cmd->path, cmd->show_hidden);
    case CMD_BUILD_GLOB:    return execute_build_glob(cmd->path, cmd->glob_type, cmd->cmd_text);
    case CMD_RENAME:        return execute_rename(cmd->path, cmd->path2);
    case CMD_CREATE_FILE:   return execute_create_file(cmd->path);
    case CMD_CREATE_DIR:    return execute_create_dir(cmd->path);
    case CMD_DELETE:        return execute_delete(cmd->path, cmd->is_dir);
    case CMD_TRASH:         return trash_item(cmd->path);
    case CMD_LAUNCH_EDITOR: return execute_launch_editor(cmd->path);
    case CMD_PREVIEW:       return execute_preview(cmd->path);
    case CMD_LIST_ARCHIVE:  return execute_list_archive(cmd->path, cmd->archive_format, cmd->path2, cmd->is_dir);
    case CMD_EXTRACT_MEMBER: return execute_extract_member(cmd->path, cmd->archive_format, cmd->path2);
    case CMD_EXTRACT_MEMBER_TO: return execute_extract_member_to(cmd->path, cmd->archive_format, cmd->path2, cmd->path3);
    case CMD_OPEN_ARCHIVE_MEMBER: return execute_open_archive_member(cmd->path, cmd->archive_format, cmd->path2, 0);
    case CMD_PREVIEW_ARCHIVE_MEMBER: return execute_open_archive_member(cmd->path, cmd->archive_format, cmd->path2, 1);
    case CMD_COPY:          return execute_copy(cmd->path, cmd->path2);
    case CMD_MOVE:          return execute_move(cmd->path, cmd->path2);
    case CMD_RUN:           return execute_run_cmd(cmd->path, cmd->cmd_text, cmd->selected_path);
    default:                return (Msg){ .type = MSG_NONE };
    }
}

static void style_colors(StyleTag style, uintattr_t *fg, uintattr_t *bg)
{
    switch (style) {
    case STYLE_SELECTED:
        /* Explicit colors instead of bare TB_REVERSE: some terminals don't
         * swap TB_DEFAULT/TB_DEFAULT visibly, which renders as no contrast
         * at all rather than a highlighted row. */
        *fg = TB_BLACK;
        *bg = TB_WHITE;
        break;
    case STYLE_ERROR:
        *fg = TB_RED;
        *bg = TB_DEFAULT;
        break;
    case STYLE_VALID:
        *fg = TB_GREEN;
        *bg = TB_DEFAULT;
        break;
    default:
        *fg = TB_DEFAULT;
        *bg = TB_DEFAULT;
        break;
    }
}

static void render(const Model *model)
{
    View v = view(model);

    tb_clear();
    for (int i = 0; i < v.line_count; i++) {
        uintattr_t fg, bg;
        style_colors(v.lines[i].style, &fg, &bg);

        int row = (i == v.line_count - 1) ? tb_height() - 1 : i;
        tb_print(0, row, fg, bg, v.lines[i].text);
    }
    tb_present();
}

/* Command keys (MSG_RENAME/MSG_NEW/MSG_QUIT) are only
 * recognized outside text-entry modes, so typing "r" while naming a file
 * inserts the letter instead of re-triggering rename. */
static Msg translate_event(struct tb_event ev, AppMode mode)
{
    Msg msg = { .type = MSG_NONE };

    if (ev.type == TB_EVENT_RESIZE) {
        msg.type = MSG_RESIZE;
        msg.resize.width = ev.w;
        msg.resize.height = ev.h;
        return msg;
    }

    if (ev.type != TB_EVENT_KEY)
        return msg;

    int text_entry = (mode == MODE_RENAME || mode == MODE_CREATE || mode == MODE_RUN_CMD ||
                       mode == MODE_FILTER || mode == MODE_GLOB);

    if (text_entry) {
        if (ev.key == TB_KEY_ESC)
            msg.type = MSG_CANCEL;
        else if (ev.key == TB_KEY_ENTER)
            msg.type = MSG_ACTIVATE;
        else if (ev.key == TB_KEY_BACKSPACE || ev.key == TB_KEY_BACKSPACE2 || ev.key == TB_KEY_DELETE)
            msg.type = MSG_DELETE;
        else if (ev.ch != 0 && isprint((int)ev.ch)) {
            msg.type = MSG_TEXT_INPUT;
            msg.ch = (char)ev.ch;
        }
        return msg;
    }

    switch (ev.key) {
    case TB_KEY_ARROW_UP:
        msg.type = MSG_MOVE_UP;
        return msg;
    case TB_KEY_ARROW_DOWN:
        msg.type = MSG_MOVE_DOWN;
        return msg;
    case TB_KEY_ARROW_LEFT:
        msg.type = MSG_GO_PARENT;
        return msg;
    case TB_KEY_ARROW_RIGHT:
    case TB_KEY_ENTER:
        msg.type = MSG_ACTIVATE;
        return msg;
    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
    case TB_KEY_DELETE:
        msg.type = MSG_DELETE;
        return msg;
    case TB_KEY_ESC:
        msg.type = MSG_CANCEL;
        return msg;
    default:
        break;
    }

    if (ev.ch == 'r' || ev.ch == 'R')
        msg.type = MSG_RENAME;
    else if (ev.ch == 'n')
        msg.type = MSG_NEW;
    else if (ev.ch == ':')
        msg.type = MSG_RUN_CMD;
    else if (ev.ch == 'f')
        msg.type = MSG_FILTER_PLAIN;
    else if (ev.ch == 'F')
        msg.type = MSG_FILTER_REGEX;
    else if (ev.ch == 'g')
        msg.type = MSG_GLOB_PLAIN;
    else if (ev.ch == 'G')
        msg.type = MSG_GLOB_REGEX;
    else if (ev.ch == ' ')
        msg.type = MSG_PREVIEW;
    else if (ev.ch == 'c')
        msg.type = MSG_YANK_COPY;
    else if (ev.ch == 'm')
        msg.type = MSG_YANK_MOVE;
    else if (ev.ch == 'p')
        msg.type = MSG_PASTE;
    else if (ev.ch == 's')
        msg.type = MSG_CYCLE_SORT;
    else if (ev.ch == 'd')
        msg.type = MSG_CYCLE_GROUP;
    else if (ev.ch == 'o')
        msg.type = MSG_CYCLE_PAGE;
    else if (ev.ch == 'a' || ev.ch == 'A')
        msg.type = MSG_TOGGLE_HIDDEN;
    else if (ev.ch == 'x')
        msg.type = MSG_DELETE_PERMANENT;
    else if (ev.ch == 'q')
        msg.type = MSG_QUIT;
    else if (ev.ch != 0) {
        msg.type = MSG_TEXT_INPUT;
        msg.ch = (char)ev.ch;
    }

    return msg;
}

static int detect_window_size(int *cols, int *rows)
{
    int fd = open("/dev/tty", O_RDONLY);
    if (fd < 0)
        return -1;

    struct winsize ws;
    int rc = ioctl(fd, TIOCGWINSZ, &ws);
    close(fd);
    if (rc != 0)
        return -1;

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

static void print_help(void)
{
    printf("dired - terminal file explorer\n\n");
    printf("Usage: dired [-help]\n\n");
    printf("Controls:\n");
    printf("  up/down       Navigate\n");
    printf("  left          Go to parent directory\n");
    printf("  right/Enter   Open file or enter directory\n");
    printf("  r             Rename selected file/directory\n");
    printf("  n             Create a new file or directory (trailing / for a directory)\n");
    printf("  :             Run a shell command (prefix with !, e.g. !unzip $FILE); $FILE is the selected entry\n");
    printf("  f             Filter listing by filename (plain substring)\n");
    printf("  F             Filter listing by filename (extended regex)\n");
    printf("  g             Recursively glob the current directory tree by filename (plain substring)\n");
    printf("  G             Recursively glob the current directory tree by filename (extended regex)\n");
    printf("  Esc           Cancel a pending yank (nav); cancel composing (Rename/Create/Filter/Run command)\n");
    printf("  space         Preview selected file (text pages, binary is hex-dumped)\n");
    printf("  c             Yank copy\n");
    printf("  m             Yank move\n");
    printf("  p             Paste\n");
    printf("  s             Cycle sort key/direction (name, date, size, extension)\n");
    printf("  d             Cycle directory grouping (first, last, mixed)\n");
    printf("  o             Jump to the next page (wraps to the first page)\n");
    printf("  a             Toggle hidden files\n");
    printf("  Backspace     Move selected file/directory to trash (~/.trash)\n");
    printf("  x             Permanently delete selected file/directory (bypasses trash)\n");
    printf("  q             Quit\n\n");
    printf("Build time: %s\n", BUILD_TIMESTAMP);

    int cols, rows;
    if (detect_window_size(&cols, &rows) == 0)
        printf("Detected window size: %d cols x %d rows\n", cols, rows);
    else
        printf("Detected window size: unavailable (%s)\n", strerror(errno));
}

static void load_preview_config(void)
{
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
        return;

    char config_dir[PATH_MAX_LEN];
    snprintf(config_dir, sizeof(config_dir), "%s/.config", home);
    if (mkdir(config_dir, 0755) != 0 && errno != EEXIST)
        return;

    char config_path[PATH_MAX_LEN];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(config_path, sizeof(config_path), "%s/dired", config_dir);
#pragma GCC diagnostic pop

    if (access(config_path, F_OK) != 0) {
        FILE *created = fopen(config_path, "w");
        if (!created)
            return;
        fclose(created);
    }

    FILE *f = fopen(config_path, "r");
    if (!f)
        return;

    char text[PREVIEW_CONFIG_MAX_BYTES];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';

    char errbuf[256];
    int count = parse_preview_rules(text, g_preview_rules, PREVIEW_RULE_MAX, errbuf, sizeof(errbuf));
    if (count < 0) {
        fprintf(stderr, "dired: %s: %s\n", config_path, errbuf);
        exit(EXIT_FAILURE);
    }

    g_preview_rule_count = count;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return EXIT_SUCCESS;
        }
    }

    load_preview_config();

    tb_init();

    Model model;
    memset(&model, 0, sizeof(model));
    model.mode = MODE_NAV;
    getcwd(model.current_path, sizeof(model.current_path));
    model.term_height = tb_height();
    model.term_width = tb_width();

    Cmd cmd = { .type = CMD_LOAD_DIR, .show_hidden = model.show_hidden };
    snprintf(cmd.path, sizeof(cmd.path), "%s", model.current_path);

    while (!model.should_quit) {
        if (cmd.type != CMD_NONE) {
            Msg outcome = execute_cmd(&cmd);
            Model next_model;
            Cmd next_cmd;
            update(&outcome, &model, &next_model, &next_cmd);
            model = next_model;
            cmd = next_cmd;
            continue;
        }

        render(&model);

        struct tb_event ev;
        tb_poll_event(&ev);
        Msg msg = translate_event(ev, model.mode);

        Model next_model;
        Cmd next_cmd;
        update(&msg, &model, &next_model, &next_cmd);
        model = next_model;
        cmd = next_cmd;
    }

    for (int i = 0; i < model.archive_depth; i++) {
        if (model.archive_stack[i].source_is_tmp)
            remove(model.archive_stack[i].source_path);
        free(model.archive_stack[i].members);
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
