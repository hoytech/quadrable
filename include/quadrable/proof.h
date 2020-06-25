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

Proof encoding:

[1 byte proof type]
[ProofElem]+
[ProofCmd]+


ProofElem:

[1 byte elem type, Invalid means end of elems]
[1 byte depth]
if CompactNoKeys:
  [32 byte keyHash]
else if CompactWithKeys:
  [varint size of key]
  [N-byte key]
[varint size of val]
[N-byte val]


ProofCmd:

       hashing: 0[7 bits hashing details, all 0 == merge]
short jump fwd: 100[5 bits distance]   jumps d+1, range: 1 to 32
short jump rev: 101[5 bits distance]   jumps -(d+1) range: -1 to -32
 long jump fwd: 110[5 bits distance]   jumps 2^(d+6) range: 64, 128, 256, 512, ..., 2^37
 long jump rev: 111[5 bits distance]   jumps -2^(d+6) range: -64, -128, -256, -512, ..., -2^37

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
    std::vector<ProofCmd> hashQueue;

    auto flushHashQueue = [&]{
        if (hashQueue.size() == 0) return;
        assert(hashQueue.size() < 7);

        uint64_t bits = 0;

        for (size_t i = 0; i < hashQueue.size() ; i++) {
            if (hashQueue[i].op == ProofCmd::Op::HashProvided) bits |= 1 << (7 - i);
        }

        bits |= 1 << (6 - hashQueue.size());

        o += static_cast<unsigned char>(bits); // hashing

        for (auto &cmd : hashQueue) {
            if (cmd.op == ProofCmd::Op::HashProvided) o += cmd.hash;
        }

        hashQueue.clear();
    };

    for (auto &cmd : p.cmds) {
        if (cmd.nodeOffset != currPos) {
            flushHashQueue();

            // jump

            currPos = cmd.nodeOffset;
        }

        if (cmd.op == ProofCmd::Op::Merge) {
            // merge
            flushHashQueue();
            o += static_cast<unsigned char>(0);
        } else {
            // hash provided/empty
            hashQueue.emplace_back(cmd);
            if (hashQueue.size() == 6) flushHashQueue();
        }
    }

    flushHashQueue();

    return o;
}


}}
