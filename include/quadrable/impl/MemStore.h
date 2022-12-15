public:

void withMemStore(MemStore &m, std::function<void()> cb) {
    MemStoreGuard g(this);

    memStore = &m;

    cb();
}

private:

struct MemStoreGuard {
    Quadrable *db;
    MemStore *prev;

    MemStoreGuard(Quadrable *db_) : db(db_), prev(db_->memStore) {
    }

    ~MemStoreGuard() {
        db->memStore = prev;
    }
};
