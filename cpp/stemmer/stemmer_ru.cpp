#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

using u32 = uint32_t;

static bool utf8_decode_at(const char* s, size_t n, size_t i, u32* cp, size_t* len) {
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

static inline bool is_cyr(u32 cp) { return (cp >= 0x0400 && cp <= 0x052F); }
static inline u32 lower_ru(u32 cp) {
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}
static inline u32 fold_yo(u32 cp) { return (cp == 0x0451) ? 0x0435 : cp; }

static std::u32string to_u32_ru_fold(const std::string& s) {
    std::u32string r;
    size_t i = 0, n = s.size();
    while (i < n) {
        u32 cp; size_t l;
        if (!utf8_decode_at(s.data(), n, i, &cp, &l)) { i += 1; continue; }
        cp = lower_ru(cp);
        cp = fold_yo(cp);
        r.push_back((char32_t)cp);
        i += l;
    }
    return r;
}

static std::string from_u32(const std::u32string& s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (char32_t c : s) {
        char enc[4];
        int el = utf8_encode((u32)c, enc);
        out.append(enc, enc + el);
    }
    return out;
}

static inline bool is_vowel(char32_t c) {
    switch ((u32)c) {
        case 0x0430: case 0x0435: case 0x0438: case 0x043E: case 0x0443:
        case 0x044B: case 0x044D: case 0x044E: case 0x044F:
            return true;
        default:
            return false;
    }
}

static bool ends_with(const std::u32string& s, const char32_t* suf, size_t m) {
    if (s.size() < m) return false;
    size_t n = s.size();
    for (size_t i = 0; i < m; ++i) if (s[n - m + i] != suf[i]) return false;
    return true;
}

static bool remove_suffix(std::u32string& s, const char32_t* suf, size_t m) {
    if (!ends_with(s, suf, m)) return false;
    s.resize(s.size() - m);
    return true;
}

static size_t rv_pos(const std::u32string& w) {
    for (size_t i = 0; i < w.size(); ++i) {
        if (is_vowel(w[i])) return i + 1;
    }
    return w.size();
}

static bool remove_any(std::u32string& w, size_t start, const char32_t* const* sufs, const size_t* lens, size_t k) {
    if (start > w.size()) return false;
    for (size_t i = 0; i < k; ++i) {
        size_t m = lens[i];
        if (w.size() < start + m) continue;
        bool ok = true;
        for (size_t j = 0; j < m; ++j) if (w[w.size() - m + j] != sufs[i][j]) { ok = false; break; }
        if (ok) { w.resize(w.size() - m); return true; }
    }
    return false;
}

static std::u32string stem_ru_porter_like(std::u32string w) {
    if (w.size() < 2) return w;
    for (auto& c : w) {
        u32 cp = (u32)c;
        if (cp == 0x0451) c = (char32_t)0x0435;
    }

    size_t rv = rv_pos(w);
    if (rv >= w.size()) return w;

    auto in_rv = [&](size_t idx){ return idx >= rv; };

    auto try_remove_in_rv = [&](const char32_t* suf, size_t m) -> bool {
        if (w.size() < m) return false;
        if (!in_rv(w.size() - m)) return false;
        return remove_suffix(w, suf, m);
    };

    static const char32_t suf_perfect1[] = {U'в',U'ш',U'и',U'с',U'ь'};
    static const char32_t suf_perfect2[] = {U'в',U'ш',U'и'};
    static const char32_t suf_perfect3[] = {U'в'};
    static const char32_t suf_perfect4[] = {U'в',U'ш',U'и',U'в'};
    static const char32_t suf_perfect5[] = {U'в',U'ш',U'и',U'в',U'ш',U'и'};
    static const char32_t suf_reflex1[] = {U'с',U'я'};
    static const char32_t suf_reflex2[] = {U'с',U'ь'};
    static const char32_t suf_part1[] = {U'и',U'в',U'ш',U'и'};
    static const char32_t suf_part2[] = {U'ы',U'в',U'ш',U'и'};
    static const char32_t suf_part3[] = {U'и',U'в',U'ш'};
    static const char32_t suf_part4[] = {U'ы',U'в',U'ш'};
    static const char32_t suf_part5[] = {U'у',U'ю',U'щ'};
    static const char32_t suf_part6[] = {U'ю',U'щ'};
    static const char32_t suf_ger1[] = {U'в',U'ш',U'и'};
    static const char32_t suf_ger2[] = {U'в',U'ш',U'и',U'с',U'ь'};
    static const char32_t suf_ger3[] = {U'в'};
    static const char32_t suf_ger4[] = {U'в',U'ш',U'и',U'в'};
    static const char32_t suf_adj1[] = {U'е',U'е'};
    static const char32_t suf_adj2[] = {U'и',U'е'};
    static const char32_t suf_adj3[] = {U'ы',U'е'};
    static const char32_t suf_adj4[] = {U'о',U'е'};
    static const char32_t suf_adj5[] = {U'е',U'й'};
    static const char32_t suf_adj6[] = {U'и',U'й'};
    static const char32_t suf_adj7[] = {U'ы',U'й'};
    static const char32_t suf_adj8[] = {U'о',U'й'};
    static const char32_t suf_adj9[] = {U'ы',U'м'};
    static const char32_t suf_adj10[] = {U'и',U'м'};
    static const char32_t suf_adj11[] = {U'о',U'м'};
    static const char32_t suf_adj12[] = {U'е',U'м'};
    static const char32_t suf_adj13[] = {U'и',U'х'};
    static const char32_t suf_adj14[] = {U'ы',U'х'};
    static const char32_t suf_adj15[] = {U'у',U'ю'};
    static const char32_t suf_adj16[] = {U'ю',U'ю'};
    static const char32_t suf_adj17[] = {U'а',U'я'};
    static const char32_t suf_adj18[] = {U'я',U'я'};
    static const char32_t suf_adj19[] = {U'о'};
    static const char32_t suf_adj20[] = {U'е'};
    static const char32_t suf_adj21[] = {U'а'};
    static const char32_t suf_adj22[] = {U'я'};
    static const char32_t suf_adj23[] = {U'ы'};
    static const char32_t suf_adj24[] = {U'и'};
    static const char32_t suf_noun1[] = {U'а'};
    static const char32_t suf_noun2[] = {U'я'};
    static const char32_t suf_noun3[] = {U'о'};
    static const char32_t suf_noun4[] = {U'е'};
    static const char32_t suf_noun5[] = {U'ы'};
    static const char32_t suf_noun6[] = {U'и'};
    static const char32_t suf_noun7[] = {U'у'};
    static const char32_t suf_noun8[] = {U'ю'};
    static const char32_t suf_noun9[] = {U'а',U'х'};
    static const char32_t suf_noun10[] = {U'я',U'х'};
    static const char32_t suf_noun11[] = {U'а',U'м'};
    static const char32_t suf_noun12[] = {U'я',U'м'};
    static const char32_t suf_noun13[] = {U'о',U'в'};
    static const char32_t suf_noun14[] = {U'е',U'в'};
    static const char32_t suf_noun15[] = {U'и',U'е'};
    static const char32_t suf_noun16[] = {U'ь'};
    static const char32_t suf_noun17[] = {U'и',U'й'};
    static const char32_t suf_noun18[] = {U'й'};
    static const char32_t suf_noun19[] = {U'и',U'я'};
    static const char32_t suf_noun20[] = {U'ь',U'ю'};
    static const char32_t suf_noun21[] = {U'и',U'и'};
    static const char32_t suf_noun22[] = {U'и',U'е',U'й'};
    static const char32_t suf_noun23[] = {U'и',U'я',U'м',U'и'};
    static const char32_t suf_noun24[] = {U'а',U'м',U'и'};
    static const char32_t suf_noun25[] = {U'я',U'м',U'и'};
    static const char32_t suf_noun26[] = {U'о',U'м'};
    static const char32_t suf_noun27[] = {U'е',U'м'};

    bool removed = false;

    removed = try_remove_in_rv(suf_perfect5, 6) || try_remove_in_rv(suf_perfect1, 5) || try_remove_in_rv(suf_perfect4, 4) ||
              try_remove_in_rv(suf_perfect2, 3) || try_remove_in_rv(suf_perfect3, 1);

    try_remove_in_rv(suf_reflex1, 2) || try_remove_in_rv(suf_reflex2, 2);

    if (!removed) {
        static const char32_t* const adj_sufs[] = {
            suf_adj1,suf_adj2,suf_adj3,suf_adj4,suf_adj5,suf_adj6,suf_adj7,suf_adj8,
            suf_adj9,suf_adj10,suf_adj11,suf_adj12,suf_adj13,suf_adj14,suf_adj15,suf_adj16,
            suf_adj17,suf_adj18,suf_adj19,suf_adj20,suf_adj21,suf_adj22,suf_adj23,suf_adj24
        };
        static const size_t adj_len[] = {
            2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1
        };
        if (remove_any(w, rv, adj_sufs, adj_len, sizeof(adj_sufs)/sizeof(adj_sufs[0]))) {
            static const char32_t* const part_sufs[] = {suf_part1,suf_part2,suf_part3,suf_part4,suf_part5,suf_part6};
            static const size_t part_len[] = {4,4,3,3,3,2};
            remove_any(w, rv, part_sufs, part_len, sizeof(part_sufs)/sizeof(part_sufs[0]));
        } else {
            static const char32_t* const noun_sufs[] = {
                suf_noun23,suf_noun22,suf_noun25,suf_noun24,suf_noun21,suf_noun20,suf_noun19,suf_noun18,suf_noun17,suf_noun16,
                suf_noun15,suf_noun14,suf_noun13,suf_noun12,suf_noun11,suf_noun10,suf_noun9,
                suf_noun8,suf_noun7,suf_noun6,suf_noun5,suf_noun4,suf_noun3,suf_noun2,suf_noun1,suf_noun26,suf_noun27
            };
            static const size_t noun_len[] = {
                4,3,3,3,2,2,2,1,2,1,
                2,2,2,2,2,2,2,
                1,1,1,1,1,1,1,1,2,2
            };
            remove_any(w, rv, noun_sufs, noun_len, sizeof(noun_sufs)/sizeof(noun_sufs[0]));
        }
    }

    if (rv < w.size()) {
        static const char32_t suf_i[] = {U'и'};
        if (in_rv(w.size()-1)) remove_suffix(w, suf_i, 1);
    }

    auto r2_pos = [&](const std::u32string& s)->size_t {
        size_t n = s.size();
        size_t i = 0;
        while (i < n && !is_vowel(s[i])) ++i;
        while (i < n && is_vowel(s[i])) ++i;
        while (i < n && !is_vowel(s[i])) ++i;
        while (i < n && is_vowel(s[i])) ++i;
        return i;
    };

    size_t r2 = r2_pos(w);
    if (r2 < w.size()) {
        static const char32_t suf_ost[] = {U'о',U'с',U'т',U'ь'};
        if (ends_with(w, suf_ost, 4) && (w.size() - 4) >= r2) w.resize(w.size() - 4);
    }

    if (rv < w.size()) {
        static const char32_t suf_nn[] = {U'н',U'н'};
        static const char32_t suf_n[]  = {U'н'};
        if (ends_with(w, suf_nn, 2) && (w.size() - 2) >= rv) w.resize(w.size() - 1);
        static const char32_t suf_soft[] = {U'ь'};
        if (ends_with(w, suf_soft, 1) && (w.size() - 1) >= rv) w.resize(w.size() - 1);
        static const char32_t suf_eysh[] = {U'е',U'й',U'ш'};
        static const char32_t suf_eyshe[] = {U'е',U'й',U'ш',U'е'};
        if (ends_with(w, suf_eyshe, 4) && (w.size() - 4) >= rv) w.resize(w.size() - 4);
        else if (ends_with(w, suf_eysh, 3) && (w.size() - 3) >= rv) w.resize(w.size() - 3);
        static const char32_t suf_nost[] = {U'н',U'о',U'с',U'т'};
        if (ends_with(w, suf_nost, 4) && (w.size() - 4) >= rv) w.resize(w.size() - 4);
        if (ends_with(w, suf_n, 1) && (w.size() - 1) >= rv) {
            if (w.size() >= 2 && w[w.size()-2] == U'н') {}
        }
    }

    return w;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { std::cout << "\n"; continue; }

        std::u32string w = to_u32_ru_fold(line);
        std::u32string st = stem_ru_porter_like(std::move(w));
        std::string out = from_u32(st);
        std::cout << out << "\n";
    }
    return 0;
}
