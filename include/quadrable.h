#pragma once

#include <string.h>
#include <assert.h>

#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>
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
    std::string key; // only used if trackKeys is set
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
        Invalid = 0,
        Leaf = 1,
        WitnessLeaf = 2,
        WitnessEmpty = 3,
    } elemType;
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
    std::vector<ProofElem> elems;
    std::vector<ProofCmd> cmds;
};







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



// ParsedNode holds a string_view into the DB, so it shouldn't be held after modifying the DB/ending the transaction

class ParsedNode {
  public:

    NodeType nodeType;
    std::string_view raw;
    uint64_t leftNodeId = 0;
    uint64_t rightNodeId = 0;
    uint64_t nodeId;

    ParsedNode(lmdb::txn &txn, lmdb::dbi &dbi_node, uint64_t nodeId_) : nodeId(nodeId_) {
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
        if (trackKeys) dbi_key = lmdb::dbi::open(txn, "quadrable_key", MDB_CREATE | MDB_INTEGERKEY);
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

    uint64_t getHeadNodeId(lmdb::txn &txn, std::string_view otherHead) {
        uint64_t nodeId = 0;

        std::string_view headRaw;
        if (dbi_head.get(txn, otherHead, headRaw)) nodeId = lmdb::from_sv<uint64_t>(headRaw);

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
            if (keyRaw.size() == 0) throw quaderr("zero-length keys not allowed");
            map.insert_or_assign(Hash::hash(keyRaw), Update{std::string(db->trackKeys ? keyRaw : ""), std::string(val), false, false});
            return *this;
        }

        UpdateSet &del(std::string_view keyRaw) {
            if (keyRaw.size() == 0) throw quaderr("zero-length keys not allowed");
            map.insert_or_assign(Hash::hash(keyRaw), Update{std::string(keyRaw), "", true, false});
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

    class BuiltNode {
      friend class Quadrable;

      public:
        uint64_t nodeId;
        Hash nodeHash;
        NodeType nodeType;

        static BuiltNode empty() {
            return {0, Hash::nullHash(), NodeType::Empty};
        }

        static BuiltNode reuse(ParsedNode &node) {
            return {node.nodeId, Hash::existingHash(node.nodeHash()), node.nodeType};
        }

        static BuiltNode existing(uint64_t nodeId, const Hash &nodeHash, NodeType nodeType = NodeType::Invalid) {
            return {nodeId, nodeHash, nodeType};
        }

        static BuiltNode newLeaf(Quadrable *db, lmdb::txn &txn, const Hash &keyHash, std::string_view val, uint64_t depth, std::string_view leafKey = "") {
            BuiltNode output;

            {
                Hash valHash = Hash::hash(val);
                unsigned char depthChar = static_cast<unsigned char>(depth);

                Keccak k;

                k.add(&depthChar, 1);
                k.add(keyHash.sv());
                k.add(valHash.sv());

                k.getHash(output.nodeHash.data);
            }

            std::string nodeRaw;

            nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Leaf));
            nodeRaw += output.nodeHash.sv();
            nodeRaw += keyHash.sv();
            nodeRaw += val;

            output.nodeId = db->writeNodeToDb(txn, nodeRaw);
            output.nodeType = NodeType::Leaf;

            db->setLeafKey(txn, output.nodeId, leafKey);

            return output;
        }

        static BuiltNode newWitnessLeaf(Quadrable *db, lmdb::txn &txn, const Hash &keyHash, const Hash &valHash, uint64_t depth) {
            BuiltNode output;

            {
                unsigned char depthChar = static_cast<unsigned char>(depth);

                Keccak k;

                k.add(&depthChar, 1);
                k.add(keyHash.sv());
                k.add(valHash.sv());

                k.getHash(output.nodeHash.data);
            }

            std::string nodeRaw;

            nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::WitnessLeaf));
            nodeRaw += output.nodeHash.sv();
            nodeRaw += keyHash.sv();
            nodeRaw += valHash.sv();

            output.nodeId = db->writeNodeToDb(txn, nodeRaw);
            output.nodeType = NodeType::WitnessLeaf;

