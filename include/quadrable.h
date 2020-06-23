#pragma once

#include <string.h>
#include <assert.h>

#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <iterator>
#include <algorithm>

#include "lmdbxx/lmdb++.h"

#include "quadrable/keccak.h"





namespace quadrable {





inline void buildString(std::ostream&) { }

template<class First, class... Rest>
inline void buildString(std::ostream& o, const First& value, const Rest&... rest) {
    o << value;
    buildString(o, rest...);
}

template<class... T>
std::runtime_error quaderr(const T&... value) {
    std::ostringstream o;
    buildString(o, value...);
    return std::runtime_error(o.str());
}






class Hash {
  public:
    Hash() {};

    static Hash hash(std::string_view s) {
        Hash h;

        Keccak k;
        k.add(s.data(), s.size());
        k.getHash(h.data);

        return h;
    }

    static Hash existingHash(std::string_view s) {
        Hash h;

        if (s.size() != sizeof(h.data)) throw quaderr("incorrect size for existingHash");
        memcpy(h.data, s.data(), sizeof(h.data));

        return h;
    }

    static Hash nullHash() {
        Hash h;

        memset(h.data, '\0', sizeof(h.data));

        return h;
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

static inline bool operator <(const Hash &h1, const Hash &h2) {
    return memcmp(h1.data, h2.data, sizeof(h1.data)) < 0;
}

static inline bool operator ==(const Hash &h1, const Hash &h2) {
    return memcmp(h1.data, h2.data, sizeof(h1.data)) == 0;
}

static inline bool operator ==(const Hash &h1, std::string_view sv) {
    if (sv.size() != sizeof(h1.data)) return false;
    return memcmp(h1.data, sv.data(), sizeof(h1.data)) == 0;
}

static inline bool operator !=(const Hash &h1, std::string_view sv) {
    return !(h1 == sv);
}






struct Update {
    std::string val;
    bool deletion;
    bool witness; // if true, val is a hash of the value. used for inserting WitnessLeaf nodes
};

using UpdateSetMap = std::map<Hash, Update>;



struct GetMultiResult {
    bool exists;
    std::string_view val;
};

using GetMultiQuery = std::map<std::string, GetMultiResult>;





struct ProofElem {
    enum class Type {
        Leaf = 0,
        WitnessLeaf = 1,
        WitnessEmpty = 2,
    } proofType;
    uint64_t depth;
    std::string keyHash;
    std::string val;  // Type::Leaf: value, Type::WitnessLeaf: hash(value), Type::WitnessEmpty: ignored
};

struct ProofCmd {
    enum class Op {
        HashProvided = 0,
        HashEmpty = 1,
        Merge = 2,
    } op;
    uint64_t nodeOffset;
    Hash h; // HashProvided cmds only
};

struct Proof {
    std::vector<ProofElem> elems;
    std::vector<ProofCmd> cmds;
};














// nodeId: incrementing uint64_t

// node format:
//   branch left:  <8 bytes: \x01 tag + left nodeId> <32 bytes: nodeHash>
//   branch right: <8 bytes: \x02 tag + right nodeId> <32 bytes: nodeHash>
//   branch both:  <8 bytes: \x03 tag + left nodeId> <32 bytes: nodeHash> <8 bytes: right nodeId>
//   leaf:         <8 bytes: \x04 tag + 0> <32 bytes: nodeHash> <32 bytes: keyHash> <N bytes: val>
//
//   witness:      <8 bytes: \x05 tag + 0> <32 bytes: nodeHash>
//   witnessLeaf:  <8 bytes: \x06 tag + 0> <32 bytes: nodeHash> <32 bytes: keyHash> <32 bytes: valHash>

enum class NodeType {
    Empty = 0,
    BranchLeft = 1,
    BranchRight = 2,
    BranchBoth = 3,
    Leaf = 4,
    Witness = 5,
    WitnessLeaf = 6,
};


static inline std::string buildNodeEmpty() {
    return std::string(40, '\0');
}

static inline std::string buildNodeLeaf(const Hash &keyHash, std::string_view val, uint64_t depth) {
    Hash nodeHash;

    {
        Hash valHash = Hash::hash(val);
        unsigned char depthChar = static_cast<unsigned char>(depth);

        Keccak k;

        k.add(keyHash.sv());
        k.add(&depthChar, 1);
        k.add(valHash.sv());

        k.getHash(nodeHash.data);
    }

    std::string o;

    o += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Leaf));
    o += nodeHash.sv();
    o += keyHash.sv();
    o += val;

