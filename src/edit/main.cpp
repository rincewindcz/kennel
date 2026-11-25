#include "edit.h"

// kennel-edit [FILE]   text editor; a FILE that does not exist is created on the first save
int main(int argc, char** argv) {
    static Edit edit;
    return edit.run(argc, argv);
}
