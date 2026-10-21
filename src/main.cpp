#include <cstdio>
#include <cstring>

#include "app.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--version")) {
            printf("kennel %s\n", KENNEL_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "--help")) {
            printf("usage: kennel [--version]\n"
                   "environment: KENNEL_PLATFORM (id or ini path), KENNEL_ROOT (test tree prefix), KENNEL_ASSETS\n");
            return 0;
        }
    }
    return app.run(argc, argv);
}