    return o;
}

static inline std::string buildNodeBranch(uint64_t leftNodeId, uint64_t rightNodeId, Hash &leftHash, Hash &rightHash) {
    Hash nodeHash;

    {
        Keccak k;
        k.add(leftHash.data, sizeof(leftHash.data));
        k.add(rightHash.data, sizeof(rightHash.data));
        k.getHash(nodeHash.data);
    }

    std::string o;

    uint64_t w1;

    if (rightNodeId == 0) {
        w1 = uint64_t(NodeType::BranchLeft) | leftNodeId << 8;
    } else if (leftNodeId == 0) {
        w1 = uint64_t(NodeType::BranchRight) | rightNodeId << 8;
    } else {
        w1 = uint64_t(NodeType::BranchBoth) | leftNodeId << 8;
    }

    o += lmdb::to_sv<uint64_t>(w1);
    o += nodeHash.sv();

    if (rightNodeId && leftNodeId) {
        o += lmdb::to_sv<uint64_t>(rightNodeId);
    }

    return o;
}

static inline std::string buildNodeWitness(const Hash &hash) {
    std::string o;

    o += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Witness));
    o += hash.sv();

    return o;
}

static inline std::string buildNodeWitnessLeaf(const Hash &keyHash, const Hash &valHash, uint64_t depth) {
    Hash nodeHash;

    {
        unsigned char depthChar = static_cast<unsigned char>(depth);

        Keccak k;

        k.add(keyHash.sv());
        k.add(&depthChar, 1);
        k.add(valHash.sv());

        k.getHash(nodeHash.data);
    }

    std::string o;

    o += lmdb::to_sv<uint64_t>(uint64_t(NodeType::WitnessLeaf));
    o += nodeHash.sv();
    o += keyHash.sv();
    o += valHash.sv();

    return o;
}



class ParsedNode {
  public:

    NodeType nodeType;
    std::string_view raw;
    uint64_t leftNodeId = 0;
    uint64_t rightNodeId = 0;

    ParsedNode(lmdb::txn &txn, lmdb::dbi &dbi_node, uint64_t nodeId) {
        if (nodeId == 0) {
            nodeType = NodeType::Empty;
            return;
        }

        if (!dbi_node.get(txn, lmdb::to_sv<uint64_t>(nodeId), raw)) throw quaderr("couldn't find nodeId ", nodeId);

        if (raw.size() < 8) throw quaderr("invalid node, too short");

        uint64_t w1Packed = lmdb::from_sv<uint64_t>(raw.substr(0, 8));
        nodeType = static_cast<NodeType>(w1Packed & 0xFF);
        uint64_t w1 = w1Packed >> 8;

        if (nodeType == NodeType::BranchLeft) {
            leftNodeId = w1;
        } else if (nodeType == NodeType::BranchRight) {
            rightNodeId = w1;
        } else if (nodeType == NodeType::BranchBoth) {
            leftNodeId = w1;
            rightNodeId = lmdb::from_sv<uint64_t>(raw.substr(8 + 32, 8));
        }
    }

    bool isLeaf() {
        return nodeType == NodeType::Leaf || nodeType == NodeType::WitnessLeaf;
    }

    bool isBranch() {
        return nodeType == NodeType::BranchLeft || nodeType == NodeType::BranchRight || nodeType == NodeType::BranchBoth;
    }

    bool isWitness() {
        return nodeType == NodeType::Witness || nodeType == NodeType::WitnessLeaf;
    }

    std::string_view nodeHash() {
        static const char nullBytes[32] = {};
        if (nodeType == NodeType::Empty) return std::string_view{nullBytes, 32};
        return raw.substr(8, 32);
    }

    std::string_view leafKeyHash() {
        if (nodeType != NodeType::Leaf && nodeType != NodeType::WitnessLeaf) throw quaderr("node is not a Leaf/WitnessLeaf");
        return raw.substr(8 + 32, 32);
    }

    std::string_view leafVal() {
        if (nodeType != NodeType::Leaf) throw quaderr("node is not a Leaf");
        return raw.substr(8 + 32 + 32);
    }

    std::string leafValHash() {
        if (nodeType == NodeType::Leaf) {
            return Hash::hash(leafVal()).str();
        } else if (nodeType == NodeType::WitnessLeaf) {
            return std::string(raw.substr(8 + 32 + 32));
        }

        throw quaderr("node is not a Leaf/WitnessLeaf");
    }
};







