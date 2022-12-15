public:

bool isDetachedHead() {
    return detachedHead;
}

std::string getHead() {
    if (detachedHead) throw quaderr("in detached head mode");
    return head;
}

std::string root(lmdb::txn &txn) {
    uint64_t nodeId = getHeadNodeId(txn);

    return root(txn, nodeId);
}

std::string root(lmdb::txn &txn, uint64_t nodeId) {
    ParsedNode node(this, txn, nodeId);

    return std::string(node.nodeHash());
}

Key rootKey(lmdb::txn &txn) {
    uint64_t nodeId = getHeadNodeId(txn);

    return rootKey(txn, nodeId);
}

Key rootKey(lmdb::txn &txn, uint64_t nodeId) {
    ParsedNode node(this, txn, nodeId);

    return Key::existing(node.nodeHash());
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

void setHeadWitness(lmdb::txn &txn, const Key &key) {
    auto node = BuiltNode::newWitness(this, txn, key);
    setHeadNodeId(txn, node.nodeId);
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
