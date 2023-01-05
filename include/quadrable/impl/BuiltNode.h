public:

class BuiltNode {
  friend class Quadrable;

  public:
    uint64_t nodeId;
    Key nodeHash;
    NodeType nodeType;

    static BuiltNode empty() {
        return {0, Key::null(), NodeType::Empty};
    }

    static BuiltNode reuse(const ParsedNode &node) {
        return {node.nodeId, Key::existing(node.nodeHash()), node.nodeType};
    }

    // For when you have a nodeId and a nodeHash, but don't need to create a ParsedNode
    static BuiltNode stubbed(uint64_t nodeId, const Key &nodeHash) {
        return {nodeId, nodeHash, NodeType::Invalid};
    }

    static BuiltNode newLeaf(Quadrable *db, lmdb::txn &txn, const Key &keyHash, std::string_view val, std::string_view leafKey = "") {
        BuiltNode output;

        {
            Key valHash = Key::hash(val);
            unsigned char nullChar = 0;

            {
                Hash h(sizeof(output.nodeHash.data));
                h.update(keyHash.sv());
                h.update(valHash.sv());
                h.update(&nullChar, 1);
                h.final(output.nodeHash.data);
            }
        }

        std::string nodeRaw;

        nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Leaf));
        nodeRaw += output.nodeHash.sv();
        nodeRaw += keyHash.sv();
        nodeRaw += val;

        output.nodeId = db->writeNodeToDb(txn, nodeRaw, true);
        output.nodeType = NodeType::Leaf;

        db->setLeafKey(txn, output.nodeId, leafKey);

        return output;
    }

    static BuiltNode newLeaf(Quadrable *db, lmdb::txn &txn, UpdateSetMap::iterator it) {
        if (it->second.nodeIdOverride != 0) {
            ParsedNode node(db, txn, it->second.nodeIdOverride);
            if (!node.isLeaf()) throw quaderr("trying to reuse a non-leaf node as a leaf");
            if (it->first != node.leafKeyHash()) throw quaderr("non-matching leaf key when re-using leaf node");
            return reuse(node);
        }

        return newLeaf(db, txn, it->first, it->second.val, it->second.key);
    }

    static BuiltNode newWitnessLeaf(Quadrable *db, lmdb::txn &txn, const Key &keyHash, const Key &valHash) {
        BuiltNode output;

        {
            unsigned char nullChar = 0;

            {
                Hash h(sizeof(output.nodeHash.data));
                h.update(keyHash.sv());
                h.update(valHash.sv());
                h.update(&nullChar, 1);
                h.final(output.nodeHash.data);
            }
        }

        std::string nodeRaw;

        nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::WitnessLeaf));
        nodeRaw += output.nodeHash.sv();
        nodeRaw += keyHash.sv();
        nodeRaw += valHash.sv();

        output.nodeId = db->writeNodeToDb(txn, nodeRaw, true);
        output.nodeType = NodeType::WitnessLeaf;

        return output;
    }

    static BuiltNode newBranch(Quadrable *db, lmdb::txn &txn, const BuiltNode &leftNode, const BuiltNode &rightNode) {
        BuiltNode output;

        {
            Hash h(sizeof(output.nodeHash.data));
            h.update(leftNode.nodeHash.data, sizeof(leftNode.nodeHash.data));
            h.update(rightNode.nodeHash.data, sizeof(rightNode.nodeHash.data));
            h.final(output.nodeHash.data);
        }

        std::string nodeRaw;

        uint64_t w1;

        if (rightNode.nodeId == 0) {
            output.nodeType = NodeType::BranchLeft;
            w1 = uint64_t(NodeType::BranchLeft) | leftNode.nodeId << 4;
        } else if (leftNode.nodeId == 0) {
            output.nodeType = NodeType::BranchRight;
            w1 = uint64_t(NodeType::BranchRight) | rightNode.nodeId << 4;
        } else {
            output.nodeType = NodeType::BranchBoth;
            w1 = uint64_t(NodeType::BranchBoth) | leftNode.nodeId << 4;
        }

        nodeRaw += lmdb::to_sv<uint64_t>(w1);
        nodeRaw += output.nodeHash.sv();

        if (rightNode.nodeId && leftNode.nodeId) {
            nodeRaw += lmdb::to_sv<uint64_t>(rightNode.nodeId);
        } else {
            nodeRaw += lmdb::to_sv<uint64_t>(0); // padding
        }

        output.nodeId = db->writeNodeToDb(txn, nodeRaw, false);

        return output;
    }

    static BuiltNode newWitness(Quadrable *db, lmdb::txn &txn, const Key &hash) {
        std::string nodeRaw;

        nodeRaw += lmdb::to_sv<uint64_t>(uint64_t(NodeType::Witness));
        nodeRaw += hash.sv();
        nodeRaw += lmdb::to_sv<uint64_t>(0); // padding

        BuiltNode output;

        output.nodeId = db->writeNodeToDb(txn, nodeRaw, false);
        output.nodeHash = hash;
        output.nodeType = NodeType::BranchBoth;

        return output;
    }

    bool isEmpty() { return nodeType == NodeType::Empty; }
    bool isLeaf() { return nodeType == NodeType::Leaf || nodeType == NodeType::WitnessLeaf; }
    bool isBranch() { return nodeType == NodeType::BranchLeft || nodeType == NodeType::BranchRight || nodeType == NodeType::BranchBoth; }
    bool isWitness() { return nodeType == NodeType::Witness; }
    bool isWitnessLeaf() { return nodeType == NodeType::WitnessLeaf; }
    bool isWitnessAny() { return nodeType == NodeType::Witness || nodeType == NodeType::WitnessLeaf; }
};
