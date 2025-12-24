#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "..\tokenizer\tokenizer.h"

#if defined(_WIN32)
  #include <windows.h>
  #include <shellapi.h>
#endif

using u32 = uint32_t;
using u64 = uint64_t;

static void* xmalloc(size_t s) {
    void* p = std::malloc(s);
    if (!p) { std::fprintf(stderr, "nomem\n"); std::exit(1); }
    return p;
}

static void* xcalloc(size_t n, size_t s) {
    void* p = std::calloc(n, s);
    if (!p) { std::fprintf(stderr, "nomem\n"); std::exit(1); }
    return p;
}

static void* xrealloc(void* p, size_t s) {
    void* q = std::realloc(p, s);
    if (!q) { std::fprintf(stderr, "nomem\n"); std::exit(1); }
    return q;
}

static char* xstrdup(const char* s) {
    size_t n = std::strlen(s);
    char* p = (char*)xmalloc(n + 1);
    std::memcpy(p, s, n + 1);
    return p;
}

struct LexEntry { char* term; u64 offset; u32 df; };

static int lex_cmp_term(const void* a, const void* b) {
    const LexEntry* A = (const LexEntry*)a;
    const LexEntry* B = (const LexEntry*)b;
    return std::strcmp(A->term, B->term);
}

static int lex_cmp_key(const void* key, const void* elem) {
    const char* k = (const char*)key;
    const LexEntry* e = (const LexEntry*)elem;
    return std::strcmp(k, e->term);
}

static int vbyte_read_u32(FILE* f, u32* out) {
    u32 x = 0;
    u32 shift = 0;
    for (;;) {
        int c = std::fgetc(f);
        if (c == EOF) return 0;
        unsigned char b = (unsigned char)c;
        x |= (u32)(b & 127u) << shift;
        if ((b & 128u) == 0) break;
        shift += 7;
        if (shift > 28) return 0;
    }
    *out = x;
    return 1;
}

static int file_seek(FILE* f, u64 off) {
#if defined(_WIN32)
    return _fseeki64(f, (long long)off, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)off, SEEK_SET) == 0;
#endif
}

struct Posting {
    char* term;
    u32 df;
    u32 n;
    u32* doc;
    u32* tf;
};

struct PostCache {
    Posting** a;
    u32 n;
    u32 cap;
};

static void cache_init(PostCache* c) {
    c->a = nullptr;
    c->n = 0;
    c->cap = 0;
}

static Posting* cache_find(PostCache* c, const char* term) {
    for (u32 i = 0; i < c->n; ++i) {
        if (std::strcmp(c->a[i]->term, term) == 0) return c->a[i];
    }
    return nullptr;
}

static void cache_push(PostCache* c, Posting* p) {
    if (c->n == c->cap) {
        c->cap = c->cap ? (c->cap * 2) : 16;
        c->a = (Posting**)xrealloc(c->a, (size_t)c->cap * sizeof(Posting*));
    }
    c->a[c->n++] = p;
}

static Posting* load_posting(FILE* pf, const LexEntry* le) {
    Posting* p = (Posting*)xmalloc(sizeof(Posting));
    p->term = xstrdup(le->term);
    p->df = le->df;
    p->n = 0;
    p->doc = nullptr;
    p->tf = nullptr;

    if (!file_seek(pf, le->offset)) return p;

    u32 df0 = 0;
    if (!vbyte_read_u32(pf, &df0)) return p;
    u32 df = df0;
    p->n = df;
    p->doc = (u32*)xmalloc(sizeof(u32) * (size_t)df);
    p->tf = (u32*)xmalloc(sizeof(u32) * (size_t)df);

    u32 prev_doc = 0;
    for (u32 j = 0; j < df; ++j) {
        u32 doc_gap = 0;
        u32 tf = 0;
        if (!vbyte_read_u32(pf, &doc_gap)) { p->n = j; break; }
        if (!vbyte_read_u32(pf, &tf)) { p->n = j; break; }
        u32 doc = prev_doc + doc_gap;
        prev_doc = doc;

        u32 prev_pos = 0;
        for (u32 k = 0; k < tf; ++k) {
            u32 pos_gap = 0;
            if (!vbyte_read_u32(pf, &pos_gap)) { tf = k; break; }
            prev_pos += pos_gap;
        }

        p->doc[j] = doc;
        p->tf[j] = tf;
    }

    return p;
}

