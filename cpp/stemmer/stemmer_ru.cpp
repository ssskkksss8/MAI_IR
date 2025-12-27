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
    for (size_t i = 0; i < m; ++i)
        if (s[n - m + i] != suf[i]) return false;
    return true;
}

static bool remove_suffix(std::u32string& s, const char32_t* suf, size_t m) {
    if (!ends_with(s, suf, m)) return false;
    s.resize(s.size() - m);
    return true;
}

static size_t rv_pos(const std::u32string& w) {
    for (size_t i = 0; i < w.size(); ++i)
        if (is_vowel(w[i])) return i + 1;
    return w.size();
}

static size_t r1_pos(const std::u32string& w) {
    const size_t n = w.size();
    size_t i = 0;
    while (i < n && !is_vowel(w[i])) ++i;
    while (i < n &&  is_vowel(w[i])) ++i;
    while (i < n && !is_vowel(w[i])) ++i;
    return i;
}

static size_t r2_pos(const std::u32string& w, size_t r1) {
    if (r1 >= w.size()) return w.size();
    const size_t n = w.size();
    size_t i = r1;
    while (i < n && !is_vowel(w[i])) ++i;
    while (i < n &&  is_vowel(w[i])) ++i;
    while (i < n && !is_vowel(w[i])) ++i;
    return i;
}

struct Suf { const char32_t* s; size_t n; };

static bool remove_suffix_in_region(std::u32string& w, size_t region, const char32_t* suf, size_t m) {
    if (w.size() < m) return false;
    const size_t start = w.size() - m;
    if (start < region) return false;
    return remove_suffix(w, suf, m);
}

static bool remove_one_of_in_region(std::u32string& w, size_t region, const Suf* list, size_t k) {
    for (size_t i = 0; i < k; ++i)
        if (remove_suffix_in_region(w, region, list[i].s, list[i].n)) return true;
    return false;
}

static inline bool is_ay(char32_t c) { return c == U'а' || c == U'я'; }

static bool remove_conditional_in_region(std::u32string& w, size_t region,
                                        const Suf* list, size_t k,
                                        bool (*cond)(char32_t)) {
    for (size_t i = 0; i < k; ++i) {
        const size_t m = list[i].n;
        if (w.size() < m + 1) continue;
        const size_t start = w.size() - m;
        if (start < region) continue;
        if (!ends_with(w, list[i].s, m)) continue;
        char32_t prev = w[w.size() - m - 1];
        if (!cond(prev)) continue;
        w.resize(w.size() - m);
        return true;
    }
    return false;
}

