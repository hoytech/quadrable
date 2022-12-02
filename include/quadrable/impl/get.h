public:

using GetMultiInternalMap = std::map<Key, GetMultiResult &>;

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

bool getRaw(lmdb::txn &txn, const std::string_view key, std::string_view &val) {
    GetMultiQuery query;

    auto rec = query.emplace(key, GetMultiResult{});

    getMultiRaw(txn, query);

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

void getMultiRaw(lmdb::txn &txn, GetMultiQuery &queryMap) {
    GetMultiInternalMap map;

    for (auto &[key, res] : queryMap) {
        map.emplace(Key::existing(key), res);
    }

    uint64_t depth = 0;
    auto nodeId = getHeadNodeId(txn);

    getMultiAux(txn, depth, nodeId, map.begin(), map.end());
}

void getMulti(lmdb::txn &txn, GetMultiQuery &queryMap) {
    GetMultiInternalMap map;

    for (auto &[key, res] : queryMap) {
        map.emplace(Key::hash(key), res);
    }

    uint64_t depth = 0;
    auto nodeId = getHeadNodeId(txn);

    getMultiAux(txn, depth, nodeId, map.begin(), map.end());
}


private:

void getMultiAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, GetMultiInternalMap::iterator begin, GetMultiInternalMap::iterator end) {
    if (begin == end) {
        return;
    }

    ParsedNode node(txn, dbi_node, nodeId);

    if (node.isEmpty()) {
        for (auto i = begin; i != end; ++i) {
            i->second.exists = false;
        }
    } else if (node.isLeaf()) {
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

        assertDepth(depth);

        getMultiAux(txn, depth+1, node.leftNodeId, begin, middle);
        getMultiAux(txn, depth+1, node.rightNodeId, middle, end);
    } else if (node.isWitnessAny()) {
        throw quaderr("encountered witness node: incomplete tree");
    } else {
        throw quaderr("unrecognized nodeType: ", int(node.nodeType));
    }
}
