#pragma once

namespace quadrable {


struct Update {
    std::string key; // only used if trackKeys is set
    std::string val;
    bool deletion;
    uint64_t nodeId = 0; // when a leaf is split, a special-case Update is created with this set
};

using UpdateSetMap = std::map<Key, Update>;



struct GetMultiResult {
    bool exists;
    std::string_view val;
};

using GetMultiQuery = std::map<std::string, GetMultiResult>;

using GetMultiIntegerQuery = std::map<uint64_t, GetMultiResult>;



struct ProofStrand {
    enum class Type {
        Leaf = 0,
        Invalid = 1,
        WitnessLeaf = 2,
        WitnessEmpty = 3,
    } strandType;
    uint64_t depth;
    std::string keyHash;
    std::string val;  // Type::Leaf: value, Type::WitnessLeaf: hash(value), Type::WitnessEmpty: ignored
    std::string key;  // Type::Leaf: key (if available), Type::Witness*: ignored
};

struct ProofCmd {
    enum class Op {
        HashProvided = 0,
        HashEmpty = 1,
        Merge = 2,
    } op;
    uint64_t nodeOffset;
    std::string hash; // HashProvided ops only
};

struct Proof {
    std::vector<ProofStrand> strands;
    std::vector<ProofCmd> cmds;
};


}
