#include "tokenizer.h"
#include <cstdlib>
#include <cstring>

static void* xmalloc(std::size_t n) {
    void* p = std::malloc(n);
    if (!p) std::abort();
    return p;
}

static void* xrealloc(void* p, std::size_t n) {
    void* q = std::realloc(p, n);
    if (!q) std::abort();
    return q;
}

void tokenlist_init(TokenList* tl) {
    tl->a = nullptr;
    tl->n = 0;
    tl->cap = 0;
}

void tokenlist_clear(TokenList* tl) {
    for (u32 i = 0; i < tl->n; ++i) std::free(tl->a[i].s);
    tl->n = 0;
}

void tokenlist_free(TokenList* tl) {
    tokenlist_clear(tl);
    std::free(tl->a);
    tl->a = nullptr;
    tl->cap = 0;
}

static void tokenlist_push(TokenList* tl, const char* s, u32 len, u32 pos) {
    if (tl->n == tl->cap) {
        tl->cap = tl->cap ? (tl->cap * 2) : 64;
        tl->a = (Token*)xrealloc(tl->a, (std::size_t)tl->cap * sizeof(Token));
    }
    char* dup = (char*)xmalloc((std::size_t)len + 1);
    if (len) std::memcpy(dup, s, len);
    dup[len] = 0;
    tl->a[tl->n].s = dup;
    tl->a[tl->n].len = len;
    tl->a[tl->n].pos = pos;
    tl->n++;
}

static bool utf8_decode_at(const char* s, std::size_t n, std::size_t i, u32* cp, std::size_t* len) {
    if (i >= n) return false;
    unsigned char c0 = (unsigned char)s[i];

    if (c0 < 0x80) { *cp = (u32)c0; *len = 1; return true; }

    if ((c0 & 0xE0) == 0xC0) {
        if (i + 1 >= n) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        if ((c1 & 0xC0) != 0x80) return false;
        u32 v = ((u32)(c0 & 0x1F) << 6) | (u32)(c1 & 0x3F);
        if (v < 0x80) return false;
        *cp = v; *len = 2; return true;
    }

    if ((c0 & 0xF0) == 0xE0) {
        if (i + 2 >= n) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        unsigned char c2 = (unsigned char)s[i + 2];
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) return false;
        u32 v = ((u32)(c0 & 0x0F) << 12) | ((u32)(c1 & 0x3F) << 6) | (u32)(c2 & 0x3F);
        if (v < 0x800) return false;
        if (v >= 0xD800 && v <= 0xDFFF) return false;
        *cp = v; *len = 3; return true;
    }

    if ((c0 & 0xF8) == 0xF0) {
        if (i + 3 >= n) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        unsigned char c2 = (unsigned char)s[i + 2];
        unsigned char c3 = (unsigned char)s[i + 3];
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) return false;
        u32 v = ((u32)(c0 & 0x07) << 18) | ((u32)(c1 & 0x3F) << 12) | ((u32)(c2 & 0x3F) << 6) | (u32)(c3 & 0x3F);
        if (v < 0x10000) return false;
        if (v > 0x10FFFF) return false;
        *cp = v; *len = 4; return true;
    }

    return false;
}

