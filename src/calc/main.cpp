#include <cstdio>
#include <cstring>

#include "calc.h"
#include "eval.h"

// kennel-calc                          calculator
// kennel-calc --eval EXPR [--rad]      prints the value of EXPR (development)
int main(int argc, char** argv) {
    if (argc >= 3 && !strcmp(argv[1], "--eval")) {
        bool degrees = !(argc == 4 && !strcmp(argv[3], "--rad"));
        std::vector<calc::Token> tokens;
        if (!calc::parse(argv[2], tokens)) {
            fprintf(stderr, "cannot parse: %s\n", argv[2]);
            return 2;
        }
        calc::Result r = calc::evaluate(tokens, degrees);
        if (!r.ok) {
            printf("%s\n", r.error.empty() ? "Syntax error" : r.error.c_str());
            return 1;
        }
        printf("%s\n", calc::format(r.value, false).c_str());
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "usage: kennel-calc | --eval EXPR [--rad]\n");
        return 2;
    }
    static Calc calc;
    return calc.run(argc, argv);
}
