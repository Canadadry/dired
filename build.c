#include "build.h"

#define TARGET_DIRED "dired"
#define TARGET_EDITOR "editor"

#define MACOS_FLAGS ""

void build_lib(int debug){
    BuildCtx ctx = build_init();
    build_set_src_dir(&ctx, "src");
    build_set_build_dir(&ctx, "build");
    build_make_dir(ctx.build_dir);

    if (!debug){
        build_set_cflags(&ctx, "-Wall -O2");
        build_set_ldflags(&ctx, "-lncurses "MACOS_FLAGS);
        build_add_static_lib(&ctx, "lib"TARGET_DIRED".a");
        build_add_entry_point(&ctx, "dired.c",TARGET_DIRED);
        build_add_entry_point(&ctx, "editor.c",TARGET_EDITOR);
    }else{
        build_set_cflags(&ctx, "-Wall -Werror -g -fsanitize=address -DBUILD_DEBUG");
        build_set_ldflags(&ctx, "-fsanitize=address -lncurses"MACOS_FLAGS);
        build_add_static_lib(&ctx, "lib"TARGET_DIRED"d.a");
        build_add_entry_point(&ctx, "dired.c",TARGET_DIRED"d");
        build_add_entry_point(&ctx, "editor.c",TARGET_EDITOR"d");
    }

    build_compile(&ctx, "*.c");
    build_link_all(&ctx);
}

// void build_test(){
//     BuildCtx ctx = build_init();
//     build_set_src_dir(&ctx, "test");
//     build_set_build_dir(&ctx, "build/tests");
//     build_set_cflags(&ctx, "-Wall -g -fsanitize=address -DBUILD_DEBUG");
//     build_set_ldflags(&ctx, "-Lbuild -fsanitize=address -Lvendor/raylib/macos -lraylib -l"TARGET_DIRED"d "MACOS_FLAGS);

//     build_make_dir(ctx.build_dir);

//     build_add_entry_point(&ctx, "main.c", "run_testsd");

//     build_compile(&ctx, "*.c");
//     build_link_all(&ctx);
// }

int main(int argc, char **argv) {

    if (build_has_arg(argc, argv,  "clean")){
        BUILD_RUN_CMD("rm", "-rf", "build");
    }

    build_lib(build_has_arg(argc, argv,  "debug","test"));
    // if (build_has_arg(argc, argv,  "test")){
    //     build_test();
    //     BUILD_RUN_CMD("./build/tests/run_testsd");
    // }

    if (build_has_arg(argc, argv,  "dired")){
        if(build_has_arg(argc, argv, "debug","test")){
            BUILD_RUN_CMD("./build/"TARGET_DIRED"d");
        }else{
            BUILD_RUN_CMD("./build/"TARGET_DIRED);
        }
    }else if (build_has_arg(argc, argv,  "editor")){
        if(build_has_arg(argc, argv, "debug","test")){
            BUILD_RUN_CMD("./build/"TARGET_EDITOR"d","readme.md");
        }else{
            BUILD_RUN_CMD("./build/"TARGET_EDITOR,"readme.md");
        }
    }

    return 0;
}
