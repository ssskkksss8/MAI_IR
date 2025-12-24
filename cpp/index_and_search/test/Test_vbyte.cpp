#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using u32 = uint32_t;

static void fail(const char* msg) {
    std::fprintf(stderr, "TEST FAILED: %s\n", msg);
    std::exit(1);
}

static void require_true(bool ok, const char* msg) {
    if (!ok) fail(msg);
}

static void require_u32_eq(u32 got, u32 exp, const char* msg) {
    if (got != exp) {
        std::fprintf(stderr, "TEST FAILED: %s\nExpected: %u\nGot:      %u\n",
                     msg, (unsigned)exp, (unsigned)got);
        std::exit(1);
    }
}

static void require_vec_eq(const std::vector<u32>& got, const std::vector<u32>& exp, const char* msg) {
    if (got != exp) {
        std::fprintf(stderr, "TEST FAILED: %s\nExpected:", msg);
        for (u32 x : exp) std::fprintf(stderr, " %u", (unsigned)x);
        std::fprintf(stderr, "\nGot:     ");
        for (u32 x : got) std::fprintf(stderr, " %u", (unsigned)x);
        std::fprintf(stderr, "\n");
        std::exit(1);
    }
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

static u32 vbyte_len_u32(u32 x) {
    u32 n = 1;
    while (x >= 128u) { n++; x >>= 7; }
    return n;
}

static long file_size_bytes(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fclose(f);
    return sz;
}

static void test_vbyte_boundaries_roundtrip() {
    const std::vector<u32> vals = {
        0u, 1u, 2u, 10u, 127u,
        128u, 129u, 255u, 16383u,
        16384u, 16385u, 2097151u,
        2097152u, 268435455u,
        268435456u, 4294967295u
    };

    const std::vector<u32> exp_len = {
        1u, 1u, 1u, 1u, 1u,
        2u, 2u, 2u, 2u,
        3u, 3u, 3u,
        4u, 4u,
        5u, 5u
    };

    require_true(vals.size() == exp_len.size(), "internal test vectors mismatch");

    const char* path = "test_tmp_vbyte.bin";
    FILE* w = std::fopen(path, "wb");
    require_true(w != nullptr, "cannot open temp file for write");

    u32 total_expected = 0;
    for (size_t i = 0; i < vals.size(); ++i) {
        require_u32_eq(vbyte_len_u32(vals[i]), exp_len[i], "vbyte_len_u32 mismatch");
        total_expected += exp_len[i];
        vbyte_write_u32(w, vals[i]);
    }
    std::fclose(w);

    long sz = file_size_bytes(path);
    require_true(sz >= 0, "cannot stat temp file");
    require_u32_eq((u32)sz, total_expected, "encoded file size must match sum of vbyte lengths");

    FILE* r = std::fopen(path, "rb");
    require_true(r != nullptr, "cannot open temp file for read");

    for (size_t i = 0; i < vals.size(); ++i) {
        u32 x = 0;
        int ok = vbyte_read_u32(r, &x);
        require_true(ok == 1, "vbyte_read_u32 failed");
        require_u32_eq(x, vals[i], "roundtrip value mismatch");
    }

    u32 extra = 0;
    require_true(vbyte_read_u32(r, &extra) == 0, "expected EOF after reading all values");
    std::fclose(r);

    std::remove(path);
}

struct PostingCase {
    u32 df;
    std::vector<u32> doc;
    std::vector<u32> tf;
    std::vector<std::vector<u32>> pos;
};

static void write_posting_like_indexer(FILE* f, const PostingCase& pc) {
    vbyte_write_u32(f, pc.df);

    u32 prev_doc = 0;
    for (u32 i = 0; i < pc.df; ++i) {
        u32 doc_gap = pc.doc[i] - prev_doc;
        prev_doc = pc.doc[i];
        vbyte_write_u32(f, doc_gap);

        u32 tf = pc.tf[i];
        vbyte_write_u32(f, tf);

        u32 prev_pos = 0;
        for (u32 k = 0; k < tf; ++k) {
            u32 pos_gap = pc.pos[i][k] - prev_pos;
            prev_pos = pc.pos[i][k];
            vbyte_write_u32(f, pos_gap);
        }
    }
}

static PostingCase read_posting_like_searcher(FILE* f) {
    PostingCase pc;
    u32 df = 0;
    require_true(vbyte_read_u32(f, &df) == 1, "read df failed");
    pc.df = df;
    pc.doc.resize(df);
    pc.tf.resize(df);
    pc.pos.resize(df);

    u32 prev_doc = 0;
    for (u32 i = 0; i < df; ++i) {
        u32 doc_gap = 0, tf = 0;
        require_true(vbyte_read_u32(f, &doc_gap) == 1, "read doc_gap failed");
        require_true(vbyte_read_u32(f, &tf) == 1, "read tf failed");

        u32 doc = prev_doc + doc_gap;
        prev_doc = doc;
        pc.doc[i] = doc;
        pc.tf[i] = tf;

        pc.pos[i].resize(tf);
        u32 prev_pos = 0;
        for (u32 k = 0; k < tf; ++k) {
            u32 pos_gap = 0;
            require_true(vbyte_read_u32(f, &pos_gap) == 1, "read pos_gap failed");
            prev_pos += pos_gap;
            pc.pos[i][k] = prev_pos;
        }
    }
    return pc;
}

static void test_posting_layout_roundtrip() {
    PostingCase in;
    in.df = 3;
    in.doc = {1, 10, 1000};
    in.tf  = {3, 1, 4};
    in.pos = {
        {0, 2, 10},
        {7},
        {1, 2, 30, 31}
    };

    const char* path = "test_tmp_posting.bin";
    FILE* w = std::fopen(path, "wb");
    require_true(w != nullptr, "cannot open posting temp file for write");
    write_posting_like_indexer(w, in);
    std::fclose(w);

    FILE* r = std::fopen(path, "rb");
    require_true(r != nullptr, "cannot open posting temp file for read");
    PostingCase out = read_posting_like_searcher(r);
    std::fclose(r);

    require_u32_eq(out.df, in.df, "df mismatch");
    require_vec_eq(out.doc, in.doc, "doc list mismatch");
    require_vec_eq(out.tf, in.tf, "tf list mismatch");
    require_true(out.pos.size() == in.pos.size(), "pos outer size mismatch");
    for (size_t i = 0; i < in.pos.size(); ++i) {
        require_vec_eq(out.pos[i], in.pos[i], "positions mismatch");
    }

    std::remove(path);
}

int main() {
    test_vbyte_boundaries_roundtrip();
    test_posting_layout_roundtrip();
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
