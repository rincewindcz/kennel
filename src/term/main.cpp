#include "term.h"

// kennel-term [--cwd DIR] [-e COMMAND]   terminal with an on-screen keyboard
int main(int argc, char** argv) {
    static Term term;
    return term.run(argc, argv);
}
