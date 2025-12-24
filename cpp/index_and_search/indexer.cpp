#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "..\tokenizer\tokenizer.h"

#if defined(_WIN32)
  #include <windows.h>
  #include <shellapi.h>
  #include <direct.h>
  #define MKDIR(path) _mkdir(path)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #define MKDIR(path) mkdir(path, 0755)
#endif

using u32 = uint32_t;
using u64 = uint64_t;

struct PosArr { u32* a; u32 n; u32 cap; };
struct PostingPos { u32 doc; PosArr pos; };
struct TermEntry { char* term; PostingPos* p; u32 len; u32 cap; u32 df; };
struct DocEntry { char* title; char* url; };

static u32 HT_SIZE = 300007;

struct HTNode { TermEntry* t; HTNode* next; };

static HTNode** htable;
static TermEntry** terms_arr;
static u32 terms_count;
static DocEntry* docs;
static u32 docs_count;

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

static char* xstrndup(const char* s, size_t n) {
    char* p = (char*)xmalloc(n + 1);
    std::memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static u32 hash_bytes(const char* s, u32 mod) {
    u32 h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s; h *= 16777619u; s++; }
    return h % mod;
}

static TermEntry* ht_find_or_create(const char* term) {
    u32 idx = hash_bytes(term, HT_SIZE);
    HTNode* n = htable[idx];
    while (n) {
        if (std::strcmp(n->t->term, term) == 0) return n->t;
        n = n->next;
    }
    TermEntry* te = (TermEntry*)xmalloc(sizeof(TermEntry));
    te->term = xstrdup(term);
    te->p = nullptr; te->len = 0; te->cap = 0; te->df = 0;

    HTNode* hn = (HTNode*)xmalloc(sizeof(HTNode));
    hn->t = te; hn->next = htable[idx]; htable[idx] = hn;
    return te;
}

static void posarr_push(PosArr* pa, u32 v) {
    if (pa->cap == 0) { pa->cap = 8; pa->a = (u32*)xmalloc(sizeof(u32) * pa->cap); pa->n = 0; }
    if (pa->n >= pa->cap) { pa->cap *= 2; pa->a = (u32*)xrealloc(pa->a, sizeof(u32) * pa->cap); }
    pa->a[pa->n++] = v;
}

static void postingpos_append(TermEntry* te, u32 doc, u32* pos_arr, u32 pos_n) {
    if (te->cap == 0) { te->cap = 4; te->p = (PostingPos*)xmalloc(te->cap * sizeof(PostingPos)); }
    if (te->len >= te->cap) {
        te->cap *= 2;
        te->p = (PostingPos*)xrealloc(te->p, te->cap * sizeof(PostingPos));
    }
    te->p[te->len].doc = doc;
    te->p[te->len].pos.a = pos_arr;
    te->p[te->len].pos.n = pos_n;
    te->p[te->len].pos.cap = pos_n;
    te->len++;
    te->df = te->len;
}

static void vbyte_write_u32(FILE* f, u32 x) {
    while (x >= 128u) {
        unsigned char b = (unsigned char)((x & 127u) | 128u);
        std::fwrite(&b, 1, 1, f);
        x >>= 7;
    }
    unsigned char b = (unsigned char)x;
    std::fwrite(&b, 1, 1, f);
}

static char* read_entire_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return nullptr;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    char* buf = (char*)xmalloc((size_t)sz + 1);
    size_t r = std::fread(buf, 1, (size_t)sz, f);
    buf[r] = 0;
    std::fclose(f);
    return buf;
}

static void normalize_newlines_inplace(char* s) {
    char* w = s;
    for (char* r = s; *r; ++r) {
        if (*r == '\r') continue;
        *w++ = *r;
    }
    *w = 0;
}

struct DocTerm { char* term; DocTerm* next; PosArr pos; };
struct DocMap { DocTerm** b; u32 nb; };

static void docmap_init(DocMap* m, u32 nb) {
    m->nb = nb;
    m->b = (DocTerm**)xcalloc(nb, sizeof(DocTerm*));
}

static DocTerm* docmap_get_or_add(DocMap* m, const char* term) {
    u32 idx = hash_bytes(term, m->nb);
    DocTerm* p = m->b[idx];
    while (p) {
        if (std::strcmp(p->term, term) == 0) return p;
        p = p->next;
    }
    DocTerm* n = (DocTerm*)xmalloc(sizeof(DocTerm));
    n->term = xstrdup(term);
    n->next = m->b[idx];
    n->pos.a = nullptr; n->pos.n = 0; n->pos.cap = 0;
    m->b[idx] = n;
    return n;
}

