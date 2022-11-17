#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <functional>

#include "quadrable/utils.h"



namespace quadrable {


inline std::string encodeVarInt(uint64_t n) {
    if (n == 0) return std::string(1, '\0');

    std::string o;

    while (n) {
        o.push_back(static_cast<unsigned char>(n & 0x7F));
        n >>= 7;
    }

    std::reverse(o.begin(), o.end());

    for (size_t i = 0; i < o.size() - 1; i++) {
        o[i] |= 0x80;
    }

    return o;
}

inline uint64_t decodeVarInt(std::function<unsigned char()> getByte) {
    uint64_t res = 0;

    while (1) {
        uint64_t byte = getByte();
        res = (res << 7) | (byte & 0b0111'1111);
        if ((byte & 0b1000'0000) == 0) break;
    }

    return res;
}

inline uint64_t decodeVarInt(std::string_view s) {
    size_t next = 0;

    return decodeVarInt([&]{
        if (next == s.size()) throw quaderr("premature end of varint");
        return s[next++];
    });
}


}