struct DocSet { u32* a; u32 n; };

static void docset_free(DocSet* s) {
    std::free(s->a);
    s->a = nullptr;
    s->n = 0;
}

static DocSet docset_copy_docs(const u32* a, u32 n) {
    DocSet r;
    r.n = n;
    r.a = n ? (u32*)xmalloc(sizeof(u32) * (size_t)n) : nullptr;
    if (n) std::memcpy(r.a, a, sizeof(u32) * (size_t)n);
    return r;
}

static DocSet docset_union(const DocSet* A, const DocSet* B) {
    DocSet R;
    R.a = (u32*)xmalloc(sizeof(u32) * (size_t)(A->n + B->n));
    u32 i = 0, j = 0, k = 0;
    while (i < A->n && j < B->n) {
        u32 x = A->a[i], y = B->a[j];
        if (x < y) { R.a[k++] = x; i++; }
        else if (y < x) { R.a[k++] = y; j++; }
        else { R.a[k++] = x; i++; j++; }
    }
    while (i < A->n) R.a[k++] = A->a[i++];
    while (j < B->n) R.a[k++] = B->a[j++];
    R.n = k;
    R.a = (u32*)xrealloc(R.a, sizeof(u32) * (size_t)R.n);
    return R;
}

static DocSet docset_intersect(const DocSet* A, const DocSet* B) {
    u32 cap = (A->n < B->n) ? A->n : B->n;
    DocSet R;
    R.a = cap ? (u32*)xmalloc(sizeof(u32) * (size_t)cap) : nullptr;
    u32 i = 0, j = 0, k = 0;
    while (i < A->n && j < B->n) {
        u32 x = A->a[i], y = B->a[j];
        if (x < y) i++;
        else if (y < x) j++;
        else { R.a[k++] = x; i++; j++; }
    }
    R.n = k;
    if (cap) R.a = (u32*)xrealloc(R.a, sizeof(u32) * (size_t)R.n);
    return R;
}

static DocSet docset_complement(const DocSet* A, u32 docs_count) {
    DocSet R;
    R.a = (u32*)xmalloc(sizeof(u32) * (size_t)docs_count);
    u32 i = 0, k = 0;
    for (u32 d = 1; d <= docs_count; ++d) {
        while (i < A->n && A->a[i] < d) i++;
        if (i < A->n && A->a[i] == d) continue;
        R.a[k++] = d;
    }
    R.n = k;
    R.a = (u32*)xrealloc(R.a, sizeof(u32) * (size_t)R.n);
    return R;
}

enum TokType { TOK_END=0, TOK_TERM=1, TOK_AND=2, TOK_OR=3, TOK_NOT=4, TOK_LP=5, TOK_RP=6 };

struct Tok { TokType t; char* s; };

static char* normalize_query_term(const char* raw, u32 raw_len, const TokenizerConfig* cfg) {
    u32 nlen = 0;
    char* norm = normalize_term_utf8(raw, raw_len, cfg, &nlen);
    if (!norm) return nullptr;

    u32 b = 0;
    u32 e = nlen;

    while (b < e && (norm[b] == '-' || norm[b] == '_')) b++;
    while (e > b && (norm[e - 1] == '-' || norm[e - 1] == '_')) e--;

    if (e <= b) { std::free(norm); return nullptr; }

    u32 maxb = cfg ? cfg->max_token_bytes : 0;
    u32 len = e - b;
    if (maxb && len > maxb) len = maxb;

    char* out = (char*)xmalloc((size_t)len + 1);
    std::memcpy(out, norm + b, len);
    out[len] = 0;
    std::free(norm);
    return out;
}

struct Lexer {
    const char* s;
    u32 n;
    u32 i;
    Tok cur;
    TokenizerConfig cfg;
};

static void lexer_init(Lexer* L, const char* s) {
    L->s = s ? s : "";
    L->n = (u32)std::strlen(L->s);
    L->i = 0;
    L->cur.t = TOK_END;
    L->cur.s = nullptr;
    L->cfg.max_token_bytes = 256;
    L->cfg.fold_yo_to_e = 1;
    L->cfg.keep_joiners = 1;
}

