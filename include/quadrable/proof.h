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

    // Encoding type

    o += static_cast<unsigned char>(encodingType);

    // Elements

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

    // Cmds

    if (p.elems.size() == 0) return o;

    uint64_t currPos = p.elems.size() - 1; // starts at end
    std::vector<ProofCmd> hashQueue;

    auto flushHashQueue = [&]{
        if (hashQueue.size() == 0) return;
        assert(hashQueue.size() < 7);

        uint64_t bits = 0;

        for (size_t i = 0; i < hashQueue.size() ; i++) {
            if (hashQueue[i].op == ProofCmd::Op::HashProvided) bits |= 1 << (6 - i);
        }

        bits |= 1 << (6 - hashQueue.size());

        o += static_cast<unsigned char>(bits); // hashing

        for (auto &cmd : hashQueue) {
            if (cmd.op == ProofCmd::Op::HashProvided) o += cmd.hash;
        }

        hashQueue.clear();
    };

    for (auto &cmd : p.cmds) {
        while (cmd.nodeOffset != currPos) {
            flushHashQueue();

            int64_t delta = static_cast<int64_t>(cmd.nodeOffset) - static_cast<int64_t>(currPos);

            if (delta >= 1 && delta <= 32) {
                o += static_cast<unsigned char>(0b1000'0000 | (delta - 1));
                currPos = cmd.nodeOffset;
            } else if (delta >= -32 && delta <= -1) {
                o += static_cast<unsigned char>(0b1010'0000 | (std::abs(delta) - 1));
                currPos = cmd.nodeOffset;
            } else {
                throw quaderr("not impl");
            }
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


static inline Proof decodeProof(std::string_view encoded) {
    Proof proof;

    auto getByte = [&](){
        if (encoded.size() < 1) throw quaderr("proof ends prematurely");
        auto res = static_cast<uint64_t>(encoded[0]);
        encoded = encoded.substr(1);
        return res;
    };

    auto getBytes = [&](size_t n){
        if (encoded.size() < n) throw quaderr("proof ends prematurely");
        auto res = encoded.substr(0, n);
        encoded = encoded.substr(n);
        return res;
    };

    auto getVarInt = [&](){
        uint64_t res = 0;

        while (1) {
            uint64_t byte = getByte();
            res = (res << 7) | (byte & 0b0111'1111);
            if ((byte & 0b1000'0000) == 0) break;
        }

        return res;
    };

    // Encoding type

    auto encodingType = static_cast<EncodingType>(getByte());

    if (encodingType != EncodingType::CompactNoKeys && encodingType != EncodingType::CompactWithKeys) {
        throw quaderr("unexpected proof encoding type: ", (int)encodingType);
    }

    // Elements

    while (1) {
        auto elemType = static_cast<ProofElem::Type>(getByte());

        if (elemType == ProofElem::Type::Invalid) break;

        if (elemType != ProofElem::Type::Leaf && elemType != ProofElem::Type::WitnessLeaf && elemType != ProofElem::Type::WitnessEmpty) {
            throw quaderr("unexpected proof element type: ", (int)elemType);
        }

        ProofElem elem{elemType};

        elem.depth = getByte();

        if (encodingType == EncodingType::CompactNoKeys) {
            elem.keyHash = std::string(getBytes(32));
        } else if (encodingType == EncodingType::CompactWithKeys) {
            auto keySize = getVarInt();
            elem.key = std::string(getBytes(keySize));
            elem.keyHash = Hash::hash(elem.key).str();
        }

        auto valSize = getVarInt();
        elem.val = std::string(getBytes(valSize));

        proof.elems.emplace_back(std::move(elem));
    }

    // Cmds

    if (proof.elems.size() == 0) return proof;

    uint64_t currPos = proof.elems.size() - 1; // starts at end

    while (encoded.size()) {
        auto byte = getByte();

        if (byte == 0) {
            proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::Merge, currPos, });
        } else if ((byte & 0b1000'0000) == 0) {
            while ((byte & 1) == 0) byte >>= 1;
            byte >>= 1;

            while (byte) {
                if ((byte & 1)) {
                    proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::HashProvided, currPos, std::string(getBytes(32)), });
                } else {
                    proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::HashEmpty, currPos, });
                }

                byte >>= 1;
            }
        } else {
            auto action = byte >> 5;
            auto distance = byte & 0b1'1111;

            if (action == 0b100) { // short jump fwd
                currPos += distance + 1;
            } else if (action == 0b101) { // short jump rev
                currPos -= distance + 1;
            } else if (action == 0b110) { // long jump fwd
                currPos += 1 << (distance + 6);
            } else if (action == 0b111) { // long jump rev
                currPos -= 1 << (distance + 6);
            }

            if (currPos > proof.elems.size()) { // rely on unsigned underflow to catch negative range
                throw quaderr("jumped outside of proof elems");
            }
        }
    }

    return proof;
}


}}