static inline void checkDepth(uint64_t depth) {
    assert(depth <= 255); // should only happen on keccak256 collision (or a bug)
}



class Quadrable {
  public:
    Quadrable() {}

    void init(lmdb::txn &txn) {
        dbi_head = lmdb::dbi::open(txn, "quadrable_head", MDB_CREATE);
        dbi_node = lmdb::dbi::open(txn, "quadrable_node", MDB_CREATE | MDB_INTEGERKEY);
    }


    bool isDetachedHead() {
        return detachedHead;
    }

    std::string getHead() {
        if (detachedHead) throw quaderr("in detached head mode");
        return head;
    }

    std::string root(lmdb::txn &txn) {
        uint64_t nodeId = getHeadNodeId(txn);

        ParsedNode node(txn, dbi_node, nodeId);

        return std::string(node.nodeHash());
    }

    void checkout(uint64_t nodeId = 0) {
        detachedHead = true;
        detachedHeadNodeId = nodeId;
    }

    void checkout(std::string_view newHead) {
        head = newHead;
        detachedHead = false;
    }

    uint64_t getHeadNodeId(lmdb::txn &txn) {
        if (detachedHead) return detachedHeadNodeId;

        uint64_t nodeId = 0;

        std::string_view headRaw;
        if (dbi_head.get(txn, head, headRaw)) nodeId = lmdb::from_sv<uint64_t>(headRaw);

        return nodeId;
    }

    void setHeadNodeId(lmdb::txn &txn, uint64_t nodeId) {
        if (detachedHead) {
            detachedHeadNodeId = nodeId;
        } else {
            dbi_head.put(txn, head, lmdb::to_sv<uint64_t>(nodeId));
        }
    }

    void fork(lmdb::txn &txn) {
        uint64_t nodeId = getHeadNodeId(txn);
        checkout();
        setHeadNodeId(txn, nodeId);
    }

    void fork(lmdb::txn &txn, std::string newHead) {
        uint64_t nodeId = getHeadNodeId(txn);
        checkout(newHead);
        setHeadNodeId(txn, nodeId);
    }



    class UpdateSet {
      friend class Quadrable;

      public:
        UpdateSet(Quadrable *db_) : db(db_) {}

        UpdateSet &put(std::string_view keyRaw, std::string_view val) {
            map.insert_or_assign(Hash::hash(keyRaw), Update{std::string(val), false, false});
            return *this;
        }

        UpdateSet &del(std::string_view keyRaw) {
            map.insert_or_assign(Hash::hash(keyRaw), Update{"", true, false});
            return *this;
        }

        void apply(lmdb::txn &txn) {
            db->apply(txn, this);
        }

      private:
        void eraseRange(UpdateSetMap::iterator &begin, UpdateSetMap::iterator &end, std::function<bool(UpdateSetMap::iterator &)> pred) {
            if (begin == end) return;

            for (auto i = std::next(begin); i != end; ) {
                auto prev = i++;
                if (pred(prev)) map.erase(prev);
            }

            if (pred(begin)) ++begin;
        };

        UpdateSetMap map;
        Quadrable *db;
    };

    UpdateSet change() {
        return UpdateSet(this);
    }

    void apply(lmdb::txn &txn, UpdateSet *updatesOrig) {
        apply(txn, *updatesOrig);
    }

    struct PutNodeInfo {
        uint64_t nodeId;
        Hash nodeHash;
        NodeType nodeType;
    };

    void apply(lmdb::txn &txn, UpdateSet &updatesOrig) {
        // If exception is thrown, updatesOrig could be in inconsistent state, so ensure it's cleared
        UpdateSet updates = std::move(updatesOrig);

        uint64_t oldNodeId = getHeadNodeId(txn);

        bool bubbleUp = false;
        auto newNode = putAux(txn, 0, oldNodeId, updates, updates.map.begin(), updates.map.end(), bubbleUp);

        if (newNode.nodeId != oldNodeId) setHeadNodeId(txn, newNode.nodeId);
    }

