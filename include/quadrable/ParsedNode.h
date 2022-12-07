#pragma once

namespace quadrable {


// For internal DB-use only

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

    bool isEmpty() const { return nodeType == NodeType::Empty; }
    bool isLeaf() const { return nodeType == NodeType::Leaf || nodeType == NodeType::WitnessLeaf; }
    bool isBranch() const { return nodeType == NodeType::BranchLeft || nodeType == NodeType::BranchRight || nodeType == NodeType::BranchBoth; }
    bool isWitness() const { return nodeType == NodeType::Witness; }
    bool isWitnessLeaf() const { return nodeType == NodeType::WitnessLeaf; }
    bool isWitnessAny() const { return nodeType == NodeType::Witness || nodeType == NodeType::WitnessLeaf; }

    std::string_view nodeHash() const {
        static const char nullBytes[32] = {};
        if (isEmpty()) return std::string_view{nullBytes, 32};
        return raw.substr(8, 32);
    }

    std::string_view leafKeyHash() const {
        if (!isLeaf()) throw quaderr("node is not a Leaf/WitnessLeaf");
        return raw.substr(8 + 32, 32);
    }

    Key key() const {
        return Key::existing(leafKeyHash());
    }

    std::string_view leafVal() const {
        if (nodeType != NodeType::Leaf) throw quaderr("node is not a Leaf");
        return raw.substr(8 + 32 + 32);
    }

    std::string leafValHash() const {
        if (nodeType == NodeType::Leaf) {
            return Key::hash(leafVal()).str();
        } else if (nodeType == NodeType::WitnessLeaf) {
            return std::string(raw.substr(8 + 32 + 32));
        }

        throw quaderr("node is not a Leaf/WitnessLeaf");
    }
};


}
