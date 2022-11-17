public:

void walkTree(lmdb::txn &txn, std::function<bool(ParsedNode &, uint64_t)> cb) {
    auto nodeId = getHeadNodeId(txn);
    walkTreeAux(txn, cb, nodeId, 0);
}

void walkTree(lmdb::txn &txn, uint64_t nodeId, std::function<bool(ParsedNode &, uint64_t)> cb) {
    walkTreeAux(txn, cb, nodeId, 0);
}

private:

void walkTreeAux(lmdb::txn &txn, std::function<bool(ParsedNode &, uint64_t)> cb, uint64_t nodeId, uint64_t depth) {
    ParsedNode node(txn, dbi_node, nodeId);

    if (node.isEmpty()) return;

    if (!cb(node, depth)) return;

    if (node.isBranch()) {
        assertDepth(depth);

        walkTreeAux(txn, cb, node.leftNodeId, depth+1);
        walkTreeAux(txn, cb, node.rightNodeId, depth+1);
    }
}
