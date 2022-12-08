#pragma once

#include "quadrable/utils.h"
#include "quadrable/varint.h"



namespace quadrable { namespace proofTransport {



enum class EncodingType {
    HashedKeys = 0,
    FullKeys = 1,
};


inline std::string encodeProof(const Proof &p, EncodingType encodingType = EncodingType::HashedKeys) {
    std::string o;

    // Encoding type

    o += static_cast<unsigned char>(encodingType);

    // Strands

    auto addKeyHash = [&](std::string_view keyHash){
        uint64_t numTrailingZeros = 0;
        for (int i = 31; i >= 0; i--) {
            if (keyHash[static_cast<size_t>(i)] != '\0') break;
            numTrailingZeros++;
        }

        o += static_cast<unsigned char>(numTrailingZeros);
        o += keyHash.substr(0, 32 - numTrailingZeros);
    };

    for (auto &strand : p.strands) {
        o += static_cast<unsigned char>(strand.strandType);
        o += static_cast<unsigned char>(strand.depth);

        if (strand.strandType == ProofStrand::Type::Leaf) {
            if (encodingType == EncodingType::HashedKeys) {
                addKeyHash(strand.keyHash);
            } else if (encodingType == EncodingType::FullKeys) {
                if (strand.key.size() == 0) throw quaderr("FullKeys specified in proof encoding, but key not available");
                o += encodeVarInt(strand.key.size());
                o += strand.key;
            }

            o += encodeVarInt(strand.val.size());
            o += strand.val;
        } else if (strand.strandType == ProofStrand::Type::WitnessLeaf) {
            addKeyHash(strand.keyHash);
            o += strand.val; // holds valHash
        } else if (strand.strandType == ProofStrand::Type::WitnessEmpty) {
            addKeyHash(strand.keyHash);
        } else if (strand.strandType == ProofStrand::Type::Witness) {
            addKeyHash(strand.keyHash);
            o += strand.val; // holds nodeHash
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


inline Proof decodeProof(std::string_view encoded) {
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

    auto getKeyHash = [&](){
        auto numTrailingZeros = getByte();
        return std::string(getBytes(32 - numTrailingZeros)) + std::string(numTrailingZeros, '\0');
    };


    // Encoding type

    auto encodingType = static_cast<EncodingType>(getByte());

    if (encodingType != EncodingType::HashedKeys && encodingType != EncodingType::FullKeys) {
        throw quaderr("unexpected proof encoding type: ", (int)encodingType);
    }

    // Strands

    while (1) {
        auto strandType = static_cast<ProofStrand::Type>(getByte());

        if (strandType == ProofStrand::Type::Invalid) break; // end of strands

        ProofStrand strand{strandType};

        strand.depth = getByte();

        if (strandType == ProofStrand::Type::Leaf) {
            if (encodingType == EncodingType::HashedKeys) {
                strand.keyHash = getKeyHash();
            } else if (encodingType == EncodingType::FullKeys) {
                auto keySize = decodeVarInt(getByte);
                strand.key = std::string(getBytes(keySize));
                strand.keyHash = Key::hash(strand.key).str();
            }

            auto valSize = decodeVarInt(getByte);
            strand.val = std::string(getBytes(valSize));
        } else if (strandType == ProofStrand::Type::WitnessLeaf) {
            strand.keyHash = getKeyHash();
            strand.val = std::string(getBytes(32)); // holds valHash
        } else if (strandType == ProofStrand::Type::WitnessEmpty) {
            strand.keyHash = getKeyHash();
        } else if (strandType == ProofStrand::Type::Witness) {
            strand.keyHash = getKeyHash();
            strand.val = std::string(getBytes(32)); // holds nodeHash
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
