private:

// Internal storage interface

bool getNode(lmdb::txn &txn, uint64_t nodeId, std::string_view &output) {
    if (nodeId >= firstMemStoreNodeId) {
        if (!memStore) throw quaderr("tried to load MemStore node, but no MemStore attached");
        auto it = memStore->nodes.find(nodeId);
        if (it == memStore->nodes.end()) return false;
        output = it->second;
        return true;
    } else if (nodeId >= firstInteriorNodeId) {
        return dbi_nodesInterior.get(txn, lmdb::to_sv<uint64_t>(nodeId), output);
    } else {
        return dbi_nodesLeaf.get(txn, lmdb::to_sv<uint64_t>(nodeId), output);
    }
}

uint64_t writeNodeToDb(lmdb::txn &txn, std::string_view nodeRaw, bool isLeaf) {
    assert(nodeRaw.size() >= 40);

    uint64_t newNodeId;

    if (writeToMemStore) {
        if (!memStore) throw quaderr("no MemStore configured");

        auto it = memStore->nodes.rbegin();
        if (it == memStore->nodes.rend()) newNodeId = firstMemStoreNodeId;
        else newNodeId = it->first + 1;

        memStore->nodes[newNodeId] = std::string(nodeRaw);
    } else {
        uint64_t newNodeId = getNextId(txn, isLeaf);

        auto dbi = isLeaf ? dbi_nodesLeaf : dbi_nodesInterior;
        dbi.put(txn, lmdb::to_sv<uint64_t>(newNodeId), nodeRaw);

        assert(isLeaf || nodeRaw.size() == 48);
    }

    return newNodeId;
}

uint64_t getNextId(lmdb::txn &txn, bool isLeaf) {
    auto cursor = lmdb::cursor::open(txn, isLeaf ? dbi_nodesLeaf : dbi_nodesInterior);
    std::string_view k, v;

    if (cursor.get(k, v, MDB_LAST)) {
        return lmdb::from_sv<uint64_t>(k) + 1;
    } else {
        return isLeaf ? 1 : firstInteriorNodeId;
    }
}

void assertDepth(uint64_t depth) {
    assert(depth <= 255); // should only happen on hash collision (or a bug)
}