static void docmap_emit_and_free(DocMap* m, u32 docid) {
    for (u32 i = 0; i < m->nb; ++i) {
        DocTerm* p = m->b[i];
        while (p) {
            DocTerm* nx = p->next;
            TermEntry* te = ht_find_or_create(p->term);
            u32* arr = p->pos.a;
            u32 n = p->pos.n;
            p->pos.a = nullptr; p->pos.n = 0; p->pos.cap = 0;
            postingpos_append(te, docid, arr, n);
            std::free(p->term);
            std::free(p);
            p = nx;
        }
        m->b[i] = nullptr;
    }
}

static void docmap_destroy(DocMap* m) {
    std::free(m->b);
    m->b = nullptr;
    m->nb = 0;
}

static void process_document_line(const char* text_utf8, u32 docid) {
    DocMap dm;
    docmap_init(&dm, 4096);

    TokenizerConfig cfg;
    cfg.max_token_bytes = 256;
    cfg.fold_yo_to_e = 1;
    cfg.keep_joiners = 1;

    TokenList tl;
    tokenlist_init(&tl);

    int ok = tokenize_utf8_with_positions(text_utf8, std::strlen(text_utf8), &tl, &cfg);
    if (ok) {
        for (u32 i = 0; i < tl.n; ++i) {
            DocTerm* dt = docmap_get_or_add(&dm, tl.a[i].s);
            posarr_push(&dt->pos, tl.a[i].pos);
        }
    }

    tokenlist_free(&tl);

    docmap_emit_and_free(&dm, docid);
    docmap_destroy(&dm);
}

static int term_cmp(const void* a, const void* b) {
    TermEntry* A = *(TermEntry**)a;
    TermEntry* B = *(TermEntry**)b;
    return std::strcmp(A->term, B->term);
}

static u64 tell_file(FILE* f) {
#if defined(_WIN32)
    return (u64)_ftelli64(f);
#else
    return (u64)ftello(f);
#endif
}

static char* derive_title_from_line(const char* line, u32 docid) {
    while (*line == ' ' || *line == '\t') line++;

    if (*line == '=') {
        while (*line == '=') line++;
        while (*line == ' ') line++;
        const char* end = std::strchr(line, '=');
        if (end && end > line) {
            size_t n = (size_t)(end - line);
            if (n > 120) n = 120;
            return xstrndup(line, n);
        }
    }

    const char* cut = std::strstr(line, "==");
    size_t n = cut ? (size_t)(cut - line) : std::strlen(line);
    if (n > 120) n = 120;

    if (n < 3) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "doc_%u", docid);
        return xstrdup(buf);
    }

    return xstrndup(line, n);
}

static void write_index(const char* outdir) {
    char vpath[1024], ppath[1024], dpath[1024];
#if defined(_WIN32)
    std::snprintf(vpath, sizeof(vpath), "%s\\vocab.bin", outdir);
    std::snprintf(ppath, sizeof(ppath), "%s\\postings.bin", outdir);
    std::snprintf(dpath, sizeof(dpath), "%s\\direct.bin", outdir);
#else
    std::snprintf(vpath, sizeof(vpath), "%s/vocab.bin", outdir);
    std::snprintf(ppath, sizeof(ppath), "%s/postings.bin", outdir);
    std::snprintf(dpath, sizeof(dpath), "%s/direct.bin", outdir);
#endif

    terms_arr = (TermEntry**)xmalloc(sizeof(TermEntry*) * HT_SIZE);
    u32 idx = 0;
    for (u32 i = 0; i < HT_SIZE; i++) {
        HTNode* n = htable[i];
        while (n) { terms_arr[idx++] = n->t; n = n->next; }
    }
    terms_count = idx;
    std::qsort(terms_arr, terms_count, sizeof(TermEntry*), term_cmp);

    FILE* pf = std::fopen(ppath, "wb");
    if (!pf) { std::fprintf(stderr, "cannot write %s\n", ppath); std::exit(2); }

    u64* offsets = (u64*)xmalloc(sizeof(u64) * terms_count);

    for (u32 i = 0; i < terms_count; i++) {
        offsets[i] = tell_file(pf);

        u32 df = terms_arr[i]->len;
        vbyte_write_u32(pf, df);

        u32 prev_doc = 0;
        for (u32 j = 0; j < df; j++) {
            u32 doc = terms_arr[i]->p[j].doc;
            u32 doc_gap = doc - prev_doc;
            prev_doc = doc;
            vbyte_write_u32(pf, doc_gap);

            u32 tf = terms_arr[i]->p[j].pos.n;
            vbyte_write_u32(pf, tf);

            u32 prev_pos = 0;
            for (u32 k = 0; k < tf; k++) {
                u32 pos = terms_arr[i]->p[j].pos.a[k];
                u32 pos_gap = pos - prev_pos;
                prev_pos = pos;
                vbyte_write_u32(pf, pos_gap);
            }
        }
    }
    std::fclose(pf);

    FILE* vf = std::fopen(vpath, "wb");
    if (!vf) { std::fprintf(stderr, "cannot write %s\n", vpath); std::exit(2); }

    std::fwrite(&terms_count, 4, 1, vf);
    for (u32 i = 0; i < terms_count; i++) {
        u32 l = (u32)std::strlen(terms_arr[i]->term);
        std::fwrite(&l, 4, 1, vf);
        std::fwrite(terms_arr[i]->term, 1, l, vf);
        std::fwrite(&offsets[i], 8, 1, vf);
        std::fwrite(&terms_arr[i]->df, 4, 1, vf);
    }
    std::fclose(vf);

    FILE* dfp = std::fopen(dpath, "wb");
    if (!dfp) { std::fprintf(stderr, "cannot write %s\n", dpath); std::exit(2); }

    std::fwrite(&docs_count, 4, 1, dfp);
    for (u32 i = 0; i < docs_count; i++) {
        u32 tl = (u32)std::strlen(docs[i].title);
        std::fwrite(&tl, 4, 1, dfp); std::fwrite(docs[i].title, 1, tl, dfp);
        u32 ul = (u32)std::strlen(docs[i].url);
        std::fwrite(&ul, 4, 1, dfp); std::fwrite(docs[i].url, 1, ul, dfp);
    }
    std::fclose(dfp);

    std::free(offsets);
}