static void lexer_next(Lexer* L) {
    if (L->cur.s) { std::free(L->cur.s); L->cur.s = nullptr; }

    while (L->i < L->n) {
        unsigned char c = (unsigned char)L->s[L->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { L->i++; continue; }
        break;
    }

    if (L->i >= L->n) { L->cur.t = TOK_END; return; }

    char c = L->s[L->i];

    if (c == '&' && L->i + 1 < L->n && L->s[L->i + 1] == '&') {
        L->i += 2;
        L->cur.t = TOK_AND;
        return;
    }
    if (c == '|' && L->i + 1 < L->n && L->s[L->i + 1] == '|') {
        L->i += 2;
        L->cur.t = TOK_OR;
        return;
    }
    if (c == '!') {
        L->i += 1;
        L->cur.t = TOK_NOT;
        return;
    }
    if (c == '(') {
        L->i += 1;
        L->cur.t = TOK_LP;
        return;
    }
    if (c == ')') {
        L->i += 1;
        L->cur.t = TOK_RP;
        return;
    }

    u32 start = L->i;
    while (L->i < L->n) {
        char d = L->s[L->i];
        if (d == ' ' || d == '\t' || d == '\n' || d == '\r') break;
        if (d == '&' || d == '|' || d == '!' || d == '(' || d == ')') break;
        L->i++;
    }
    u32 raw_len = L->i - start;
    if (raw_len == 0) { L->cur.t = TOK_END; return; }

    char* t = normalize_query_term(L->s + start, raw_len, &L->cfg);
    if (!t) { lexer_next(L); return; }

    L->cur.t = TOK_TERM;
    L->cur.s = t;
}

enum NodeType { N_TERM=1, N_AND=2, N_OR=3, N_NOT=4 };

struct Node {
    int type;
    Node* a;
    Node* b;
    char* term;
};

static Node* node_new(int type) {
    Node* n = (Node*)xmalloc(sizeof(Node));
    n->type = type;
    n->a = nullptr;
    n->b = nullptr;
    n->term = nullptr;
    return n;
}

static Node* parse_expr(Lexer* L);

static Node* parse_primary(Lexer* L) {
    if (L->cur.t == TOK_TERM) {
        Node* n = node_new(N_TERM);
        n->term = L->cur.s;
        L->cur.s = nullptr;
        lexer_next(L);
        return n;
    }
    if (L->cur.t == TOK_LP) {
        lexer_next(L);
        Node* n = parse_expr(L);
        if (L->cur.t == TOK_RP) lexer_next(L);
        return n;
    }
    return node_new(N_TERM);
}

static Node* parse_unary(Lexer* L) {
    if (L->cur.t == TOK_NOT) {
        lexer_next(L);
        Node* n = node_new(N_NOT);
        n->a = parse_unary(L);
        return n;
    }
    return parse_primary(L);
}

static Node* parse_and(Lexer* L) {
    Node* left = parse_unary(L);
    while (L->cur.t == TOK_AND) {
        lexer_next(L);
        Node* r = parse_unary(L);
        Node* p = node_new(N_AND);
        p->a = left;
        p->b = r;
        left = p;
    }
    return left;
}

static Node* parse_or(Lexer* L) {
    Node* left = parse_and(L);
    while (L->cur.t == TOK_OR) {
        lexer_next(L);
        Node* r = parse_and(L);
        Node* p = node_new(N_OR);
        p->a = left;
        p->b = r;
        left = p;
    }
    return left;
}

static Node* parse_expr(Lexer* L) {
    return parse_or(L);
}

static void node_free(Node* n) {
    if (!n) return;
    node_free(n->a);
    node_free(n->b);
    if (n->term) std::free(n->term);
    std::free(n);
}

static Posting* get_posting(PostCache* cache, FILE* pf, const LexEntry* lex, u32 lex_n, const char* term) {
    Posting* c = cache_find(cache, term);
    if (c) return c;

    LexEntry* le = (LexEntry*)std::bsearch(term, lex, lex_n, sizeof(LexEntry), lex_cmp_key);
    if (!le) {
        Posting* p = (Posting*)xmalloc(sizeof(Posting));
        p->term = xstrdup(term);
        p->df = 0;
        p->n = 0;
        p->doc = nullptr;
        p->tf = nullptr;
        cache_push(cache, p);
        return p;
    }

    Posting* p = load_posting(pf, le);
    cache_push(cache, p);
    return p;
}

static DocSet eval_node(Node* n, PostCache* cache, FILE* pf, const LexEntry* lex, u32 lex_n, u32 docs_count) {
    if (!n) { DocSet z; z.a = nullptr; z.n = 0; return z; }

    if (n->type == N_TERM) {
        if (!n->term || !n->term[0]) { DocSet z; z.a = nullptr; z.n = 0; return z; }
        Posting* p = get_posting(cache, pf, lex, lex_n, n->term);
        return docset_copy_docs(p->doc, p->n);
    }

    if (n->type == N_NOT) {
        DocSet c = eval_node(n->a, cache, pf, lex, lex_n, docs_count);
        DocSet r = docset_complement(&c, docs_count);
        docset_free(&c);
        return r;
    }

    if (n->type == N_AND) {
        DocSet l = eval_node(n->a, cache, pf, lex, lex_n, docs_count);
        DocSet r = eval_node(n->b, cache, pf, lex, lex_n, docs_count);
        DocSet out = docset_intersect(&l, &r);
        docset_free(&l);
        docset_free(&r);
        return out;
    }

    if (n->type == N_OR) {
        DocSet l = eval_node(n->a, cache, pf, lex, lex_n, docs_count);
        DocSet r = eval_node(n->b, cache, pf, lex, lex_n, docs_count);
        DocSet out = docset_union(&l, &r);
        docset_free(&l);
        docset_free(&r);
        return out;
    }

    DocSet z; z.a = nullptr; z.n = 0; return z;
}

struct TermList { char** a; u32 n; u32 cap; };

static void termlist_init(TermList* t) { t->a = nullptr; t->n = 0; t->cap = 0; }

static int termlist_has(TermList* t, const char* s) {
    for (u32 i = 0; i < t->n; ++i) if (std::strcmp(t->a[i], s) == 0) return 1;
    return 0;
}

static void termlist_add(TermList* t, const char* s) {
    if (!s || !s[0]) return;
    if (termlist_has(t, s)) return;
    if (t->n == t->cap) {
        t->cap = t->cap ? (t->cap * 2) : 16;
        t->a = (char**)xrealloc(t->a, (size_t)t->cap * sizeof(char*));
    }
    t->a[t->n++] = xstrdup(s);
}

static void termlist_free(TermList* t) {
    for (u32 i = 0; i < t->n; ++i) std::free(t->a[i]);
    std::free(t->a);
    t->a = nullptr;
    t->n = 0;
    t->cap = 0;
}

static void collect_positive_terms(Node* n, int neg, TermList* out) {
    if (!n) return;
    if (n->type == N_NOT) { collect_positive_terms(n->a, !neg, out); return; }
    if (n->type == N_AND || n->type == N_OR) { collect_positive_terms(n->a, neg, out); collect_positive_terms(n->b, neg, out); return; }
    if (n->type == N_TERM) { if (!neg) termlist_add(out, n->term ? n->term : ""); return; }
}

struct ResItem { u32 doc; double score; };

static int res_cmp(const void* a, const void* b) {
    const ResItem* A = (const ResItem*)a;
    const ResItem* B = (const ResItem*)b;
    if (A->score > B->score) return -1;
    if (A->score < B->score) return 1;
    if (A->doc < B->doc) return -1;
    if (A->doc > B->doc) return 1;
    return 0;
}

static void score_and_print(const DocSet* res, TermList* qterms, PostCache* cache, FILE* pf, const LexEntry* lex, u32 lex_n, u32 docs_count) {
    if (res->n == 0) return;

    ResItem* items = (ResItem*)xmalloc(sizeof(ResItem) * (size_t)res->n);
    for (u32 i = 0; i < res->n; ++i) { items[i].doc = res->a[i]; items[i].score = 0.0; }

    int32_t* map = (int32_t*)xmalloc(sizeof(int32_t) * (size_t)(docs_count + 1));
    for (u32 d = 0; d <= docs_count; ++d) map[d] = -1;
    for (u32 i = 0; i < res->n; ++i) {
        u32 doc = items[i].doc;
        if (doc <= docs_count) map[doc] = (int32_t)i;
    }

    for (u32 ti = 0; ti < qterms->n; ++ti) {
        const char* t = qterms->a[ti];
        Posting* p = get_posting(cache, pf, lex, lex_n, t);
        u32 df = p->df;
        if (df == 0) continue;
        double idf = std::log((double)(docs_count + 1) / (double)(df + 1)) + 1.0;

        for (u32 k = 0; k < p->n; ++k) {
            u32 doc = p->doc[k];
            if (doc > docs_count) continue;
            int32_t idx = map[doc];
            if (idx >= 0) items[(u32)idx].score += (double)p->tf[k] * idf;
        }
    }

    std::qsort(items, res->n, sizeof(ResItem), res_cmp);

    for (u32 i = 0; i < res->n; ++i) {
        std::printf("%u\t%.6f\n", (unsigned)items[i].doc, items[i].score);
    }

    std::free(map);
    std::free(items);
}

static int query_has_ops(const char* q) {
    for (const char* p = q; *p; ++p) {
        if (*p == '&' || *p == '|' || *p == '!' || *p == '(' || *p == ')') return 1;
    }
    return 0;
}

static int load_docs_count(const char* direct_path, u32* out_docs) {
    FILE* f = std::fopen(direct_path, "rb");
    if (!f) return 0;
    u32 n = 0;
    if (std::fread(&n, 4, 1, f) != 1) { std::fclose(f); return 0; }
    std::fclose(f);
    *out_docs = n;
    return 1;
}

static int load_lexicon(const char* vocab_path, LexEntry** out_lex, u32* out_n) {
    FILE* f = std::fopen(vocab_path, "rb");
    if (!f) return 0;

    u32 n = 0;
    if (std::fread(&n, 4, 1, f) != 1) { std::fclose(f); return 0; }

    LexEntry* lex = (LexEntry*)xmalloc(sizeof(LexEntry) * (size_t)n);
    for (u32 i = 0; i < n; ++i) {
        u32 l = 0;
        if (std::fread(&l, 4, 1, f) != 1) { std::fclose(f); return 0; }
        char* s = (char*)xmalloc((size_t)l + 1);
        if (l) {
            if (std::fread(s, 1, l, f) != l) { std::fclose(f); return 0; }
        }
        s[l] = 0;
        u64 off = 0;
        u32 df = 0;
        if (std::fread(&off, 8, 1, f) != 1) { std::fclose(f); return 0; }
        if (std::fread(&df, 4, 1, f) != 1) { std::fclose(f); return 0; }
        lex[i].term = s;
        lex[i].offset = off;
        lex[i].df = df;
    }

    std::fclose(f);

    std::qsort(lex, n, sizeof(LexEntry), lex_cmp_term);

    *out_lex = lex;
    *out_n = n;
    return 1;
}

#if defined(_WIN32)
static int utf8_is_valid(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned char c = *p++;
        if (c < 0x80) continue;
        if ((c & 0xE0) == 0xC0) {
            unsigned char c1 = *p++;
            if (!c1) return 0;
            if ((c1 & 0xC0) != 0x80) return 0;
            u32 v = ((u32)(c & 31) << 6) | (u32)(c1 & 63);
            if (v < 0x80) return 0;
            continue;
        }
        if ((c & 0xF0) == 0xE0) {
            unsigned char c1 = *p++;
            unsigned char c2 = *p++;
            if (!c1 || !c2) return 0;
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return 0;
            u32 v = ((u32)(c & 15) << 12) | ((u32)(c1 & 63) << 6) | (u32)(c2 & 63);
            if (v < 0x800) return 0;
            if (v >= 0xD800 && v <= 0xDFFF) return 0;
            continue;
        }
        if ((c & 0xF8) == 0xF0) {
            unsigned char c1 = *p++;
            unsigned char c2 = *p++;
            unsigned char c3 = *p++;
            if (!c1 || !c2 || !c3) return 0;
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return 0;
            u32 v = ((u32)(c & 7) << 18) | ((u32)(c1 & 63) << 12) | ((u32)(c2 & 63) << 6) | (u32)(c3 & 63);
            if (v < 0x10000 || v > 0x10FFFF) return 0;
            continue;
        }
        return 0;
    }
    return 1;
}

