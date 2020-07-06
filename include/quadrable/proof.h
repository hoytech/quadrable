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





enum class EncodingType {
    CompactNoKeys = 0,
    CompactWithKeys = 1,
};


static inline std::string encodeProof(const Proof &p, EncodingType encodingType = EncodingType::CompactNoKeys) {
    std::string o;

    // Encoding type

    o += static_cast<unsigned char>(encodingType);

    // Strands

    for (auto &strand : p.strands) {
        o += static_cast<unsigned char>(strand.strandType);
        o += static_cast<unsigned char>(strand.depth);

        if (strand.strandType == ProofStrand::Type::Leaf) {
            if (encodingType == EncodingType::CompactNoKeys) {
                o += strand.keyHash;
            } else if (encodingType == EncodingType::CompactWithKeys) {
                if (strand.key.size() == 0) throw quaderr("CompactWithKeys specified in proof encoding, but key not available");
                o += varInt(strand.key.size());
                o += strand.key;
            }

            o += varInt(strand.val.size());
            o += strand.val;
        } else if (strand.strandType == ProofStrand::Type::WitnessLeaf) {
            o += strand.keyHash;
            o += strand.val; // holds valHash
        } else if (strand.strandType == ProofStrand::Type::WitnessEmpty) {
            o += strand.keyHash;
        } else {
            throw quaderr("unrecognized ProofStrand::Type when encoding proof: ", (int)strand.strandType);
        }
    }

    o += static_cast<unsigned char>(ProofStrand::Type::Invalid); // end of strand list

    // Cmds

    if (p.strands.size() == 0) return o;

    uint64_t currPos = p.strands.size() - 1; // starts at end
    std::vector<ProofCmd> hashQueue;

    auto flushHashQueue = [&]{
        if (hashQueue.size() == 0) return;
        assert(hashQueue.size() < 7);

        uint64_t bits = 0;

        for (size_t i = 0; i < hashQueue.size() ; i++) {
            if (hashQueue[i].op == ProofCmd::Op::HashProvided) bits |= 1 << i;
        }

        bits = (bits << 1) | 1;
        bits <<= (6 - hashQueue.size());

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

            if (delta >= 1 && delta < 64) {
                uint64_t distance = std::min(delta, (int64_t)32);
                o += static_cast<unsigned char>(0b1000'0000 | (distance - 1));
                currPos += distance;
            } else if (delta > -64 && delta <= -1) {
                uint64_t distance = std::min(std::abs(delta), (int64_t)32);
                o += static_cast<unsigned char>(0b1010'0000 | (distance - 1));
                currPos -= distance;
            } else {
                uint64_t logDistance = 64 - __builtin_clzll(static_cast<unsigned long>(std::abs(delta)));

                if (delta > 0) {
                    o += static_cast<unsigned char>(0b1100'0000 | (logDistance - 7));
                    currPos += 1 << (logDistance - 1);
                } else {
                    o += static_cast<unsigned char>(0b1110'0000 | (logDistance - 7));
                    currPos -= 1 << (logDistance - 1);
                }
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
        auto res = static_cast<unsigned char>(encoded[0]);
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

    // Strands

    while (1) {
        auto strandType = static_cast<ProofStrand::Type>(getByte());

        if (strandType == ProofStrand::Type::Invalid) break; // end of strands

        ProofStrand strand{strandType};

        strand.depth = getByte();

        if (strandType == ProofStrand::Type::Leaf) {
            if (encodingType == EncodingType::CompactNoKeys) {
                strand.keyHash = std::string(getBytes(32));
            } else if (encodingType == EncodingType::CompactWithKeys) {
                auto keySize = getVarInt();
                strand.key = std::string(getBytes(keySize));
                strand.keyHash = Hash::hash(strand.key).str();
            }

            auto valSize = getVarInt();
            strand.val = std::string(getBytes(valSize));
        } else if (strandType == ProofStrand::Type::WitnessLeaf) {
            strand.keyHash = std::string(getBytes(32));
            strand.val = std::string(getBytes(32)); // holds valHash
        } else if (strandType == ProofStrand::Type::WitnessEmpty) {
            strand.keyHash = std::string(getBytes(32));
        } else {
            throw quaderr("unrecognized ProofStrand::Type when decoding proof: ", (int)strandType);
        }

        proof.strands.emplace_back(std::move(strand));
    }

    // Cmds

    if (proof.strands.size() == 0) return proof;

    uint64_t currPos = proof.strands.size() - 1; // starts at end

    while (encoded.size()) {
        auto byte = getByte();

        if (byte == 0) {
            proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::Merge, currPos, });
        } else if ((byte & 0b1000'0000) == 0) {
            bool started = false;

            for (int i=0; i<7; i++) {
                if (started) {
                    if ((byte & 1)) {
                        proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::HashProvided, currPos, std::string(getBytes(32)), });
                    } else {
                        proof.cmds.emplace_back(ProofCmd{ ProofCmd::Op::HashEmpty, currPos, });
                    }
                } else {
                    if ((byte & 1)) started = true;
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

            if (currPos >= proof.strands.size()) { // rely on unsigned underflow to catch negative range
                throw quaderr("jumped outside of proof strands");
            }
        }
    }

    return proof;
}


}}