static int utf8_encode(u32 cp, char out[4]) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static inline bool is_cyrillic(u32 cp) { return (cp >= 0x0400 && cp <= 0x052F); }
static inline bool is_ascii_alnum(u32 cp) { return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'); }
static inline bool is_alnum_ru_en(u32 cp) { return is_ascii_alnum(cp) || is_cyrillic(cp); }

static inline u32 to_lower_ru_en(u32 cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + (u32)('a' - 'A');
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

static inline u32 fold_yo(u32 cp) {
    if (cp == 0x0451) return 0x0435;
    if (cp == 0x0401) return 0x0415;
    return cp;
}

char* normalize_term_utf8(const char* raw, std::size_t raw_len, const TokenizerConfig* cfg, u32* out_len) {
    if (!raw) {
        if (out_len) *out_len = 0;
        char* z = (char*)xmalloc(1);
        z[0] = 0;
        return z;
    }

    std::size_t cap = raw_len ? raw_len : 1;
    char* out = (char*)xmalloc(cap + 1);
    std::size_t w = 0;

    std::size_t i = 0;
    while (i < raw_len) {
        u32 cp = 0;
        std::size_t l = 0;
        if (!utf8_decode_at(raw, raw_len, i, &cp, &l)) { i += 1; continue; }

        cp = to_lower_ru_en(cp);
        if (cfg && cfg->fold_yo_to_e) cp = fold_yo(cp);

        char enc[4];
        int el = utf8_encode(cp, enc);

        if (w + (std::size_t)el > cap) {
            cap = cap * 2 + (std::size_t)el;
            out = (char*)xrealloc(out, cap + 1);
        }

        for (int k = 0; k < el; ++k) out[w++] = enc[k];
        i += l;
    }

    out[w] = 0;
    if (out_len) *out_len = (u32)w;
    return out;
}

static inline bool is_joiner(u32 cp) { return (cp == (u32)'-' || cp == (u32)'_'); }

static bool next_is_alnum_ru_en(const char* s, std::size_t n, std::size_t i, const TokenizerConfig* cfg) {
    u32 cp = 0;
    std::size_t l = 0;
    if (!utf8_decode_at(s, n, i, &cp, &l)) return false;
    cp = to_lower_ru_en(cp);
    if (cfg && cfg->fold_yo_to_e) cp = fold_yo(cp);
    return is_alnum_ru_en(cp);
}

static void flush_token(TokenList* out, char* cur, u32* cur_len, u32* pos_counter, const TokenizerConfig* cfg) {
    if (*cur_len == 0) return;

    u32 b = 0;
    u32 e = *cur_len;

    while (b < e && (cur[b] == '-' || cur[b] == '_')) b++;
    while (e > b && (cur[e - 1] == '-' || cur[e - 1] == '_')) e--;

    if (e > b) {
        u32 maxb = cfg ? cfg->max_token_bytes : 0;
        u32 len = e - b;
        if (maxb && len > maxb) len = maxb;
        tokenlist_push(out, cur + b, len, *pos_counter);
        (*pos_counter)++;
    }

    *cur_len = 0;
}

int tokenize_utf8_with_positions(const char* text, std::size_t text_len,
                                 TokenList* out, const TokenizerConfig* cfg) {
    if (!out) return 0;

    tokenlist_clear(out);

    u32 maxb = (cfg && cfg->max_token_bytes) ? cfg->max_token_bytes : 256;
    if (maxb < 8) maxb = 8;
    if (maxb > 4096) maxb = 4096;

    char* cur = (char*)xmalloc((std::size_t)maxb + 1);
    u32 cur_len = 0;
    u32 pos_counter = 0;

    std::size_t i = 0;
    while (i < text_len) {
        u32 cp = 0;
        std::size_t l = 0;

        if (!utf8_decode_at(text, text_len, i, &cp, &l)) {
            flush_token(out, cur, &cur_len, &pos_counter, cfg);
            i += 1;
            continue;
        }

        cp = to_lower_ru_en(cp);
        if (cfg && cfg->fold_yo_to_e) cp = fold_yo(cp);

        if (cfg && cfg->keep_joiners && is_joiner(cp)) {
            std::size_t ni = i + l;
            if (cur_len > 0 && next_is_alnum_ru_en(text, text_len, ni, cfg)) {
                if (cur_len + 1 <= maxb) cur[cur_len++] = (char)cp;
            } else {
                flush_token(out, cur, &cur_len, &pos_counter, cfg);
            }
            i += l;
            continue;
        }

        if (is_alnum_ru_en(cp)) {
            char enc[4];
            int el = utf8_encode(cp, enc);
            if ((u32)el <= maxb - cur_len) {
                for (int k = 0; k < el; ++k) cur[cur_len++] = enc[k];
            }
        } else {
            flush_token(out, cur, &cur_len, &pos_counter, cfg);
        }

        i += l;
    }

    flush_token(out, cur, &cur_len, &pos_counter, cfg);
    std::free(cur);
    return 1;
}
