#pragma once

#include "blake2.h"


namespace quadrable {


class Hash {
  public:
    Hash(size_t outputSize_) : outputSize(outputSize_) {
        blake2s_init(&s, outputSize);
    }

    void update(std::string_view sv) {
        blake2s_update(&s, reinterpret_cast<const uint8_t*>(sv.data()), sv.length());
    }

    void update(const uint8_t *input, size_t length) {
        blake2s_update(&s, input, length);
    }

    void final(uint8_t *output) {
        blake2s_final(&s, output, outputSize);
    }

  private:
    blake2s_state s;
    size_t outputSize;
};


class Key {
  public:
    Key() {};

    static Key hash(std::string_view s) {
        Key k;

        {
            Hash h(sizeof(k.data));
            h.update(s);
            h.final(k.data);
        }

        return k;
    }

    static Key existing(std::string_view s) {
        Key h;

        if (s.size() != sizeof(h.data)) throw quaderr("incorrect size for existing");
        memcpy(h.data, s.data(), sizeof(h.data));

        return h;
    }

    static Key null() {
        Key h;

        memset(h.data, '\0', sizeof(h.data));

        return h;
    }

    static Key max() {
        Key h;

        memset(h.data, '\xFF', sizeof(h.data));

        return h;
    }

    static Key fromInteger(uint64_t n) {
        if (n > std::numeric_limits<uint64_t>::max() - 2) throw quaderr("int range exceeded");

        uint64_t bits = 63 - __builtin_clzll(n + 2);

        uint64_t offset = (1ULL << bits) - 2;
        auto b = std::bitset<128>(bits - 1) << (128 - 6);
        b |= (std::bitset<128>(n - offset) << (128 - 6 - bits));

        uint64_t w1 = (b >> 64).to_ullong();
        uint64_t w2 = ((b << 64) >> 64).to_ullong();

        Key h = null();

        h.data[0] = (w1 >> (64 - 8)) & 0xFF;
        h.data[1] = (w1 >> (64 - 8*2)) & 0xFF;
        h.data[2] = (w1 >> (64 - 8*3)) & 0xFF;
        h.data[3] = (w1 >> (64 - 8*4)) & 0xFF;
        h.data[4] = (w1 >> (64 - 8*5)) & 0xFF;
        h.data[5] = (w1 >> (64 - 8*6)) & 0xFF;
        h.data[6] = (w1 >> (64 - 8*7)) & 0xFF;
        h.data[7] = (w1 >> (64 - 8*8)) & 0xFF;
        h.data[8] = (w2 >> (64 - 8)) & 0xFF;

        return h;
    }

    uint64_t toInteger() {
        if (std::any_of(data + 16, data + sizeof(data), [](uint8_t c){ return c != 0; })) throw quaderr("hash is not in integer format");

        uint64_t w1, w2;

        w1 = data[0];
        w1 = (w1 << 8) | data[1];
        w1 = (w1 << 8) | data[2];
        w1 = (w1 << 8) | data[3];
        w1 = (w1 << 8) | data[4];
        w1 = (w1 << 8) | data[5];
        w1 = (w1 << 8) | data[6];
        w1 = (w1 << 8) | data[7];
        w2 = static_cast<uint64_t>(data[8]) << (8 * 7);

        uint64_t bits = w1 >> (64 - 6);

        auto b = (std::bitset<128>(w1) << 64) | std::bitset<128>(w2);
        b <<= 6;
        b >>= 128 - bits - 1;

        uint64_t n = b.to_ullong();

        uint64_t offset = (1ULL << (bits + 1)) - 2;

        return n + offset;
    }

    std::string str() const {
        return std::string(reinterpret_cast<const char*>(data), sizeof(data));
    }

    std::string_view sv() const {
        return std::string_view(reinterpret_cast<const char*>(data), sizeof(data));
    }

    bool getBit(size_t n) const {
        return !!(data[n / 8] & (128 >> (n % 8)));
    }

    void keepPrefixBits(size_t n) {
        if (n > 256) throw quaderr("requested to zero out too many bits");
        if (n == 256) return;

        data[n / 8] &= 0xFF & (0xFF00 >> (n % 8));
        size_t remaining = 32 - (n / 8);
        if (remaining) memset(data + (n/8) + 1, '\0', remaining - 1);
    }

    uint8_t data[32];
};

inline bool operator <(const Key &h1, const Key &h2) {
    return memcmp(h1.data, h2.data, sizeof(h1.data)) < 0;
}

inline bool operator ==(const Key &h1, const Key &h2) {
    return memcmp(h1.data, h2.data, sizeof(h1.data)) == 0;
}

inline bool operator ==(const Key &h1, std::string_view sv) {
    if (sv.size() != sizeof(h1.data)) return false;
    return memcmp(h1.data, sv.data(), sizeof(h1.data)) == 0;
}

inline bool operator !=(const Key &h1, std::string_view sv) {
    return !(h1 == sv);
}


}