static std::u32string stem_ru_snowball(std::u32string w) {
    const size_t rv = rv_pos(w);
    if (rv >= w.size()) return w;

    const size_t r1 = r1_pos(w);
    const size_t r2 = r2_pos(w, r1);

    static const char32_t s_ivs[]  = {U'и',U'в',U'ш',U'и',U'с',U'ь'};
    static const char32_t s_ivsh[] = {U'и',U'в',U'ш',U'и'};
    static const char32_t s_yvs[]  = {U'ы',U'в',U'ш',U'и',U'с',U'ь'};
    static const char32_t s_yvsh[] = {U'ы',U'в',U'ш',U'и'};
    static const char32_t s_iv[]   = {U'и',U'в'};
    static const char32_t s_yv[]   = {U'ы',U'в'};
    static const Suf perfective_a[] = {
        {s_ivs,6},{s_yvs,6},{s_ivsh,4},{s_yvsh,4},{s_iv,2},{s_yv,2}
    };
    bool removed = remove_one_of_in_region(w, rv, perfective_a, sizeof(perfective_a)/sizeof(perfective_a[0]));

    if (!removed) {
        static const char32_t s_vshis[] = {U'в',U'ш',U'и',U'с',U'ь'};
        static const char32_t s_vshi[]  = {U'в',U'ш',U'и'};
        static const char32_t s_v[]     = {U'в'};
        static const Suf perfective_b[] = { {s_vshis,5},{s_vshi,3},{s_v,1} };
        removed = remove_conditional_in_region(w, rv, perfective_b, 3, is_ay);
    }

    if (!removed) {
        static const char32_t s_sya[] = {U'с',U'я'};
        static const char32_t s_ss[]  = {U'с',U'ь'};
        remove_suffix_in_region(w, rv, s_sya, 2);
        remove_suffix_in_region(w, rv, s_ss,  2);

        static const char32_t a_ego[] = {U'е',U'г',U'о'};
        static const char32_t a_ogo[] = {U'о',U'г',U'о'};
        static const char32_t a_emu[] = {U'е',U'м',U'у'};
        static const char32_t a_omu[] = {U'о',U'м',U'у'};
        static const char32_t a_imi[] = {U'и',U'м',U'и'};
        static const char32_t a_ymi[] = {U'ы',U'м',U'и'};
        static const char32_t a_ee[]  = {U'е',U'е'};
        static const char32_t a_ie[]  = {U'и',U'е'};
        static const char32_t a_ye[]  = {U'ы',U'е'};
        static const char32_t a_oe[]  = {U'о',U'е'};
        static const char32_t a_ei[]  = {U'е',U'й'};
        static const char32_t a_ii[]  = {U'и',U'й'};
        static const char32_t a_yi[]  = {U'ы',U'й'};
        static const char32_t a_oi[]  = {U'о',U'й'};
        static const char32_t a_em[]  = {U'е',U'м'};
        static const char32_t a_im[]  = {U'и',U'м'};
        static const char32_t a_ym[]  = {U'ы',U'м'};
        static const char32_t a_om[]  = {U'о',U'м'};
        static const char32_t a_ih[]  = {U'и',U'х'};
        static const char32_t a_yh[]  = {U'ы',U'х'};
        static const char32_t a_uyu[] = {U'у',U'ю'};
        static const char32_t a_yuyu[]= {U'ю',U'ю'};
        static const char32_t a_aya[] = {U'а',U'я'};
        static const char32_t a_yaya[]= {U'я',U'я'};
        static const char32_t a_oyu[] = {U'о',U'ю'};
        static const char32_t a_eyu[] = {U'е',U'ю'};

        static const Suf adj[] = {
            {a_ego,3},{a_ogo,3},{a_emu,3},{a_omu,3},
            {a_imi,3},{a_ymi,3},
            {a_ee,2},{a_ie,2},{a_ye,2},{a_oe,2},
            {a_ei,2},{a_ii,2},{a_yi,2},{a_oi,2},
            {a_em,2},{a_im,2},{a_ym,2},{a_om,2},
            {a_ih,2},{a_yh,2},
            {a_uyu,2},{a_yuyu,2},{a_aya,2},{a_yaya,2},{a_oyu,2},{a_eyu,2}
        };

        bool adj_removed = remove_one_of_in_region(w, rv, adj, sizeof(adj)/sizeof(adj[0]));

        if (adj_removed) {
            static const char32_t p_uusch[]={U'у',U'ю',U'щ'};
            static const char32_t p_ivsh[] = {U'и',U'в',U'ш'};
            static const char32_t p_yvsh[] = {U'ы',U'в',U'ш'};
            static const Suf part_a[] = { {p_uusch,3},{p_ivsh,3},{p_yvsh,3} };

            static const char32_t p_em[]  = {U'е',U'м'};
            static const char32_t p_nn[]  = {U'н',U'н'};
            static const char32_t p_vsh[] = {U'в',U'ш'};
            static const char32_t p_yusch[]={U'ю',U'щ'};
            static const char32_t p_sch[] = {U'щ'};
            static const Suf part_b[] = { {p_em,2},{p_nn,2},{p_vsh,2},{p_yusch,2},{p_sch,1} };

            if (!remove_one_of_in_region(w, rv, part_a, 3)) {
                remove_conditional_in_region(w, rv, part_b, 5, is_ay);
            }
        } else {
            static const char32_t v_nno[] = {U'н',U'н',U'о'};
            static const char32_t v_esh[] = {U'е',U'ш',U'ь'};
            static const char32_t v_ete[] = {U'е',U'т',U'е'};
            static const char32_t v_yte[] = {U'й',U'т',U'е'};
            static const char32_t v_yut[] = {U'ю',U'т'};
            static const char32_t v_la[]  = {U'л',U'а'};
            static const char32_t v_na[]  = {U'н',U'а'};
            static const char32_t v_li[]  = {U'л',U'и'};
            static const char32_t v_lo[]  = {U'л',U'о'};
            static const char32_t v_no[]  = {U'н',U'о'};
            static const char32_t v_et[]  = {U'е',U'т'};
            static const char32_t v_em[]  = {U'е',U'м'};
            static const char32_t v_ny[]  = {U'н',U'ы'};
            static const char32_t v_t[]   = {U'т',U'ь'};
            static const char32_t v_y[]   = {U'й'};
            static const char32_t v_l[]   = {U'л'};
            static const char32_t v_n[]   = {U'н'};

            static const Suf verb_a[] = {
                {v_nno,3},{v_esh,3},{v_ete,3},{v_yte,3},
                {v_yut,2},{v_la,2},{v_na,2},{v_li,2},{v_lo,2},{v_no,2},
                {v_et,2},{v_em,2},{v_ny,2},{v_t,2},
                {v_y,1},{v_l,1},{v_n,1}
            };

            static const char32_t vb_eite[] = {U'е',U'й',U'т',U'е'};
            static const char32_t vb_uite[] = {U'у',U'й',U'т',U'е'};
            static const char32_t vb_ena[]  = {U'е',U'н',U'а'};
            static const char32_t vb_ila[]  = {U'и',U'л',U'а'};
            static const char32_t vb_yla[]  = {U'ы',U'л',U'а'};
            static const char32_t vb_ite[]  = {U'и',U'т',U'е'};
            static const char32_t vb_ili[]  = {U'и',U'л',U'и'};
            static const char32_t vb_yli[]  = {U'ы',U'л',U'и'};
            static const char32_t vb_ilo[]  = {U'и',U'л',U'о'};
            static const char32_t vb_ylo[]  = {U'ы',U'л',U'о'};
            static const char32_t vb_eno[]  = {U'е',U'н',U'о'};
            static const char32_t vb_uet[]  = {U'у',U'е',U'т'};
            static const char32_t vb_eny[]  = {U'е',U'н',U'ы'};
            static const char32_t vb_ei[]   = {U'е',U'й'};
            static const char32_t vb_ui[]   = {U'у',U'й'};
            static const char32_t vb_en[]   = {U'е',U'н'};
            static const char32_t vb_il[]   = {U'и',U'л'};
            static const char32_t vb_yl[]   = {U'ы',U'л'};
            static const char32_t vb_im[]   = {U'и',U'м'};
            static const char32_t vb_ym[]   = {U'ы',U'м'};
            static const char32_t vb_yat[]  = {U'я',U'т'};
            static const char32_t vb_yut2[] = {U'ю',U'т'};
            static const char32_t vb_it[]   = {U'и',U'т'};
            static const char32_t vb_yt[]   = {U'ы',U'т'};
            static const char32_t vb_it2[]  = {U'и',U'т',U'ь'};
            static const char32_t vb_yt2[]  = {U'ы',U'т',U'ь'};
            static const char32_t vb_ish[]  = {U'и',U'ш',U'ь'};
            static const char32_t vb_uyu[]  = {U'у',U'ю'};
            static const char32_t vb_yu[]   = {U'ю'};

            static const Suf verb_b[] = {
                {vb_eite,4},{vb_uite,4},
                {vb_ena,3},{vb_ila,3},{vb_yla,3},{vb_ite,3},{vb_ili,3},{vb_yli,3},
                {vb_ilo,3},{vb_ylo,3},{vb_eno,3},{vb_uet,3},{vb_eny,3},
                {vb_it2,3},{vb_yt2,3},{vb_ish,3},
                {vb_ei,2},{vb_ui,2},{vb_en,2},{vb_il,2},{vb_yl,2},{vb_im,2},{vb_ym,2},
                {vb_yat,2},{vb_yut2,2},{vb_it,2},{vb_yt,2},{vb_uyu,2},
                {vb_yu,1}
            };

            bool verb_removed = remove_conditional_in_region(w, rv, verb_a, sizeof(verb_a)/sizeof(verb_a[0]), is_ay);
            if (!verb_removed) {
                verb_removed = remove_one_of_in_region(w, rv, verb_b, sizeof(verb_b)/sizeof(verb_b[0]));
            }

            if (!verb_removed) {
                static const char32_t n_иями[] = {U'и',U'я',U'м',U'и'};
                static const char32_t n_иях[]  = {U'и',U'я',U'х'};
                static const char32_t n_ией[]  = {U'и',U'е',U'й'};
                static const char32_t n_ями[]  = {U'я',U'м',U'и'};
                static const char32_t n_ами[]  = {U'а',U'м',U'и'};
                static const char32_t n_иям[]  = {U'и',U'я',U'м'};
                static const char32_t n_ием[]  = {U'и',U'е',U'м'};
                static const char32_t n_ях[]   = {U'я',U'х'};
                static const char32_t n_ах[]   = {U'а',U'х'};
                static const char32_t n_ев[]   = {U'е',U'в'};
                static const char32_t n_ов[]   = {U'о',U'в'};
                static const char32_t n_ье[]   = {U'ь',U'е'};
                static const char32_t n_ие[]   = {U'и',U'е'};
                static const char32_t n_еи[]   = {U'е',U'и'};
                static const char32_t n_ии[]   = {U'и',U'и'};
                static const char32_t n_ей[]   = {U'е',U'й'};
                static const char32_t n_ой[]   = {U'о',U'й'};
                static const char32_t n_ий[]   = {U'и',U'й'};
                static const char32_t n_ем[]   = {U'е',U'м'};
                static const char32_t n_ом[]   = {U'о',U'м'};
                static const char32_t n_ям[]   = {U'я',U'м'};
                static const char32_t n_ам[]   = {U'а',U'м'};
                static const char32_t n_ию[]   = {U'и',U'ю'};
                static const char32_t n_ью[]   = {U'ь',U'ю'};
                static const char32_t n_ия[]   = {U'и',U'я'};
                static const char32_t n_ья[]   = {U'ь',U'я'};
                static const char32_t n_а[]    = {U'а'};
                static const char32_t n_я[]    = {U'я'};
                static const char32_t n_о[]    = {U'о'};
                static const char32_t n_е[]    = {U'е'};
                static const char32_t n_ы[]    = {U'ы'};
                static const char32_t n_и[]    = {U'и'};
                static const char32_t n_у[]    = {U'у'};
                static const char32_t n_ю[]    = {U'ю'};
                static const char32_t n_ь[]    = {U'ь'};
                static const char32_t n_й[]    = {U'й'};

                static const Suf noun_full[] = {
                    {n_иями,4},
                    {n_иях,3},{n_ией,3},{n_ями,3},{n_ами,3},{n_иям,3},{n_ием,3},
                    {n_ях,2},{n_ах,2},{n_ев,2},{n_ов,2},{n_ье,2},{n_ие,2},{n_еи,2},{n_ии,2},
                    {n_ей,2},{n_ой,2},{n_ий,2},{n_ем,2},{n_ом,2},{n_ям,2},{n_ам,2},
                    {n_ию,2},{n_ью,2},{n_ия,2},{n_ья,2},
                    {n_а,1},{n_я,1},{n_о,1},{n_е,1},{n_ы,1},{n_и,1},{n_у,1},{n_ю,1},{n_ь,1},{n_й,1}
                };

                remove_one_of_in_region(w, rv, noun_full, sizeof(noun_full)/sizeof(noun_full[0]));
            }
        }
    }

    static const char32_t suf_i[] = {U'и'};
    remove_suffix_in_region(w, rv, suf_i, 1);

    static const char32_t suf_osty[] = {U'о',U'с',U'т',U'ь'};
    static const char32_t suf_ost[]  = {U'о',U'с',U'т'};
    if (!remove_suffix_in_region(w, r2, suf_osty, 4)) {
        remove_suffix_in_region(w, r2, suf_ost, 3);
    }

    static const char32_t suf_soft[] = {U'ь'};
    if (!remove_suffix_in_region(w, rv, suf_soft, 1)) {
        static const char32_t suf_eyshe[] = {U'е',U'й',U'ш',U'е'};
        static const char32_t suf_eysh[]  = {U'е',U'й',U'ш'};
        if (!remove_suffix_in_region(w, rv, suf_eyshe, 4)) {
            remove_suffix_in_region(w, rv, suf_eysh, 3);
        }
        static const char32_t suf_nn[] = {U'н',U'н'};
        if (ends_with(w, suf_nn, 2) && (w.size() - 2) >= rv) {
            w.resize(w.size() - 1);
        }
    }

    return w;
}

static std::string stem_utf8_ru(const std::string& s) {
    std::u32string orig = to_u32_ru_fold(s);
    std::u32string w = orig;
    std::u32string st = stem_ru_snowball(std::move(w));
    if (st.size() < 3) return from_u32(orig);
    return from_u32(st);
}

#ifndef STEMMER_NO_MAIN
int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { std::cout << "\n"; continue; }
        std::cout << stem_utf8_ru(line) << "\n";
    }
    return 0;
}
#endif
