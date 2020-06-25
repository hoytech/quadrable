#pragma once

#include "quadrable.h"



namespace quadrable { namespace proofTransport {



std::string varInt(uint64_t n) {
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




/*

ProofCmd encoding

    do hashing: 0[7 bits hashing details, all 0 == merge]
          jump: 10[6 bits signed jump]   can jump from -32 to +31
 long jump fwd: 110[n=5 bits jump]    jumps 2^(n+5)
 long jump rev: 111[n=5 bits jump]    jumps -2^(n+5)

*/




enum class EncodingType {
    CompactNoKeys = 0,
    CompactWithKeys = 1,
};


static inline std::string encodeProof(Proof &p, EncodingType encodingType = EncodingType::CompactNoKeys) {
    std::string o;

    // Proof encoding type

    o += static_cast<unsigned char>(encodingType);

    // Proof elements

    for (auto &elem : p.elems) {
        o += static_cast<unsigned char>(elem.elemType);
        o += static_cast<unsigned char>(elem.depth);

        if (elem.elemType == ProofElem::Type::Leaf) {
            if (encodingType == EncodingType::CompactNoKeys) {
                o += elem.keyHash;
            } else if (encodingType == EncodingType::CompactWithKeys) {
                if (elem.key.size() == 0) throw quaderr("CompactWithKeys specified in proof encoding, but key not available");
                o += varInt(elem.key.size());
                o += elem.key;
            }

            o += varInt(elem.val.size());
            o += elem.val;
        } else {
            throw quaderr("unrecognized ProofElem::Type when encoding proof: ", int(elem.elemType));
        }
    }

    o += static_cast<unsigned char>(ProofElem::Type::Invalid); // end of elem list

    // Proof cmds

    if (p.elems.size() == 0) return o;

    uint64_t currPos = p.elems.size() - 1; // starts at end

    for (auto &cmd : p.cmds) {
        if (cmd.nodeOffset == currPos) {
        }
    }

    return o;
}


}}
