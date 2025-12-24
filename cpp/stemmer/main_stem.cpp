#include "tokenizer.h"
#include "stemmer_ru.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>

static void die(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    std::exit(2);
}

int main() {
    const char* path = "../tokenizer/torens.txt";

    TokenizerConfig cfg{};
    cfg.max_token_bytes = 256;
    cfg.fold_yo_to_e = 1;
    cfg.keep_joiners = 1;

    std::ifstream in(path, std::ios::binary);
    if (!in) die("cannot open corpus");

    TokenList tl;
    tokenlist_init(&tl);

    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        int rc = tokenize_utf8_with_positions(line.data(), line.size(), &tl, &cfg);
        if (rc < 0) die("tokenize error");

        for (u32 i = 0; i < tl.n; ++i) {
            u32 slen = 0;
            char* stem = stem_ru_utf8(tl.a[i].s, tl.a[i].len, &slen);
            std::cout.write(stem, slen);
            std::cout << '\n';
            std::free(stem);
        }

        tokenlist_clear(&tl);
    }

    tokenlist_free(&tl);
    return 0;
}