    PutNodeInfo putAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, UpdateSet &updates, UpdateSetMap::iterator begin, UpdateSetMap::iterator end, bool &bubbleUp) {
        ParsedNode node(txn, dbi_node, nodeId);

        auto putNode = [&](NodeType nodeType, std::string_view nodeRaw){
            if (nodeRaw.size() < 40) throw quaderr("nodeRaw too short");
            uint64_t newNodeId = getNextIntegerKey(txn, dbi_node);
            dbi_node.put(txn, lmdb::to_sv<uint64_t>(newNodeId), nodeRaw);
            return PutNodeInfo{newNodeId, Hash::existingHash(nodeRaw.substr(8, 32)), nodeType};
        };

        auto reuseNode = [&]{
            return PutNodeInfo{nodeId, Hash::existingHash(node.nodeHash()), node.nodeType};
        };

        auto emptyNode = [&]{
            return PutNodeInfo{0, Hash::nullHash(), NodeType::Empty};
        };

        auto putLeaf = [&](const Hash &keyHash, std::string_view val, uint64_t depth, bool isWitness){
            if (isWitness) {
                return putNode(NodeType::WitnessLeaf, buildNodeWitnessLeaf(keyHash, Hash::existingHash(val), depth));
            } else {
                return putNode(NodeType::Leaf, buildNodeLeaf(keyHash, val, depth));
            }
        };

        bool didBubble = false;

        // recursion base cases

        if (begin == end) {
            return reuseNode();
        }

        if (node.nodeType == NodeType::Witness) {
            throw quaderr("encountered witness during update: partial tree");
        } else if (node.nodeType == NodeType::Empty) {
            updates.eraseRange(begin, end, [&](UpdateSetMap::iterator &u){ return u->second.deletion; });

            if (begin == end) {
                // All updates for this sub-tree were deletions for keys that don't exist, so do nothing.
                return reuseNode();
            }

            if (std::next(begin) == end) {
                return putLeaf(begin->first, begin->second.val, depth, begin->second.witness);
            }
        } else if (node.nodeType == NodeType::Leaf || node.nodeType == NodeType::WitnessLeaf) {
            if (std::next(begin) == end && begin->first == node.leafKeyHash()) {
                // Update an existing record

                if (begin->second.deletion) {
                    bubbleUp = true;
                    return emptyNode();
                }

                if (node.nodeType == NodeType::Leaf && begin->second.val == node.leafVal()) {
                    // No change to this leaf, so do nothing. Don't do this for WitnessLeaf nodes, since we need to upgrade them to Leafs.
                    return reuseNode();
                }

                return putLeaf(begin->first, begin->second.val, depth, begin->second.witness);
            }

            bool deleteThisLeaf = false;

            updates.eraseRange(begin, end, [&](UpdateSetMap::iterator &u){
                if (u->second.deletion && u->first == node.leafKeyHash()) {
                    deleteThisLeaf = true;
                    didBubble = true; // so we check the status of this node after handling any changes further down (may require bubbling up)
                }
                return u->second.deletion;
            });

            if (begin == end) {
                if (deleteThisLeaf) {
                    // The only update for this sub-tree was to delete this key
                    bubbleUp = true;
                    return emptyNode();
                }
                // All updates for this sub-tree were deletions for keys that don't exist, so do nothing.
                return reuseNode();
            }

            // The leaf needs to get split into a branch, so add it into our update set to get added further down (unless it itself was deleted).

            if (!deleteThisLeaf) {
                // emplace() ensures that we don't overwrite any updates to this leaf already in the UpdateSet.
                auto emplaceRes = updates.map.emplace(Hash::existingHash(node.leafKeyHash()),
                                                      node.nodeType == NodeType::Leaf
                                                          ? Update{std::string(node.leafVal()), false, false}
                                                          : Update{node.leafValHash(), false, true});

                // If we did insert it, and it went before the start of our iterator window, back up our iterator to include it.
                //   * This happens when the leaf we are splitting is to the left of the leaf we are adding.
                //   * It's not necessary to do this for the right side since the end iterator will always point *past* any right-side nodes.
                if (emplaceRes.second && emplaceRes.first->first < begin->first) begin = emplaceRes.first;
            }
        }


        // Split into left and right groups of keys

        auto middle = begin;
        while (middle != end && !middle->first.getBit(depth)) ++middle;


        // Recurse

        checkDepth(depth);

        PutNodeInfo leftNode = putAux(txn, depth+1, node.leftNodeId, updates, begin, middle, didBubble);
        PutNodeInfo rightNode = putAux(txn, depth+1, node.rightNodeId, updates, middle, end, didBubble);

        if (didBubble) {
            if (leftNode.nodeType == NodeType::Witness || rightNode.nodeType == NodeType::Witness) {
                // We don't know if one of the nodes is a branch or a leaf
                throw quaderr("can't bubble a witness node");
            } else if (leftNode.nodeType == NodeType::Empty && rightNode.nodeType == NodeType::Empty) {
                bubbleUp = true;
                return emptyNode();
            } else if ((leftNode.nodeType == NodeType::Leaf || leftNode.nodeType == NodeType::WitnessLeaf) && rightNode.nodeType == NodeType::Empty) {
                ParsedNode n(txn, dbi_node, leftNode.nodeId);
                bubbleUp = true;
                if (n.nodeType == NodeType::Leaf) return putLeaf(Hash::existingHash(n.leafKeyHash()), n.leafVal(), depth, false);
                else return putLeaf(Hash::existingHash(n.leafKeyHash()), n.leafValHash(), depth, true);
            } else if (leftNode.nodeType == NodeType::Empty && (rightNode.nodeType == NodeType::Leaf || rightNode.nodeType == NodeType::WitnessLeaf)) {
                ParsedNode n(txn, dbi_node, rightNode.nodeId);
                bubbleUp = true;
                if (n.nodeType == NodeType::Leaf) return putLeaf(Hash::existingHash(n.leafKeyHash()), n.leafVal(), depth, false);
                else return putLeaf(Hash::existingHash(n.leafKeyHash()), n.leafValHash(), depth, true);
            }

            // One of the nodes is a branch, or both are leaves, so bubbling can stop
        }

        // FIXME: BranchBoth is not necessarily correct here, just hacking it in since all we care about is whether it's some kind of branch
        return putNode(NodeType::BranchBoth, buildNodeBranch(leftNode.nodeId, rightNode.nodeId, leftNode.nodeHash, rightNode.nodeHash));
    }




    using GetMultiInternalMap = std::map<Hash, GetMultiResult &>;

    void getMulti(lmdb::txn &txn, GetMultiQuery &queryMap) {
        GetMultiInternalMap map;

        for (auto &[key, res] : queryMap) {
            map.emplace(Hash::hash(key), res);
        }

        uint64_t depth = 0;
        auto nodeId = getHeadNodeId(txn);

        getMultiAux(txn, depth, nodeId, map.begin(), map.end());
    }

    void getMultiAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, GetMultiInternalMap::iterator begin, GetMultiInternalMap::iterator end) {
        if (begin == end) {
            return;
        }

        ParsedNode node(txn, dbi_node, nodeId);

        if (node.nodeType == NodeType::Empty) {
            for (auto i = begin; i != end; ++i) {
                i->second.exists = false;
            }
        } else if (node.nodeType == NodeType::Leaf || node.nodeType == NodeType::WitnessLeaf) {
            for (auto i = begin; i != end; ++i) {
                if (i->first == node.leafKeyHash()) {
                    if (node.nodeType == NodeType::WitnessLeaf) throw quaderr("encountered witness node: incomplete tree");
                    i->second.exists = true;
                    i->second.val = node.leafVal();
                } else {
                    i->second.exists = false;
                }
            }
        } else if (node.isBranch()) {
            auto middle = begin;
            while (middle != end && !middle->first.getBit(depth)) ++middle;

            checkDepth(depth);

            getMultiAux(txn, depth+1, node.leftNodeId, begin, middle);
            getMultiAux(txn, depth+1, node.rightNodeId, middle, end);
        } else if (node.isWitness()) {
            throw quaderr("encountered witness node: incomplete tree");
        } else {
            throw quaderr("unrecognized nodeType: ", int(node.nodeType));
        }
    }

    bool get(lmdb::txn &txn, const std::string_view key, std::string_view &val) {
        GetMultiQuery query;

        auto rec = query.emplace(key, GetMultiResult{});

        getMulti(txn, query);

        if (rec.first->second.exists) {
            val = rec.first->second.val;
            return true;
        }

        return false;
    }





    struct ProofGenItem {
        uint64_t nodeId;
        uint64_t parentNodeId;
        ProofElem elem;
    };

    using ProofGenItems = std::vector<ProofGenItem>;
    using ProofHashes = std::map<Hash, std::string>; // keyHash -> key
    using ProofReverseNodeMap = std::map<uint64_t, uint64_t>; // child -> parent

    Proof generateProof(lmdb::txn &txn, const std::set<std::string> &keys) {
        ProofHashes keyHashes;

        for (auto &key : keys) {
            keyHashes.emplace(Hash::hash(key), key);
        }

        auto headNodeId = getHeadNodeId(txn);

        ProofGenItems items;
        ProofReverseNodeMap reverseMap;

        generateProofAux(txn, 0, headNodeId, 0, keyHashes.begin(), keyHashes.end(), items, reverseMap);

        Proof output;

        output.cmds = generateProofCmds(txn, items, reverseMap, headNodeId);

        for (auto &item : items) {
            output.elems.emplace_back(std::move(item.elem));
        }

        return output;
    }

    void generateProofAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, ProofHashes::iterator begin, ProofHashes::iterator end, ProofGenItems &items, ProofReverseNodeMap &reverseMap) {
        if (begin == end) {
            return;
        }

        ParsedNode node(txn, dbi_node, nodeId);

        if (node.nodeType == NodeType::Empty) {
            Hash h = begin->first;
            h.keepPrefixBits(depth);

            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofElem{ ProofElem::Type::WitnessEmpty, depth, h.str(), },
            });
        } else if (node.nodeType == NodeType::Leaf || node.nodeType == NodeType::WitnessLeaf) {
            if (std::any_of(begin, end, [&](auto &i){ return i.first == node.leafKeyHash(); })) {
                if (node.nodeType == NodeType::WitnessLeaf) {
                    throw quaderr("incomplete tree, missing leaf to make proof");
                }

                items.emplace_back(ProofGenItem{
                    nodeId,
                    parentNodeId,
                    ProofElem{ ProofElem::Type::Leaf, depth, std::string(node.leafKeyHash()), std::string(node.leafVal()), },
                });
            } else {
                items.emplace_back(ProofGenItem{
                    nodeId,
                    parentNodeId,
                    ProofElem{ ProofElem::Type::WitnessLeaf, depth, std::string(node.leafKeyHash()), node.leafValHash(), },
                });
            }
        } else if (node.isBranch()) {
            auto middle = begin;
            while (middle != end && !middle->first.getBit(depth)) ++middle;

            checkDepth(depth);

            if (node.leftNodeId) reverseMap.emplace(node.leftNodeId, nodeId);
            if (node.rightNodeId) reverseMap.emplace(node.rightNodeId, nodeId);

            // If one side is empty and the other side has elements to prove, don't go down the empty side.
            // This avoids unnecessary empty witnesses, since they will be satisfied with HashEmpty cmds from the other side.

            if (node.leftNodeId || middle == end) generateProofAux(txn, depth+1, node.leftNodeId, nodeId, begin, middle, items, reverseMap);
            if (node.rightNodeId || begin == middle) generateProofAux(txn, depth+1, node.rightNodeId, nodeId, middle, end, items, reverseMap);
        } else if (node.nodeType == NodeType::Witness) {
            throw quaderr("encountered witness node: incomplete tree");
        } else {
            throw quaderr("unrecognized nodeType: ", int(node.nodeType));
        }
    }

    struct GenProofItemAccum {
        uint64_t depth;
        uint64_t nodeId;
        ssize_t next;
    };

    std::vector<ProofCmd> generateProofCmds(lmdb::txn &txn, ProofGenItems &items, ProofReverseNodeMap &reverseMap, uint64_t headNodeId) {
        std::vector<ProofCmd> proofCmds;
        if (items.size() == 0) return proofCmds;

        std::vector<GenProofItemAccum> accums;
        uint64_t maxDepth = 0;

        for (size_t i = 0; i < items.size(); i++) {
            auto &item = items[i];
            maxDepth = std::max(maxDepth, item.elem.depth);
            accums.emplace_back(GenProofItemAccum{ item.elem.depth, item.nodeId, static_cast<ssize_t>(i+1), });
        }

        accums.back().next = -1;
        uint64_t currDepth = maxDepth;

        // Complexity: O(N*D) = O(N*log(N))

        for (; currDepth > 0; currDepth--) {
            for (ssize_t i = 0; i != -1; i = accums[i].next) {
                auto &curr = accums[i];
                if (curr.depth != currDepth) continue;

                auto currParent = curr.nodeId ? reverseMap[curr.nodeId] : items[i].parentNodeId;

                if (curr.next != -1) {
                    auto &next = accums[curr.next];

                    auto nextParent = next.nodeId ? reverseMap[next.nodeId] : items[curr.next].parentNodeId;

                    if (currParent == nextParent) {
                        proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::Merge, static_cast<uint64_t>(i), Hash::nullHash(), });
                        curr.next = next.next;
                        curr.nodeId = currParent;
                        curr.depth--;
                        continue;
                    }
                }

                ParsedNode parentNode(txn, dbi_node, currParent);
                uint64_t siblingNodeId = parentNode.leftNodeId == curr.nodeId ? parentNode.rightNodeId : parentNode.leftNodeId;

                if (siblingNodeId) {
                    ParsedNode siblingNode(txn, dbi_node, siblingNodeId);
                    proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashProvided, static_cast<uint64_t>(i), Hash::existingHash(siblingNode.nodeHash()), });
                } else {
                    proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashEmpty, static_cast<uint64_t>(i), Hash::nullHash(), });
                }

                curr.nodeId = currParent;
                curr.depth--;
            }
        }

        assert(accums[0].depth == 0);
        assert(accums[0].nodeId == headNodeId);
        assert(accums[0].next == -1);

        return proofCmds;
    }



    struct ImportProofItemAccum {
        uint64_t depth;
        uint64_t nodeId;
        ssize_t next;
        Hash keyHash;
        Hash nodeHash;

        bool merged = false;
    };

    void importProof(lmdb::txn &txn, Proof &proof, std::string expectedRoot) {
        auto rootNode = importProof(txn, proof);

        if (rootNode.nodeHash != expectedRoot) throw quaderr("proof invalid");

        setHeadNodeId(txn, rootNode.nodeId);
    }

    PutNodeInfo importProof(lmdb::txn &txn, Proof &proof) {
        auto putNode = [&](NodeType nodeType, std::string_view nodeRaw){
            if (nodeRaw.size() < 40) throw quaderr("nodeRaw too short");
            uint64_t newNodeId = getNextIntegerKey(txn, dbi_node);
            dbi_node.put(txn, lmdb::to_sv<uint64_t>(newNodeId), nodeRaw);
            return PutNodeInfo{newNodeId, Hash::existingHash(nodeRaw.substr(8, 32)), nodeType};
        };

        if (getHeadNodeId(txn)) throw quaderr("can't importProof into existing head");

        std::vector<ImportProofItemAccum> accums;

        for (size_t i = 0; i < proof.elems.size(); i++) {
            auto &elem = proof.elems[i];
            auto keyHash = Hash::existingHash(elem.keyHash);
            auto next = static_cast<ssize_t>(i+1);

            if (elem.proofType == ProofElem::Type::Leaf) {
                auto info = putNode(NodeType::Leaf, buildNodeLeaf(keyHash, elem.val, elem.depth));
                accums.emplace_back(ImportProofItemAccum{ elem.depth, info.nodeId, next, keyHash, info.nodeHash, });
            } else if (elem.proofType == ProofElem::Type::WitnessLeaf) {
                auto info = putNode(NodeType::WitnessLeaf, buildNodeWitnessLeaf(keyHash, Hash::existingHash(elem.val), elem.depth));
                accums.emplace_back(ImportProofItemAccum{ elem.depth, info.nodeId, next, keyHash, info.nodeHash, });
            } else if (elem.proofType == ProofElem::Type::WitnessEmpty) {
                accums.emplace_back(ImportProofItemAccum{ elem.depth, 0, next, keyHash, Hash::nullHash(), });
            } else {
                throw quaderr("unrecognized ProofItem type: ", int(elem.proofType));
            }
        }

        accums.back().next = -1;


        for (auto &cmd : proof.cmds) {
            if (cmd.nodeOffset >= proof.elems.size()) throw quaderr("nodeOffset in cmd is out of range");
            auto &accum = accums[cmd.nodeOffset];

            if (accum.merged) throw quaderr("element already merged");
            if (accum.depth == 0) throw quaderr("node depth underflow");

            PutNodeInfo siblingInfo;

            if (cmd.op == ProofCmd::Op::HashProvided) {
                siblingInfo = putNode(NodeType::Witness, buildNodeWitness(cmd.h));
            } else if (cmd.op == ProofCmd::Op::HashEmpty) {
                siblingInfo = PutNodeInfo{ 0, Hash::nullHash(), NodeType::Empty, };
            } else if (cmd.op == ProofCmd::Op::Merge) {
                if (accum.next < 0) throw quaderr("no nodes left to merge with");
                auto &accumNext = accums[accum.next];

                if (accum.depth != accumNext.depth) throw quaderr("merge depth mismatch");

                accum.next = accumNext.next;
                accumNext.merged = true;

                siblingInfo = PutNodeInfo{ accumNext.nodeId, accumNext.nodeHash, NodeType::Empty, }; // FIXME: nodeType doesn't matter here, Empty is placeholder
            } else {
                throw quaderr("unrecognized ProofCmd op: ", int(cmd.op));
            }

            std::string branch;
            if (cmd.op == ProofCmd::Op::Merge || !accum.keyHash.getBit(accum.depth - 1)) {
                branch = buildNodeBranch(accum.nodeId, siblingInfo.nodeId, accum.nodeHash, siblingInfo.nodeHash);
            } else {
                branch = buildNodeBranch(siblingInfo.nodeId, accum.nodeId, siblingInfo.nodeHash, accum.nodeHash);
            }

            // FIXME: BranchBoth is not necessarily correct here
            auto branchInfo = putNode(NodeType::BranchBoth, branch);

            accum.depth--;
            accum.nodeId = branchInfo.nodeId;
            accum.nodeHash = branchInfo.nodeHash;
        }

        if (accums[0].next != -1) throw quaderr("not all proof elems were merged");
        if (accums[0].depth != 0) throw quaderr("proof didn't reach root");

        // FIXME: BranchBoth is not necessarily correct here
        return PutNodeInfo{accums[0].nodeId, accums[0].nodeHash, NodeType::BranchBoth};
    }




    struct Stats {
        uint64_t numNodes = 0;
        uint64_t numLeafNodes = 0;
        uint64_t numBranchNodes = 0;
        uint64_t numWitnessNodes = 0;

        uint64_t maxDepth = 0;
        uint64_t numBytes = 0;
    };


    Stats stats(lmdb::txn &txn) {
        Stats output;

        auto nodeId = getHeadNodeId(txn);

        statsAux(txn, nodeId, 0, output);

        return output;
    }

    void statsAux(lmdb::txn &txn, uint64_t nodeId, uint64_t depth, Stats &output) {
        ParsedNode node(txn, dbi_node, nodeId);

        if (node.nodeType == NodeType::Empty) return;

        output.numNodes++;
        output.maxDepth = std::max(output.maxDepth, depth);
        output.numBytes += node.raw.size();

        if (node.nodeType == NodeType::Leaf) {
            output.numLeafNodes++;
        } else if (node.isBranch()) {
            output.numBranchNodes++;

            checkDepth(depth);

            statsAux(txn, node.leftNodeId, depth+1, output);
            statsAux(txn, node.rightNodeId, depth+1, output);
        } else if (node.isWitness()) {
            output.numWitnessNodes++;
        } else {
            throw quaderr("unrecognized nodeType: ", int(node.nodeType));
        }
    }






  private:

    uint64_t getIntegerKeyOrZero(lmdb::cursor &cursor, MDB_cursor_op cursorOp = MDB_LAST) {
        uint64_t id;

        std::string_view k, v;

        if (cursor.get(k, v, cursorOp)) {
            id = lmdb::from_sv<uint64_t>(k);
        } else {
            id = 0;
        }

        return id;
    }

    uint64_t getLargestIntegerKeyOrZero(lmdb::txn &txn, lmdb::dbi &dbi) {
        auto cursor = lmdb::cursor::open(txn, dbi);
        return getIntegerKeyOrZero(cursor, MDB_LAST);
    }

    uint64_t getNextIntegerKey(lmdb::txn &txn, lmdb::dbi &dbi) {
        return getLargestIntegerKeyOrZero(txn, dbi) + 1;
    }




  public:

    lmdb::dbi dbi_head;
    lmdb::dbi dbi_node;

  private:

    std::string head = "master";
    bool detachedHead = false;
    uint64_t detachedHeadNodeId = 0;
};





static inline bool verifyProofBasic(std::string_view root, std::string_view key, std::string_view val, std::vector<std::string> &proof) {
    if (proof.size() > 200) throw quaderr("proof size unreasonably large: ", proof.size());

    Hash keyHash = Hash::hash(key);
    Hash nodeHash;

    Keccak k;

    {
        Hash valHash = Hash::hash(val);
        unsigned char depthChar = static_cast<unsigned char>(proof.size());

        k.add(keyHash.sv());
        k.add(&depthChar, 1);
        k.add(valHash.sv());

        k.getHash(nodeHash.data);
    }

    for (int depth = static_cast<int>(proof.size()) - 1; depth >= 0; depth--) {
        k.reset();

        if (keyHash.getBit(depth)) {
            k.add(proof[depth]);
            k.add(nodeHash.sv());
        } else {
            k.add(nodeHash.sv());
            k.add(proof[depth]);
        }

        k.getHash(nodeHash.data);
    }

    return nodeHash == root;
}




}
