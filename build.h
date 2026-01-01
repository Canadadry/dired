#ifndef BUILD_H
#define BUILD_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <glob.h>
#include <time.h>
#include <unistd.h>

#define BUILD_CMD_MAX 65536
#define BUILD_PATH_MAX 4096
#define BUILD_MAX_TARGETS 32
#define BUILD_RUN_CMD(...) build_run_cmd(__VA_ARGS__, NULL)

typedef enum {
    BUILD_TARGET_EXE,
    BUILD_TARGET_LIB
} BuildTargetType;

typedef struct {
    BuildTargetType type;
    const char *entry;
    const char *output;
} BuildTarget;

typedef struct {
    const char *src_dir;
    const char *build_dir;
    const char *cflags;
    const char *ldflags;
    const char *cc;
    const char *ld;
    int release;

    BuildTarget targets[BUILD_MAX_TARGETS];
    int target_count;
} BuildCtx;

static inline BuildCtx build_init(void) {
    BuildCtx ctx = {0};
    ctx.cflags = "";
    ctx.ldflags = "";
    ctx.cc = "cc";
    ctx.ld = "cc";
    ctx.release = 0;
    ctx.target_count = 0;
    return ctx;
}

static inline void build_set_src_dir(BuildCtx *ctx, const char *dir) { ctx->src_dir = dir; }
static inline void build_set_build_dir(BuildCtx *ctx, const char *dir) { ctx->build_dir = dir; }
static inline void build_set_cflags(BuildCtx *ctx, const char *flags) { ctx->cflags = flags; }
static inline void build_set_ldflags(BuildCtx *ctx, const char *flags) { ctx->ldflags = flags; }
static inline void build_set_release(BuildCtx *ctx, const char *opt_flags) { ctx->release = 1; ctx->cflags = opt_flags; }
static inline void build_set_cc(BuildCtx *ctx, const char *compiler) { ctx->cc = compiler; }
static inline void build_set_ld(BuildCtx *ctx, const char *linker) { ctx->ld = linker; }

static inline void build_make_dir(const char *dir) {
    struct stat st = {0};
    if (stat(dir, &st) == -1) mkdir(dir, 0755);
}

static inline int build_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static inline void build_run_cmd(const char *first, ...) {
    va_list args;
    va_start(args, first);

    char cmd[BUILD_CMD_MAX];
    snprintf(cmd, sizeof(cmd), "%s", first);

    const char *arg;
    while ((arg = va_arg(args, const char*))) {
        strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
        strncat(cmd, arg, sizeof(cmd) - strlen(cmd) - 1);
    }

    va_end(args);
    printf("RUN: %s\n", cmd);
    system(cmd);
}


#define build_has_arg(argc,argv,...) __build_has_arg((argc),(argv),__VA_ARGS__, NULL)

static inline int __build_has_arg(int argc, char **argv, ...) {
    va_list args;
    va_start(args, argv);

    const char *alias = NULL;
    while ((alias = va_arg(args, const char*))) {
        for (int i = 1; i < argc; i++) {
            const char * arg_val =argv[i];
            if (strcmp(arg_val, alias) == 0) {
                va_end(args);
                return 1;
            }
        }
    }
    va_end(args);
    return 0;
}


static inline void build_compile(BuildCtx *ctx, const char *pattern) {
    char path[BUILD_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", ctx->src_dir, pattern);

    glob_t globbuf;
    if (glob(path, 0, NULL, &globbuf) != 0) return;

    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
        const char *src = globbuf.gl_pathv[i];
        const char *filename = strrchr(src, '/');
        filename = filename ? filename + 1 : src;

        char obj[BUILD_PATH_MAX];
        snprintf(obj, sizeof(obj), "%s/%s.o", ctx->build_dir, filename);

        int compile_needed = 1;
        struct stat src_stat, obj_stat;
        if (stat(src, &src_stat) == 0 && stat(obj, &obj_stat) == 0) {
            if (difftime(src_stat.st_mtime, obj_stat.st_mtime) <= 0)
                compile_needed = 0;
        }

        if (compile_needed) {
            BUILD_RUN_CMD(ctx->cc, "-c", src, "-o", obj, ctx->cflags);
        } else {
            printf("SKIP: %s is up to date\n", obj);
        }
    }
    globfree(&globbuf);
}

static inline void build_add_entry_point(BuildCtx *ctx, const char *entry_c, const char *output) {
    if (ctx->target_count < BUILD_MAX_TARGETS) {
        ctx->targets[ctx->target_count++] = (BuildTarget){BUILD_TARGET_EXE, entry_c, output};
    }
}

static inline void build_add_static_lib(BuildCtx *ctx, const char *output) {
    if (ctx->target_count < BUILD_MAX_TARGETS) {
        ctx->targets[ctx->target_count++] = (BuildTarget){BUILD_TARGET_LIB, NULL, output};
    }
}

static inline void build_link_all(BuildCtx *ctx) {
    for (int t = 0; t < ctx->target_count; t++) {
        BuildTarget *target = &ctx->targets[t];
        char objs[BUILD_CMD_MAX] = {0};
        DIR *d = opendir(ctx->build_dir);
        if (!d) continue;

        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strstr(e->d_name, ".o")) continue;

            int is_other_entry = 0;
            for (int i = 0; i < ctx->target_count; i++) {
                if (ctx->targets[i].type != BUILD_TARGET_EXE) continue;
                if (!ctx->targets[i].entry) continue;

                char expected[BUILD_PATH_MAX];
                snprintf(expected, sizeof(expected), "%s.o", ctx->targets[i].entry);

                if (strcmp(e->d_name, expected) == 0) {
                    if (target->type == BUILD_TARGET_EXE &&
                        strcmp(ctx->targets[i].entry, target->entry) == 0) {
                        continue;
                    } else {
                        is_other_entry = 1;
                        break;
                    }
                }
            }
            if (is_other_entry) continue;

            char obj_path[BUILD_PATH_MAX];
            snprintf(obj_path, sizeof(obj_path), "%s/%s", ctx->build_dir, e->d_name);
            strncat(objs, obj_path, sizeof(objs) - strlen(objs) - 1);
            strncat(objs, " ", sizeof(objs) - strlen(objs) - 1);
        }
        closedir(d);

        char out_path[BUILD_PATH_MAX];
        snprintf(out_path, sizeof(out_path), "%s/%s", ctx->build_dir, target->output);

        if (target->type == BUILD_TARGET_EXE) {
            BUILD_RUN_CMD(ctx->ld, objs, "-o", out_path, ctx->ldflags);
        } else if (target->type == BUILD_TARGET_LIB) {
            BUILD_RUN_CMD("ar", "rcs", out_path, objs);
        }
    }
}

#endif // BUILD_H
