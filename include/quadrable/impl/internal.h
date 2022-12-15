private:

// Internal storage interface

bool getNode(lmdb::txn &txn, uint64_t nodeId, std::string_view &output) {
    if (memStore) {
        auto it = memStore->nodes.find(nodeId);
        if (it == memStore->nodes.end()) return false;
        output = it->second;
        return true;
    } else {
        return dbi_node.get(txn, lmdb::to_sv<uint64_t>(nodeId), output);
    }
}

uint64_t writeNodeToDb(lmdb::txn &txn, std::string_view nodeRaw) {
    assert(nodeRaw.size() >= 40);

    uint64_t newNodeId;

    if (memStore) {
        auto it = memStore->nodes.rbegin();
        if (it == memStore->nodes.rend()) newNodeId = firstMemStoreNodeId;
        else newNodeId = it->first + 1;

        memStore->nodes[newNodeId] = std::string(nodeRaw);
    } else {
        newNodeId = getNextIntegerKey(txn, dbi_node);

        dbi_node.put(txn, lmdb::to_sv<uint64_t>(newNodeId), nodeRaw);
    }

    return newNodeId;
}

// Assertions

void assertDepth(uint64_t depth) {
    assert(depth <= 255); // should only happen on hash collision (or a bug)
}

// Helper methods for LMDB:

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

uint64_t getNextIntegerKey(lmdb::txn &txn, lmdb::dbi &dbi) {
    return getLargestIntegerKeyOrZero(txn, dbi) + 1;
}

uint64_t getLargestIntegerKeyOrZero(lmdb::txn &txn, lmdb::dbi &dbi) {
    auto cursor = lmdb::cursor::open(txn, dbi);
    return getIntegerKeyOrZero(cursor, MDB_LAST);
}
