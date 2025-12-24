#include "tokenizer.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iostream>

static void die(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    std::exit(2);
}

int main(int argc, char** argv) {
    const char* in_path  = (argc >= 2) ? argv[1] : "../../corpus_small.txt";
    const char* out_path = (argc >= 3) ? argv[2] : "tokens.txt";

    TokenizerConfig cfg{};
    cfg.max_token_bytes = 256;
    cfg.fold_yo_to_e = 1;
    cfg.keep_joiners = 1;

    std::ifstream in(in_path, std::ios::binary);
    if (!in) die("cannot open corpus");

    std::ofstream out(out_path, std::ios::binary);
    if (!out) die("cannot open output");

    TokenList tl;
    tokenlist_init(&tl);

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        int rc = tokenize_utf8_with_positions(line.data(), line.size(), &tl, &cfg);
        if (rc < 0) die("tokenize error");

        for (u32 i = 0; i < tl.n; ++i) {
            out.write(tl.a[i].s, tl.a[i].len);
            out.put('\n');
        }

        tokenlist_clear(&tl);
    }

    tokenlist_free(&tl);
    return 0;
}