static void parse_corpus_lines(const char* path) {
    char* text = read_entire_file(path);
    if (!text) { std::fprintf(stderr, "cannot read %s\n", path); std::exit(2); }

    normalize_newlines_inplace(text);

    docs_count = 0;
    u32 cap = 1024;
    docs = (DocEntry*)xmalloc(cap * sizeof(DocEntry));

    char* p = text;
    while (*p) {
        char* line = p;
        char* eol = std::strchr(p, '\n');
        if (eol) { *eol = 0; p = eol + 1; }
        else { p = p + std::strlen(p); }

        char* q = line;
        while (*q == ' ' || *q == '\t') q++;
        if (*q == 0) continue;

        u32 docid = docs_count + 1;

        if (docs_count >= cap) {
            cap *= 2;
            docs = (DocEntry*)xrealloc(docs, cap * sizeof(DocEntry));
        }

        char* title = derive_title_from_line(line, docid);
        docs[docs_count].title = title;
        docs[docs_count].url = xstrdup("");

        process_document_line(line, docid);

        docs_count++;
    }

    std::free(text);
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

static int main_utf8(int argc, const char* const* argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <corpus.txt> <outdir>\n", argv[0] ? argv[0] : "indexer");
        return 1;
    }
    const char* corpus = argv[1];
    const char* outdir = argv[2];

    htable = (HTNode**)xmalloc(sizeof(HTNode*) * HT_SIZE);
    for (u32 i = 0; i < HT_SIZE; i++) htable[i] = nullptr;

    parse_corpus_lines(corpus);

    (void)MKDIR(outdir);

    write_index(outdir);

    std::fprintf(stdout, "indexed docs: %u\n", docs_count);
    return 0;
}

int main(int argc, char** argv) {
    win_set_console_utf8();

    int ok_utf8 = 1;
    for (int i = 0; i < argc; ++i) {
        if (!utf8_is_valid(argv[i])) { ok_utf8 = 0; break; }
    }
    if (ok_utf8) {
        return main_utf8(argc, (const char* const*)argv);
    }

    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        return main_utf8(argc, (const char* const*)argv);
    }

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
static int main_utf8(int argc, const char* const* argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <corpus.txt> <outdir>\n", argv[0] ? argv[0] : "indexer");
        return 1;
    }
    const char* corpus = argv[1];
    const char* outdir = argv[2];

    htable = (HTNode**)xmalloc(sizeof(HTNode*) * HT_SIZE);
    for (u32 i = 0; i < HT_SIZE; i++) htable[i] = nullptr;

    parse_corpus_lines(corpus);

    (void)MKDIR(outdir);

    write_index(outdir);

    std::fprintf(stdout, "indexed docs: %u\n", docs_count);
    return 0;
}

int main(int argc, char** argv) {
    return main_utf8(argc, (const char* const*)argv);
}
#endif