static void win_set_console_utf8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}
#endif

static int main_utf8(int argc, const char* const* argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <index_dir> <query>\n", argv[0] ? argv[0] : "searcher");
        return 1;
    }

    const char* dir = argv[1];
    const char* query = argv[2];

    char vpath[1024], ppath[1024], dpath[1024];
#if defined(_WIN32)
    std::snprintf(vpath, sizeof(vpath), "%s\\vocab.bin", dir);
    std::snprintf(ppath, sizeof(ppath), "%s\\postings.bin", dir);
    std::snprintf(dpath, sizeof(dpath), "%s\\direct.bin", dir);
#else
    std::snprintf(vpath, sizeof(vpath), "%s/vocab.bin", dir);
    std::snprintf(ppath, sizeof(ppath), "%s/postings.bin", dir);
    std::snprintf(dpath, sizeof(dpath), "%s/direct.bin", dir);
#endif

    u32 docs_count = 0;
    if (!load_docs_count(dpath, &docs_count)) {
        std::fprintf(stderr, "cannot read %s\n", dpath);
        return 2;
    }

    LexEntry* lex = nullptr;
    u32 lex_n = 0;
    if (!load_lexicon(vpath, &lex, &lex_n)) {
        std::fprintf(stderr, "cannot read %s\n", vpath);
        return 2;
    }

    FILE* pf = std::fopen(ppath, "rb");
    if (!pf) {
        std::fprintf(stderr, "cannot read %s\n", ppath);
        return 2;
    }

    PostCache cache;
    cache_init(&cache);

    DocSet res;
    res.a = nullptr;
    res.n = 0;

    TermList qterms;
    termlist_init(&qterms);

    if (!query_has_ops(query)) {
        TokenizerConfig cfg;
        cfg.max_token_bytes = 256;
        cfg.fold_yo_to_e = 1;
        cfg.keep_joiners = 1;

        TokenList tl;
        tokenlist_init(&tl);
        int ok = tokenize_utf8_with_positions(query, std::strlen(query), &tl, &cfg);
        if (ok) {
            DocSet acc;
            acc.a = nullptr;
            acc.n = 0;

            for (u32 i = 0; i < tl.n; ++i) {
                if (!tl.a[i].s || !tl.a[i].s[0]) continue;
                termlist_add(&qterms, tl.a[i].s);

                Posting* p = get_posting(&cache, pf, lex, lex_n, tl.a[i].s);
                DocSet ds = docset_copy_docs(p->doc, p->n);

                if (acc.n == 0) {
                    docset_free(&acc);
                    acc = ds;
                } else {
                    DocSet u = docset_union(&acc, &ds);
                    docset_free(&acc);
                    docset_free(&ds);
                    acc = u;
                }
            }

            res = acc;
        }
        tokenlist_free(&tl);
    } else {
        Lexer L;
        lexer_init(&L, query);
        lexer_next(&L);

        Node* root = parse_expr(&L);

        if (L.cur.s) { std::free(L.cur.s); L.cur.s = nullptr; }

        res = eval_node(root, &cache, pf, lex, lex_n, docs_count);
        collect_positive_terms(root, 0, &qterms);

        node_free(root);
    }

    score_and_print(&res, &qterms, &cache, pf, lex, lex_n, docs_count);

    docset_free(&res);
    termlist_free(&qterms);

    std::fclose(pf);

    for (u32 i = 0; i < lex_n; ++i) std::free(lex[i].term);
    std::free(lex);

    for (u32 i = 0; i < cache.n; ++i) {
        Posting* p = cache.a[i];
        std::free(p->term);
        std::free(p->doc);
        std::free(p->tf);
        std::free(p);
    }
    std::free(cache.a);

    return 0;
}

#if defined(_WIN32)
int main(int argc, char** argv) {
    win_set_console_utf8();

    int ok_utf8 = 1;
    for (int i = 0; i < argc; ++i) {
        if (!utf8_is_valid(argv[i])) { ok_utf8 = 0; break; }
    }
    if (ok_utf8) return main_utf8(argc, (const char* const*)argv);

    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) return main_utf8(argc, (const char* const*)argv);

    char** argv8 = (char**)xmalloc(sizeof(char*) * (size_t)wargc);
    for (int i = 0; i < wargc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) argv8[i] = xstrdup("");
        else {
            argv8[i] = (char*)xmalloc((size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv8[i], n, nullptr, nullptr);
        }
    }

    int rc = main_utf8(wargc, (const char* const*)argv8);

    for (int i = 0; i < wargc; ++i) std::free(argv8[i]);
    std::free(argv8);
    LocalFree(wargv);
    return rc;
}
#else
int main(int argc, char** argv) {
    return main_utf8(argc, (const char* const*)argv);
}
#endif
