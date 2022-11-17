public:

bool getLeafKey(lmdb::txn &txn, uint64_t nodeId, std::string_view &leafKey) {
    if (!trackKeys) return false;
    return dbi_key.get(txn, lmdb::to_sv<uint64_t>(nodeId), leafKey);
}

void setLeafKey(lmdb::txn &txn, uint64_t nodeId, std::string_view leafKey) {
    if (!trackKeys || leafKey.size() == 0) return;
    dbi_key.put(txn, lmdb::to_sv<uint64_t>(nodeId), leafKey);
}
