#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <cstddef>
#include <cstdint>

using u32 = uint32_t;

struct TokenizerConfig {
    u32 max_token_bytes;
    int fold_yo_to_e;
    int keep_joiners;
};

struct Token {
    char* s;
    u32 len;
    u32 pos;
};

struct TokenList {
    Token* a;
    u32 n;
    u32 cap;
};

void tokenlist_init(TokenList* tl);
void tokenlist_clear(TokenList* tl);
void tokenlist_free(TokenList* tl);

char* normalize_term_utf8(const char* raw, std::size_t raw_len, const TokenizerConfig* cfg, u32* out_len);

int tokenize_utf8_with_positions(const char* text, std::size_t text_len,
                                 TokenList* out, const TokenizerConfig* cfg);

#endif
