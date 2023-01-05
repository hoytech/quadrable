#include <string>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <vector>
#include <bitset>
#include <random>

#include "quadrable.h"
#include "quadrable/transport.h"
#include "quadrable/debug.h"




namespace quadrable {

void doIt() {
    ::system("mkdir -p testdb/ ; rm testdb/*.mdb");
    std::string dbDir = "testdb/";


    lmdb::env lmdb_env = lmdb::env::create();

    lmdb_env.set_max_dbs(64);
    lmdb_env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL * 1024UL);

    lmdb_env.open(dbDir.c_str(), MDB_CREATE, 0664);

    lmdb_env.reader_check();

    quadrable::Quadrable db;

    {
        auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);
        db.init(txn);
        txn.commit();
    }



    auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);


    std::mt19937 rnd;
    rnd.seed(0);

    for (uint loopVar = 10; loopVar < 20'001; loopVar *= 2) {
        uint64_t numElems = 100000;
        uint64_t maxElem = numElems;
        uint64_t numAlterations = loopVar;

        db.checkout();

        {
            auto c = db.change();
            for (uint64_t i = 0; i < numElems; i++) {
                auto n = rnd() % maxElem;
                c.put(quadrable::Key::fromInteger(n), std::to_string(n));
            }
            c.apply(txn);
        }

        uint64_t origNodeId = db.getHeadNodeId(txn);
        db.fork(txn);

        {
            auto chg = db.change();

            for (uint64_t i = 0; i < numAlterations; i++) {
                auto n = numElems + rnd() % maxElem;
                auto action = rnd() % 2;
                if (action == 0) {
                    chg.put(quadrable::Key::fromInteger(n), "");
                } else if (action == 1) {
                    chg.del(quadrable::Key::fromInteger(n));
                }
            }

            chg.apply(txn);
        }

        uint64_t newNodeId = db.getHeadNodeId(txn);
        auto newKey = db.rootKey(txn);

        Quadrable::Sync sync(&db);
        sync.init(txn, origNodeId);

        uint64_t bytesDown = 0;
        uint64_t bytesUp = 0;
        uint64_t roundTrips = 0;

        while(1) {
            auto reqs = sync.getReqs(txn, 10000);
            uint64_t reqSize = transport::encodeSyncRequests(reqs).size();
            bytesUp += reqSize;
            if (reqs.size() == 0) break;

            auto resps = db.handleSyncRequests(txn, newNodeId, reqs, 100000);
            uint64_t respSize = transport::encodeSyncResponses(resps).size();
            bytesDown += respSize;
            sync.addResps(txn, reqs, resps);

            roundTrips++;

            std::cout << "RT: " << roundTrips << " up: " << reqSize << " down: " << respSize << std::endl;
        }

        db.checkout(sync.nodeIdShadow);
        if (db.rootKey(txn) != newKey) throw quaderr("NOT EQUAL AFTER IMPORT");

        std::cout << loopVar << "," << roundTrips << "," << bytesUp << "," << bytesDown << std::endl;
    }



    txn.abort();
}


}



int main() {
    try {
        quadrable::doIt();
    } catch (const std::runtime_error& error) {
        std::cerr << "Test failure: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
