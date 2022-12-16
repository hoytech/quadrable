public:

void addMemStore(bool _writeToMemStore = true) {
    memStore = new MemStore;
    memStoreOwned = true;
}

void removeMemStore() {
    if (!memStoreOwned) throw quaderr("can't remove non-owned MemStore");
    delete memStore;
    memStore = nullptr;
    memStoreOwned = false;
}

void withMemStore(MemStore &m, std::function<void()> cb) {
    MemStoreGuard g(this, m);
    cb();
}

private:

struct MemStoreGuard {
    Quadrable *db;

    MemStoreGuard(Quadrable *db_, MemStore &m) : db(db_) {
        if (db->trackKeys) throw quaderr("trackKeys not supported in MemStore");
        if (db->memStore) throw quaderr("memStore already installed");

        db->memStore = &m;
    }

    ~MemStoreGuard() {
        db->memStore->headNodeId = db->detachedHeadNodeId;

        db->memStore = nullptr;
    }
};
