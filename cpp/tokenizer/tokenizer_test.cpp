#include "tokenizer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static void fail(const char* name, const char* msg) {
    std::fprintf(stderr, "TEST FAILED: %s\n%s\n", name, msg);
    std::exit(1);
}

static void require_true(const char* name, bool ok, const char* msg) {
    if (!ok) fail(name, msg);
}

static void require_u32_eq(const char* name, u32 got, u32 exp) {
    if (got != exp) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "Expected: %u\nGot:      %u", (unsigned)exp, (unsigned)got);
        fail(name, buf);
    }
}

static void require_bytes_eq(const char* name, const char* got, u32 got_len, const char* exp) {
    u32 exp_len = (u32)std::strlen(exp);
    if (got_len != exp_len || std::memcmp(got, exp, exp_len) != 0) {
        std::fprintf(stderr, "TEST FAILED: %s\nExpected: '%s'\nGot:      '%.*s'\n",
                     name, exp, (int)got_len, got ? got : "");
        std::exit(1);
    }
}

static void run_tok(const char* name, const char* text, const TokenizerConfig* cfg, TokenList* out) {
    tokenlist_init(out);
    int rc = tokenize_utf8_with_positions(text, std::strlen(text), out, cfg);
    require_true(name, rc >= 0, "tokenize_utf8_with_positions returned error");
}

static void check_tokens(const char* name,
                         const TokenList* tl,
                         const char* const* exp_terms,
                         const u32* exp_pos,
                         u32 exp_n) {
    require_u32_eq(name, tl->n, exp_n);
    for (u32 i = 0; i < exp_n; ++i) {
        const Token* t = &tl->a[i];
        require_bytes_eq(name, t->s, t->len, exp_terms[i]);
        require_u32_eq(name, t->pos, exp_pos[i]);
    }
}

static TokenizerConfig cfg_default() {
    TokenizerConfig cfg;
    cfg.max_token_bytes = 256;
    cfg.fold_yo_to_e = 0;
    cfg.keep_joiners = 1;
    return cfg;
}

static void test_ascii_lower() {
    TokenizerConfig cfg = cfg_default();
    TokenList tl;
    run_tok("ascii_lower_run", "Hello, WORLD!", &cfg, &tl);

    const char* exp[] = {"hello", "world"};
    const u32 pos[] = {0, 1};
    check_tokens("ascii_lower", &tl, exp, pos, 2);

    tokenlist_free(&tl);
}

static void test_cyrillic_lower() {
    TokenizerConfig cfg = cfg_default();
    TokenList tl;
    run_tok("cyrillic_lower_run", u8"ЭсМинЕц конвой", &cfg, &tl);

    const char* exp[] = {u8"эсминец", u8"конвой"};
    const u32 pos[] = {0, 1};
    check_tokens("cyrillic_lower", &tl, exp, pos, 2);

    tokenlist_free(&tl);
}

static void test_fold_yo_to_e() {
    TokenizerConfig cfg = cfg_default();
    cfg.fold_yo_to_e = 1;

    TokenList tl;
    run_tok("fold_yo_to_e_run", u8"самолёт самолет ЁЖИК ежик", &cfg, &tl);

    const char* exp[] = {u8"самолет", u8"самолет", u8"ежик", u8"ежик"};
    const u32 pos[] = {0, 1, 2, 3};
    check_tokens("fold_yo_to_e", &tl, exp, pos, 4);

    tokenlist_free(&tl);
}

static void test_no_fold_yo_to_e() {
    TokenizerConfig cfg = cfg_default();
    cfg.fold_yo_to_e = 0;

    TokenList tl;
    run_tok("no_fold_yo_to_e_run", u8"самолёт самолет", &cfg, &tl);

    const char* exp[] = {u8"самолёт", u8"самолет"};
    const u32 pos[] = {0, 1};
    check_tokens("no_fold_yo_to_e", &tl, exp, pos, 2);

    tokenlist_free(&tl);
}

static void test_joiners_keep() {
    TokenizerConfig cfg = cfg_default();
    cfg.keep_joiners = 1;

    TokenList tl;
    run_tok("joiners_keep_run", u8"Ан-12 CH-53 F-19 a_b _lead -trail a__b", &cfg, &tl);

    const char* exp[] = {u8"ан-12", "ch-53", "f-19", "a_b", "lead", "trail", "a", "b"};
    const u32 pos[] = {0, 1, 2, 3, 4, 5, 6, 7};
    check_tokens("joiners_keep", &tl, exp, pos, 8);

    tokenlist_free(&tl);
}

static void test_joiners_disabled() {
    TokenizerConfig cfg = cfg_default();
    cfg.keep_joiners = 0;

    TokenList tl;
    run_tok("joiners_disabled_run", u8"Ан-12 CH-53 a_b", &cfg, &tl);

    const char* exp[] = {u8"ан", "12", "ch", "53", "a", "b"};
    const u32 pos[] = {0, 1, 2, 3, 4, 5};
    check_tokens("joiners_disabled", &tl, exp, pos, 6);

    tokenlist_free(&tl);
}

static void test_positions_simple() {
    TokenizerConfig cfg = cfg_default();
    TokenList tl;
    run_tok("positions_run", "a b a", &cfg, &tl);

    const char* exp[] = {"a", "b", "a"};
    const u32 pos[] = {0, 1, 2};
    check_tokens("positions", &tl, exp, pos, 3);

    tokenlist_free(&tl);
}

static void test_max_token_bytes() {
    TokenizerConfig cfg = cfg_default();
    cfg.max_token_bytes = 16;

    char s[128];
    for (int i = 0; i < 100; ++i) s[i] = 'a';
    s[100] = 0;

    TokenList tl;
    run_tok("max_token_bytes_run", s, &cfg, &tl);

    require_u32_eq("max_token_bytes_n", tl.n, 1);
    require_u32_eq("max_token_bytes_len", tl.a[0].len, 16);

    for (u32 i = 0; i < tl.a[0].len; ++i) {
        require_true("max_token_bytes_content", tl.a[0].s[i] == 'a', "token content mismatch");
    }

    tokenlist_free(&tl);
}

static void test_invalid_utf8_delimiter() {
    TokenizerConfig cfg = cfg_default();

    char s[4];
    s[0] = 'a';
    s[1] = (char)0xFF;
    s[2] = 'b';
    s[3] = 0;

    TokenList tl;
    run_tok("invalid_utf8_run", s, &cfg, &tl);

    const char* exp[] = {"a", "b"};
    const u32 pos[] = {0, 1};
    check_tokens("invalid_utf8_delimiter", &tl, exp, pos, 2);

    tokenlist_free(&tl);
}

static void test_normalize_term() {
    TokenizerConfig cfg = cfg_default();
    cfg.fold_yo_to_e = 1;

    u32 out_len = 0;
    const char* raw = u8"ЁЖИК-12";
    char* norm = normalize_term_utf8(raw, std::strlen(raw), &cfg, &out_len);
    require_true("normalize_term_alloc", norm != nullptr, "normalize_term_utf8 returned null");

    require_bytes_eq("normalize_term_value", norm, out_len, u8"ежик-12");

    std::free(norm);
}

int main() {
    test_ascii_lower();
    test_cyrillic_lower();
    test_fold_yo_to_e();
    test_no_fold_yo_to_e();
    test_joiners_keep();
    test_joiners_disabled();
    test_positions_simple();
    test_max_token_bytes();
    test_invalid_utf8_delimiter();
    test_normalize_term();
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
