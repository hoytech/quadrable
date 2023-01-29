public:

struct GCStats {
    uint64_t total = 0;
    uint64_t garbage = 0;
};

template <typename Set = std::set<uint64_t>>
class GarbageCollector {
  friend class Quadrable;

  private:
    Quadrable &db;
    Set markedNodes;
    Set garbageNodes;

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
        db.walkTree(txn, rootNodeId, [&](Quadrable::ParsedNode &node, uint64_t){
            if (markedNodes.find(node.nodeId) != markedNodes.end()) return false;
            markedNodes.insert(node.nodeId);
            return true;
        });
    }

    GCStats sweep(lmdb::txn &txn, std::optional<std::function<bool(uint64_t)>> cb = std::nullopt) {
        GCStats stats;

        std::string_view k, v;

        auto doSweep = [&](lmdb::dbi dbi){
            auto cursor = lmdb::cursor::open(txn, dbi);
            for (bool found = cursor.get(k, v, MDB_FIRST); found; found = cursor.get(k, v, MDB_NEXT)) {
                stats.total++;
                uint64_t nodeId = lmdb::from_sv<uint64_t>(k);
                if (markedNodes.find(nodeId) == markedNodes.end() && (!cb || (*cb)(nodeId))) {
                    garbageNodes.insert(nodeId);
                    stats.garbage++;
                }
            }
        };

        doSweep(db.dbi_nodesInterior);
        doSweep(db.dbi_nodesLeaf);

        return stats;
    }

    void deleteNodes(lmdb::txn &txn) {
        for (auto nodeId : garbageNodes) {
            if (nodeId < firstInteriorNodeId) {
                db.dbi_nodesLeaf.del(txn, lmdb::to_sv<uint64_t>(nodeId));
                if (db.trackKeys) db.dbi_key.del(txn, lmdb::to_sv<uint64_t>(nodeId));
            } else {
                db.dbi_nodesInterior.del(txn, lmdb::to_sv<uint64_t>(nodeId));
            }
        }
    }
};
