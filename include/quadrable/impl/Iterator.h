public:

struct SavedIterator {
    Key path = Key::null();
    uint64_t depth;
};

struct Iterator {
    friend class Quadrable;

    Quadrable *db;
    lmdb::txn &txn;
    std::vector<ParsedNode> nodeStack;
    bool reverse;

    Iterator(Quadrable *db_, lmdb::txn &txn_, const Key &target, bool reverse_ = false) : db(db_), txn(txn_), reverse(reverse_) {
        nodeStack.emplace_back(db, txn, db->getHeadNodeId(txn));

        bool leftBias = false;

        while (nodeStack.back().isBranch()) {
            uint64_t nextNodeId = target.getBit(nodeStack.size() - 1) == 0 ? nodeStack.back().leftNodeId : nodeStack.back().rightNodeId;
            if (nextNodeId == 0) {
                if (nodeStack.back().leftNodeId) {
                    nextNodeId = nodeStack.back().leftNodeId;
                    leftBias = false;
                } else {
                    nextNodeId = nodeStack.back().rightNodeId;
                    leftBias = true;
                }
                nodeStack.emplace_back(db, txn, nextNodeId);
                break;
            } else {
                nodeStack.emplace_back(db, txn, nextNodeId);
            }
        }

        while (nodeStack.back().isBranch()) {
            uint64_t nextNodeId;
            if (leftBias) nextNodeId = nodeStack.back().leftNodeId != 0 ? nodeStack.back().leftNodeId : nodeStack.back().rightNodeId;
            else nextNodeId = nodeStack.back().rightNodeId != 0 ? nodeStack.back().rightNodeId : nodeStack.back().leftNodeId;
            nodeStack.emplace_back(db, txn, nextNodeId);
        }

        if (nodeStack.back().isLeaf()) {
            if (reverse) {
                if (nodeStack.back().leafKeyHash() > target.sv()) next();
            } else {
                if (nodeStack.back().leafKeyHash() < target.sv()) next();
            }
        }
    }

    void next() {
        {
            uint64_t prevNodeId;
            uint64_t testNodeId;

            do {
                prevNodeId = nodeStack.back().nodeId;
                nodeStack.pop_back();
                if (nodeStack.size()) testNodeId = reverse ? nodeStack.back().leftNodeId : nodeStack.back().rightNodeId;
            } while (nodeStack.size() > 0 && nodeStack.back().isBranch() && testNodeId != 0 && testNodeId == prevNodeId);
        }

        if (nodeStack.size() == 0) return;

        nodeStack.emplace_back(db, txn, reverse ? nodeStack.back().leftNodeId : nodeStack.back().rightNodeId);

        while (nodeStack.back().isBranch()) {
            uint64_t nextNodeId;
            if (reverse) nextNodeId = nodeStack.back().rightNodeId != 0 ? nodeStack.back().rightNodeId : nodeStack.back().leftNodeId;
            else nextNodeId = nodeStack.back().leftNodeId != 0 ? nodeStack.back().leftNodeId : nodeStack.back().rightNodeId;

            nodeStack.emplace_back(db, txn, nextNodeId);
        }
    }

    ParsedNode get() {
        if (nodeStack.size() == 0) return ParsedNode(db, txn, 0);
        return nodeStack.back();
    }

    bool atEnd() {
        return nodeStack.size() == 0 || nodeStack.back().nodeId == 0;
    }

    SavedIterator save() {
        SavedIterator s;

        s.depth = nodeStack.size() - 1;

        if (nodeStack.size() == 0) throw quaderr("empty nodeStack");

        for (size_t i = 0; i < nodeStack.size() - 1; i++) {
            s.path.setBit(i, nodeStack[i].rightNodeId == nodeStack[i + 1].nodeId);
        }

        return s;
    }

    bool restore(lmdb::txn &txn_, const SavedIterator &s) {
        nodeStack.clear();
        nodeStack.emplace_back(db, txn, db->getHeadNodeId(txn));

        for (size_t i = 0; i < s.depth; i++) {
            if (!nodeStack.back().isBranch()) return false;
            nodeStack.emplace_back(db, txn, s.path.getBit(i) ? nodeStack.back().rightNodeId : nodeStack.back().leftNodeId);
        }

        return true;
    }
};

Iterator iterate(lmdb::txn &txn, const Key &target, bool reverse = false) {
    return Iterator(this, txn, target, reverse);
}
