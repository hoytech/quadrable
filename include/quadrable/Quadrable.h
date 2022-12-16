#pragma once

namespace quadrable {


class Quadrable {
  public:

    lmdb::dbi dbi_head;
    lmdb::dbi dbi_node;
    lmdb::dbi dbi_key;
    bool trackKeys = false;
    bool writeToMemStore = false;

  private:

    std::string head = "master";
    bool detachedHead = false;
    uint64_t detachedHeadNodeId = 0;
    MemStore *memStore = nullptr;
    bool memStoreOwned = false;

  public:

    // Setup

    Quadrable() {
    }

    ~Quadrable() {
        if (memStore && memStoreOwned) {
            delete memStore;
            memStore = nullptr;
            memStoreOwned = writeToMemStore = false;
        }
    }

    void init(lmdb::txn &txn) {
        dbi_head = lmdb::dbi::open(txn, "quadrable_head", MDB_CREATE);
        dbi_node = lmdb::dbi::open(txn, "quadrable_node", MDB_CREATE | MDB_INTEGERKEY);
        if (trackKeys) dbi_key = lmdb::dbi::open(txn, "quadrable_key", MDB_CREATE | MDB_INTEGERKEY);
    }

    #include "quadrable/impl/ParsedNode.h"
    #include "quadrable/impl/BuiltNode.h"
    #include "quadrable/impl/heads.h"
    #include "quadrable/impl/get.h"
    #include "quadrable/impl/update.h"
    #include "quadrable/impl/leafKeys.h"
    #include "quadrable/impl/Iterator.h"
    #include "quadrable/impl/proof.h"
    #include "quadrable/impl/sync.h"
    #include "quadrable/impl/walk.h"
    #include "quadrable/impl/stats.h"
    #include "quadrable/impl/gc.h"
    #include "quadrable/impl/diff.h"
    #include "quadrable/impl/MemStore.h"
    #include "quadrable/impl/internal.h"
};


}
