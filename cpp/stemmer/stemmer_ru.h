#pragma once
#include <cstddef>
#include <cstdint>

using u32 = uint32_t;

char* stem_ru_utf8(const char* s, std::size_t n, u32* out_len);