            return output;
        }

        static BuiltNode newBranch(Quadrable *db, lmdb::txn &txn, const BuiltNode &leftNode, const BuiltNode &rightNode) {
            BuiltNode output;

            {
                Keccak k;
                k.add(leftNode.nodeHash.data, sizeof(leftNode.nodeHash.data));
                k.add(rightNode.nodeHash.data, sizeof(rightNode.nodeHash.data));
                k.getHash(output.nodeHash.data);
            }

            std::string nodeRaw;

            uint64_t w1;

            if (rightNode.nodeId == 0) {
                output.nodeType = NodeType::BranchLeft;
                w1 = uint64_t(NodeType::BranchLeft) | leftNode.nodeId << 8;
            } else if (leftNode.nodeId == 0) {
                output.nodeType = NodeType::BranchRight;
                w1 = uint64_t(NodeType::BranchRight) | rightNode.nodeId << 8;
            } else {
                output.nodeType = NodeType::BranchBoth;
                w1 = uint64_t(NodeType::BranchBoth) | leftNode.nodeId << 8;
            }

            nodeRaw += lmdb::to_sv<uint64_t>(w1);
            nodeRaw += output.nodeHash.sv();

            if (rightNode.nodeId && leftNode.nodeId) {
                nodeRaw += lmdb::to_sv<uint64_t>(rightNode.nodeId);
            }

            output.nodeId = db->writeNodeToDb(txn, nodeRaw);

            return output;
        }

        static BuiltNode newWitness(Quadrable *db, lmdb::txn &txn, const Hash &hash) {
            std::string nodeRaw;

            nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Witness));
            nodeRaw += hash.sv();

            BuiltNode output;

            output.nodeId = db->writeNodeToDb(txn, nodeRaw);
            output.nodeHash = hash;
            output.nodeType = NodeType::BranchBoth;

            return output;
        }

        // Wrappers

        static BuiltNode newLeaf(Quadrable *db, lmdb::txn &txn, UpdateSetMap::iterator it, uint64_t depth) {
            if (it->second.witness) {
                return newWitnessLeaf(db, txn, it->first, Hash::existingHash(it->second.val), depth);
            } else {
                auto leaf = newLeaf(db, txn, it->first, it->second.val, depth, it->second.key);
                return leaf;
            }
        }

        static BuiltNode copyLeaf(Quadrable *db, lmdb::txn &txn, ParsedNode &node, uint64_t depth) {
            if (node.nodeType == NodeType::Leaf) {
                auto leaf = newLeaf(db, txn, Hash::existingHash(node.leafKeyHash()), node.leafVal(), depth);
                if (db->trackKeys) {
                    std::string_view leafKey;
                    if (db->getLeafKey(txn, node.nodeId, leafKey)) db->setLeafKey(txn, leaf.nodeId, leafKey);
                }
                return leaf;
            } else if (node.nodeType == NodeType::WitnessLeaf) {
                return newWitnessLeaf(db, txn, Hash::existingHash(node.leafKeyHash()), Hash::existingHash(node.leafValHash()), depth);
            } else {
                throw quaderr("tried to copyLeaf on a non-leaf");
            }
        }
    };


    void apply(lmdb::txn &txn, UpdateSet &updatesOrig) {
        // If exception is thrown, updatesOrig could be in inconsistent state, so ensure it's cleared
        UpdateSet updates = std::move(updatesOrig);

        uint64_t oldNodeId = getHeadNodeId(txn);

        bool bubbleUp = false;
        auto newNode = putAux(txn, 0, oldNodeId, updates, updates.map.begin(), updates.map.end(), bubbleUp);

        if (newNode.nodeId != oldNodeId) setHeadNodeId(txn, newNode.nodeId);
    }

    void put(lmdb::txn &txn, std::string_view key, std::string_view val) {
        change().put(key, val).apply(txn);
    }

    void del(lmdb::txn &txn, std::string_view key) {
        change().del(key).apply(txn);
    }

    Update makeUpdateFromNode(lmdb::txn &txn, ParsedNode &node) {
        if (node.nodeType == NodeType::Leaf) {
            std::string_view leafKey;
            if (trackKeys) getLeafKey(txn, node.nodeId, leafKey);
            return Update{std::string(leafKey), std::string(node.leafVal()), false, false};
        } else if (node.nodeType == NodeType::WitnessLeaf) {
            return Update{"", node.leafValHash(), false, true};
        } else {
            throw quaderr("can't convert leaf to Update");
        }
    }

    bool getLeafKey(lmdb::txn &txn, uint64_t nodeId, std::string_view &leafKey) {
        if (!trackKeys) return false;
        return dbi_key.get(txn, lmdb::to_sv<uint64_t>(nodeId), leafKey);
    }

    void setLeafKey(lmdb::txn &txn, uint64_t nodeId, std::string_view leafKey) {
        if (!trackKeys || leafKey.size() == 0) return;
        dbi_key.put(txn, lmdb::to_sv<uint64_t>(nodeId), leafKey);
    }

    BuiltNode putAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, UpdateSet &updates, UpdateSetMap::iterator begin, UpdateSetMap::iterator end, bool &bubbleUp) {
        ParsedNode node(txn, dbi_node, nodeId);
        bool didBubble = false;

        // recursion base cases

        if (begin == end) {
            return BuiltNode::reuse(node);
        }

        if (node.nodeType == NodeType::Witness) {
            throw quaderr("encountered witness during update: partial tree");
        } else if (node.nodeType == NodeType::Empty) {
            updates.eraseRange(begin, end, [&](UpdateSetMap::iterator &u){ return u->second.deletion; });

            if (begin == end) {
                // All updates for this sub-tree were deletions for keys that don't exist, so do nothing.
                return BuiltNode::reuse(node);
            }

            if (std::next(begin) == end) {
                return BuiltNode::newLeaf(this, txn, begin, depth);
            }
        } else if (node.isLeaf()) {
            if (std::next(begin) == end && begin->first == node.leafKeyHash()) {
                // Update an existing record

                if (begin->second.deletion) {
                    bubbleUp = true;
                    return BuiltNode::empty();
                }

                if (node.nodeType == NodeType::Leaf && begin->second.val == node.leafVal()) {
                    // No change to this leaf, so do nothing. Don't do this for WitnessLeaf nodes, since we need to upgrade them to leaves.
                    return BuiltNode::reuse(node);
                }

                return BuiltNode::newLeaf(this, txn, begin, depth);
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
                    return BuiltNode::empty();
                }
                // All updates for this sub-tree were deletions for keys that don't exist, so do nothing.
                return BuiltNode::reuse(node);
            }

            // The leaf needs to get split into a branch, so add it into our update set to get added further down (unless it itself was deleted).

            if (!deleteThisLeaf) {
                // emplace() ensures that we don't overwrite any updates to this leaf already in the UpdateSet.
                auto emplaceRes = updates.map.emplace(Hash::existingHash(node.leafKeyHash()), makeUpdateFromNode(txn, node));

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

        auto leftNode = putAux(txn, depth+1, node.leftNodeId, updates, begin, middle, didBubble);
        auto rightNode = putAux(txn, depth+1, node.rightNodeId, updates, middle, end, didBubble);

        if (didBubble) {
            if (leftNode.nodeType == NodeType::Witness || rightNode.nodeType == NodeType::Witness) {
                // We don't know if one of the nodes is a branch or a leaf
                throw quaderr("can't bubble a witness node");
            } else if (leftNode.nodeType == NodeType::Empty && rightNode.nodeType == NodeType::Empty) {
                bubbleUp = true;
                return BuiltNode::empty();
            } else if ((leftNode.nodeType == NodeType::Leaf || leftNode.nodeType == NodeType::WitnessLeaf) && rightNode.nodeType == NodeType::Empty) {
                ParsedNode n(txn, dbi_node, leftNode.nodeId);
                bubbleUp = true;
                return BuiltNode::copyLeaf(this, txn, n, depth);
            } else if (leftNode.nodeType == NodeType::Empty && (rightNode.nodeType == NodeType::Leaf || rightNode.nodeType == NodeType::WitnessLeaf)) {
                ParsedNode n(txn, dbi_node, rightNode.nodeId);
                bubbleUp = true;
                return BuiltNode::copyLeaf(this, txn, n, depth);
            }

            // One of the nodes is a branch, or both are leaves, so bubbling can stop
        }

        return BuiltNode::newBranch(this, txn, leftNode, rightNode);
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

    GetMultiQuery get(lmdb::txn &txn, std::set<std::string> keys) {
        GetMultiQuery query;

        for (auto &key : keys) {
            query.emplace(key, GetMultiResult{});
        }

        getMulti(txn, query);

        return query;
    }





    struct ProofGenItem {
        uint64_t nodeId;
        uint64_t parentNodeId;
        ProofElem elem;
    };

    using ProofGenItems = std::vector<ProofGenItem>;
    using ProofHashes = std::map<Hash, std::string>; // keyHash -> key
    using ProofReverseNodeMap = std::map<uint64_t, uint64_t>; // child -> parent

    Proof exportProof(lmdb::txn &txn, const std::set<std::string> &keys) {
        ProofHashes keyHashes;

        for (auto &key : keys) {
            keyHashes.emplace(Hash::hash(key), key);
        }

        auto headNodeId = getHeadNodeId(txn);

        ProofGenItems items;
        ProofReverseNodeMap reverseMap;

        exportProofAux(txn, 0, headNodeId, 0, keyHashes.begin(), keyHashes.end(), items, reverseMap);

        Proof output;

        output.cmds = exportProofCmds(txn, items, reverseMap, headNodeId);

        for (auto &item : items) {
            output.elems.emplace_back(std::move(item.elem));
        }

        return output;
    }

    void exportProofAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, ProofHashes::iterator begin, ProofHashes::iterator end, ProofGenItems &items, ProofReverseNodeMap &reverseMap) {
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
        } else if (node.isLeaf()) {
            if (std::any_of(begin, end, [&](auto &i){ return i.first == node.leafKeyHash(); })) {
                if (node.nodeType == NodeType::WitnessLeaf) {
                    throw quaderr("incomplete tree, missing leaf to make proof");
                }

                std::string_view leafKey;
                getLeafKey(txn, node.nodeId, leafKey);

                items.emplace_back(ProofGenItem{
                    nodeId,
                    parentNodeId,
                    ProofElem{ ProofElem::Type::Leaf, depth, std::string(node.leafKeyHash()), std::string(node.leafVal()), std::string(leafKey) },
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

            if (node.leftNodeId || middle == end) exportProofAux(txn, depth+1, node.leftNodeId, nodeId, begin, middle, items, reverseMap);
            if (node.rightNodeId || begin == middle) exportProofAux(txn, depth+1, node.rightNodeId, nodeId, middle, end, items, reverseMap);
        } else if (node.nodeType == NodeType::Witness) {
            throw quaderr("encountered witness node: incomplete tree");
        } else {
            throw quaderr("unrecognized nodeType: ", int(node.nodeType));
        }
    }

    struct GenProofItemAccum {
        size_t index;
        uint64_t depth;
        uint64_t nodeId;
        ssize_t next;

        uint64_t mergedOrder = 0;
        std::vector <ProofCmd> proofCmds;
    };

    std::vector<ProofCmd> exportProofCmds(lmdb::txn &txn, ProofGenItems &items, ProofReverseNodeMap &reverseMap, uint64_t headNodeId) {
        if (items.size() == 0) return {};

        std::vector<GenProofItemAccum> accums;
        uint64_t maxDepth = 0;

        for (size_t i = 0; i < items.size(); i++) {
            auto &item = items[i];
            maxDepth = std::max(maxDepth, item.elem.depth);
            accums.emplace_back(GenProofItemAccum{ i, item.elem.depth, item.nodeId, static_cast<ssize_t>(i+1), });
        }

        accums.back().next = -1;
        uint64_t currDepth = maxDepth;
        uint64_t currMergeOrder = 0;

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
                        curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::Merge, static_cast<uint64_t>(i), });
                        next.mergedOrder = currMergeOrder++;
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
                    curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashProvided, static_cast<uint64_t>(i), std::string(siblingNode.nodeHash()), });
                } else {
                    curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashEmpty, static_cast<uint64_t>(i), });
                }

                curr.nodeId = currParent;
                curr.depth--;
            }
        }

        assert(accums[0].depth == 0);
        assert(accums[0].nodeId == headNodeId);
        assert(accums[0].next == -1);
        accums[0].mergedOrder = currMergeOrder;

        std::sort(accums.begin(), accums.end(), [](auto &a, auto &b){ return a.mergedOrder < b.mergedOrder; });

        std::vector<ProofCmd> proofCmds;
        for (auto &a : accums) {
            proofCmds.insert(proofCmds.end(), a.proofCmds.begin(), a.proofCmds.end());
        }

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

    BuiltNode importProof(lmdb::txn &txn, Proof &proof, std::string expectedRoot = "") {
        if (getHeadNodeId(txn)) throw quaderr("can't importProof into non-empty head");

        auto rootNode = importProofInternal(txn, proof);

        if (expectedRoot.size() && rootNode.nodeHash != expectedRoot) throw quaderr("proof invalid");

        setHeadNodeId(txn, rootNode.nodeId);

        return rootNode;
    }

    BuiltNode mergeProof(lmdb::txn &txn, Proof &proof) {
        auto rootNode = importProofInternal(txn, proof);

        if (rootNode.nodeHash != root(txn)) throw quaderr("different roots, unable to merge proofs");

        auto updatedRoot = mergeProofInternal(txn, getHeadNodeId(txn), rootNode.nodeId);

        setHeadNodeId(txn, updatedRoot.nodeId);

        return rootNode;
    }

    BuiltNode importProofInternal(lmdb::txn &txn, Proof &proof) {
        std::vector<ImportProofItemAccum> accums;

        for (size_t i = 0; i < proof.elems.size(); i++) {
            auto &elem = proof.elems[i];
            auto keyHash = Hash::existingHash(elem.keyHash);
            auto next = static_cast<ssize_t>(i+1);

            if (elem.elemType == ProofElem::Type::Leaf) {
                auto info = BuiltNode::newLeaf(this, txn, keyHash, elem.val, elem.depth, elem.key);
                accums.emplace_back(ImportProofItemAccum{ elem.depth, info.nodeId, next, keyHash, info.nodeHash, });
            } else if (elem.elemType == ProofElem::Type::WitnessLeaf) {
                auto info = BuiltNode::newWitnessLeaf(this, txn, keyHash, Hash::existingHash(elem.val), elem.depth);
                accums.emplace_back(ImportProofItemAccum{ elem.depth, info.nodeId, next, keyHash, info.nodeHash, });
            } else if (elem.elemType == ProofElem::Type::WitnessEmpty) {
                accums.emplace_back(ImportProofItemAccum{ elem.depth, 0, next, keyHash, Hash::nullHash(), });
            } else {
                throw quaderr("unrecognized ProofItem type: ", int(elem.elemType));
            }
        }

        accums.back().next = -1;


        for (auto &cmd : proof.cmds) {
            if (cmd.nodeOffset >= proof.elems.size()) throw quaderr("nodeOffset in cmd is out of range");
            auto &accum = accums[cmd.nodeOffset];

            if (accum.merged) throw quaderr("element already merged");
            if (accum.depth == 0) throw quaderr("node depth underflow");

            BuiltNode siblingInfo;

            if (cmd.op == ProofCmd::Op::HashProvided) {
                siblingInfo = BuiltNode::newWitness(this, txn, Hash::existingHash(cmd.hash));
            } else if (cmd.op == ProofCmd::Op::HashEmpty) {
                siblingInfo = BuiltNode::empty();
            } else if (cmd.op == ProofCmd::Op::Merge) {
                if (accum.next < 0) throw quaderr("no nodes left to merge with");
                auto &accumNext = accums[accum.next];

                if (accum.depth != accumNext.depth) throw quaderr("merge depth mismatch");

                accum.next = accumNext.next;
                accumNext.merged = true;

                siblingInfo = BuiltNode::existing(accumNext.nodeId, accumNext.nodeHash);
            } else {
                throw quaderr("unrecognized ProofCmd op: ", int(cmd.op));
            }

            BuiltNode branchInfo;

            if (cmd.op == ProofCmd::Op::Merge || !accum.keyHash.getBit(accum.depth - 1)) {
                branchInfo = BuiltNode::newBranch(this, txn, BuiltNode::existing(accum.nodeId, accum.nodeHash), BuiltNode::existing(siblingInfo.nodeId, siblingInfo.nodeHash));
            } else {
                branchInfo = BuiltNode::newBranch(this, txn, BuiltNode::existing(siblingInfo.nodeId, siblingInfo.nodeHash), BuiltNode::existing(accum.nodeId, accum.nodeHash));
            }

            accum.depth--;
            accum.nodeId = branchInfo.nodeId;
            accum.nodeHash = branchInfo.nodeHash;
        }

        if (accums[0].next != -1) throw quaderr("not all proof elems were merged");
        if (accums[0].depth != 0) throw quaderr("proof didn't reach root");

        return BuiltNode::existing(accums[0].nodeId, accums[0].nodeHash);
    }

    BuiltNode mergeProofInternal(lmdb::txn &txn, uint64_t origNodeId, uint64_t newNodeId) {
        ParsedNode origNode(txn, dbi_node, origNodeId);
        ParsedNode newNode(txn, dbi_node, newNodeId);

        if ((origNode.isWitness() && !newNode.isWitness()) ||
            (origNode.nodeType == NodeType::Witness && newNode.nodeType == NodeType::WitnessLeaf)) {

            // FIXME: if keys are available on one of them
            return BuiltNode::reuse(newNode);
        } else if (origNode.isBranch() && newNode.isBranch()) {
            auto newLeftNode = mergeProofInternal(txn, origNode.leftNodeId, newNode.leftNodeId);
            auto newRightNode = mergeProofInternal(txn, origNode.rightNodeId, newNode.rightNodeId);

            if (origNode.leftNodeId == newLeftNode.nodeId && origNode.rightNodeId == newRightNode.nodeId) {
                return BuiltNode::reuse(origNode);
            } else if (newNode.leftNodeId == newLeftNode.nodeId && newNode.rightNodeId == newRightNode.nodeId) {
                return BuiltNode::reuse(newNode);
            } else {
                return BuiltNode::newBranch(this, txn, newLeftNode, newRightNode);
            }
        } else {
            return BuiltNode::reuse(origNode);
        }
    }







    void walkTree(lmdb::txn &txn, std::function<bool(ParsedNode &, uint64_t)> cb) {
        auto nodeId = getHeadNodeId(txn);
        walkTreeAux(txn, cb, nodeId, 0);
    }

    void walkTree(lmdb::txn &txn, uint64_t nodeId, std::function<bool(ParsedNode &, uint64_t)> cb) {
        walkTreeAux(txn, cb, nodeId, 0);
    }

    void walkTreeAux(lmdb::txn &txn, std::function<bool(ParsedNode &, uint64_t)> cb, uint64_t nodeId, uint64_t depth) {
        ParsedNode node(txn, dbi_node, nodeId);

        if (node.nodeType == NodeType::Empty) return;

        if (!cb(node, depth)) return;

        if (node.isBranch()) {
            checkDepth(depth);

            walkTreeAux(txn, cb, node.leftNodeId, depth+1);
            walkTreeAux(txn, cb, node.rightNodeId, depth+1);
        }
    }



    class GarbageCollector {
      friend class Quadrable;

      public:
        GarbageCollector(Quadrable &db_) : db(db_) {}

        void markAllHeads(lmdb::txn &txn) {
            std::string_view k, v;
            auto cursor = lmdb::cursor::open(txn, db.dbi_head);
            for (bool found = cursor.get(k, v, MDB_FIRST); found; found = cursor.get(k, v, MDB_NEXT)) {
                markTree(txn, lmdb::from_sv<uint64_t>(v));
            }
        }

        void markTree(lmdb::txn &txn, uint64_t rootNodeId) {
            db.walkTree(txn, rootNodeId, [&](quadrable::ParsedNode &node, uint64_t){
                if (markedNodes.find(node.nodeId) != markedNodes.end()) return false;
                markedNodes.emplace(node.nodeId);
                return true;
            });
        }

        struct Stats {
            uint64_t total = 0;
            uint64_t collected = 0;
        };

        Stats sweep(lmdb::txn &txn) {
            Stats stats;

            std::string_view k, v;
            auto cursor = lmdb::cursor::open(txn, db.dbi_node);
            for (bool found = cursor.get(k, v, MDB_FIRST); found; found = cursor.get(k, v, MDB_NEXT)) {
                stats.total++;
                uint64_t nodeId = lmdb::from_sv<uint64_t>(k);
                if (markedNodes.find(nodeId) == markedNodes.end()) {
                    cursor.del();
                    db.dbi_key.del(txn, lmdb::to_sv<uint64_t>(nodeId));
                    stats.collected++;
                }
            }

            return stats;
        }

      private:
        Quadrable &db;
        std::unordered_set<uint64_t> markedNodes;
    };





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

        walkTree(txn, [&](ParsedNode &node, uint64_t depth){
            output.numNodes++;
            output.maxDepth = std::max(output.maxDepth, depth);
            output.numBytes += node.raw.size();

            if (node.nodeType == NodeType::Leaf) {
                output.numLeafNodes++;
            } else if (node.isBranch()) {
                output.numBranchNodes++;
            } else if (node.isWitness()) {
                output.numWitnessNodes++;
            }

            return true;
        });

        return output;
    }




    struct Diff {
        std::string keyHash;
        std::string key;
        std::string val; // new value if insertion, old value if deletion
        bool deletion = false;
    };

    std::vector<Diff> diff(lmdb::txn &txn, uint64_t nodeIdA, uint64_t nodeIdB) {
        std::vector<Diff> output;

        diffAux(txn, nodeIdA, nodeIdB, output);

        return output;
    }

    void diffPush(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output, bool deletion) {
        std::string_view key;
        getLeafKey(txn, node.nodeId, key);

        output.emplace_back(Diff{
            std::string(node.leafKeyHash()),
            std::string(key),
            std::string(node.leafVal()),
            deletion,
        });
    }

    void diffPushAdd(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output) {
        diffPush(txn, node, output, false);
    }

    void diffPushDel(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output) {
        diffPush(txn, node, output, true);
    }

    void diffWalk(lmdb::txn &txn, uint64_t nodeId, std::function<void(ParsedNode &)> cb) {
        walkTree(txn, nodeId, [&](ParsedNode &node, uint64_t depth){
            if (node.isWitness()) throw quaderr("encountered witness during diffWalk");
            if (node.isLeaf()) cb(node);
            return true;
        });
    }

    void diffAux(lmdb::txn &txn, uint64_t nodeIdA, uint64_t nodeIdB, std::vector<Diff> &output) {
        if (nodeIdA == nodeIdB) return;

        ParsedNode nodeA(txn, dbi_node, nodeIdA);
        ParsedNode nodeB(txn, dbi_node, nodeIdB);

        if (nodeA.isWitness() || nodeB.isWitness()) throw quaderr("encountered witness during diff");

        if (nodeA.isBranch() && nodeB.isBranch()) {
            diffAux(txn, nodeA.leftNodeId, nodeB.leftNodeId, output);
            diffAux(txn, nodeA.rightNodeId, nodeB.rightNodeId, output);
        } else if (!nodeA.isBranch() && nodeB.isBranch()) {
            // All keys in B were added (except maybe if A is a leaf)
            bool foundLeaf = false;
            diffWalk(txn, nodeIdB, [&](ParsedNode &node){
                if (nodeA.isLeaf() && node.leafKeyHash() == nodeA.leafKeyHash()) {
                    foundLeaf = true;
                    if (node.leafVal() != nodeA.leafVal()) {
                        diffPushDel(txn, nodeA, output);
                        diffPushAdd(txn, node, output);
                    }
                } else {
                    diffPushAdd(txn, node, output);
                }
            });
            if (nodeA.isLeaf() && !foundLeaf) diffPushDel(txn, nodeA, output);
        } else if (nodeA.isBranch() && !nodeB.isBranch()) {
            // All keys in A were deleted (except maybe if B is a leaf)
            bool foundLeaf = false;
            diffWalk(txn, nodeIdA, [&](ParsedNode &node){
                if (nodeB.isLeaf() && node.leafKeyHash() == nodeB.leafKeyHash()) {
                    foundLeaf = true;
                    if (node.leafVal() != nodeB.leafVal()) {
                        diffPushDel(txn, nodeB, output);
                        diffPushAdd(txn, node, output);
                    }
                } else {
                    diffPushDel(txn, node, output);
                }
            });
            if (nodeB.isLeaf() && !foundLeaf) diffPushAdd(txn, nodeB, output);
        } else if (nodeA.isLeaf() && nodeB.isLeaf()) {
            if (nodeA.leafKeyHash() != nodeB.leafKeyHash() || nodeA.leafVal() != nodeB.leafVal()) {
                diffPushDel(txn, nodeA, output);
                diffPushAdd(txn, nodeB, output);
            }
        } else if (nodeA.isLeaf()) {
            diffPushDel(txn, nodeA, output);
        } else if (nodeB.isLeaf()) {
            diffPushAdd(txn, nodeB, output);
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

    uint64_t writeNodeToDb(lmdb::txn &txn, std::string_view nodeRaw) {
        assert(nodeRaw.size() >= 40);
        uint64_t newNodeId = getNextIntegerKey(txn, dbi_node);
        dbi_node.put(txn, lmdb::to_sv<uint64_t>(newNodeId), nodeRaw);
        return newNodeId;
    }




  public:

    lmdb::dbi dbi_head;
    lmdb::dbi dbi_node;
    lmdb::dbi dbi_key;

    bool trackKeys = false;

  private:

    std::string head = "master";
    bool detachedHead = false;
    uint64_t detachedHeadNodeId = 0;
};



}
