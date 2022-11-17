public:

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
