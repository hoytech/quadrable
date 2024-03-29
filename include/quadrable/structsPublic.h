#pragma once

namespace quadrable {


// These values are for internal DB reference only (proofs have different values)

enum class NodeType {
    Empty = 0,
    BranchLeft = 1,
    BranchRight = 2,
    BranchBoth = 3,
    Leaf = 4,
    Witness = 5,
    WitnessLeaf = 6,
    Invalid = 15,
};



struct Update {
    std::string key; // only used if trackKeys is set
    std::string val;
    bool deletion;
    uint64_t *outputNodeId = nullptr; // if non-null, write out a newly created node's id here
    uint64_t nodeIdOverride = 0; // To force re-use of a node. Also, when a leaf is split, a special-case Update is created with this
};

using UpdateSetMap = std::map<Key, Update>;



struct GetMultiResult {
    bool exists;
    std::string_view val;
    uint64_t nodeId;
};

using GetMultiQuery = std::map<std::string, GetMultiResult>;

using GetMultiIntegerQuery = std::map<uint64_t, GetMultiResult>;



struct ProofStrand {
    enum class Type {
        Leaf = 0,
        Invalid = 1,
        WitnessLeaf = 2,
        WitnessEmpty = 3,
        Witness = 4,
    } strandType;
    uint64_t depth;
    std::string keyHash;
    std::string val;  // Type::Leaf: value, Type::WitnessLeaf: hash(value), Type::WitnessEmpty: ignored, Type::Witness: nodeHash
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



struct SyncRequest {
    Key path;
    uint64_t startDepth;
    uint64_t depthLimit;
    bool expandLeaves;
};

using SyncRequests = std::vector<SyncRequest>;
using SyncResponses = std::vector<Proof>;



const uint64_t firstInteriorNodeId = 288230376151711744ULL; // 2**58
const uint64_t firstMemStoreNodeId = 576460752303423488ULL; // 2**59

struct MemStore {
    std::map<uint64_t, std::string> nodes;
    uint64_t headNodeId = 0;
};


}
