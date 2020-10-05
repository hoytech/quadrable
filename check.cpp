#include <string>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <set>
#include <bitset>

#include "quadrable.h"
#include "quadrable/utils.h"
#include "quadrable/proof.h"




namespace quadrable {

void doTests() {
    std::string dbDir = "testdb/";


    lmdb::env lmdb_env = lmdb::env::create();

    lmdb_env.set_max_dbs(64);
    lmdb_env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL * 1024UL);

    lmdb_env.open(dbDir.c_str(), MDB_CREATE, 0664);

    lmdb_env.reader_check();

    quadrable::Quadrable db;

    //db.trackKeys = true;

    {
        auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);
        db.init(txn);
        txn.commit();
    }



    auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);



#   define verify(condition) do { if (!(condition)) throw quaderr(#condition, "  |  ", __FILE__, ":", __LINE__); } while(0)
#   define verifyThrow(condition, expected) { \
        bool caught = false; \
        std::string errorMsg; \
        try { condition; } \
        catch (const std::runtime_error &e) { \
            caught = true; \
            errorMsg = e.what(); \
        } \
        if (!caught) throw quaderr(#condition, " | expected error, but didn't get one (", expected, ")"); \
        if (errorMsg.find(expected) == std::string::npos) throw quaderr(#condition, " | error msg not what we expected: ", errorMsg, " (not ", expected, ")"); \
    }

    auto test = [&](std::string testName, std::function<void()> cb) {
        db.checkout();

        std::cout << "TEST: " << testName << std::endl;

        try {
            cb();
        } catch (const std::runtime_error& error) {
            throw quaderr(testName, "  |  ", error.what());
        }

        std::cout << "OK." << std::endl;
    };

    auto equivHeads = [&](std::string desc, std::function<void()> cb1, std::function<void()> cb2, bool expectEqual = true) {
        if (desc.size()) std::cout << "  - " << desc << std::endl;

        db.checkout();
        cb1();
        auto root1 = db.root(txn);

        db.checkout();
        cb2();
        auto root2 = db.root(txn);

        verify((root1 == root2) == expectEqual);
    };

    auto proofRoundtrip = [](const Proof &p) {
        return quadrable::proofTransport::decodeProof(quadrable::proofTransport::encodeProof(p));
    };

    auto dump = [&]{ quadrable::dumpDb(db, txn); };
    auto stats = [&]{ quadrable::dumpStats(db, txn); };
    auto stop = [&]{ throw quaderr("STOP"); };
    (void)dump;
    (void)stop;
    (void)stats;



    test("basic put/get", [&]{
        db.change()
          .put("hello", "world")
          .apply(txn);

        std::string_view val;
        verify(db.get(txn, "hello", val));
        verify(val == "world");

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 1);
    });


    test("zero-length keys", [&]{
        verifyThrow(db.change().put("", "1").apply(txn), "zero-length keys not allowed");
        verifyThrow(db.change().del("").apply(txn), "zero-length keys not allowed");
    });


    test("overwriting updates before apply", [&]{
        equivHeads("double put", [&]{
            db.change().put("a", "1").apply(txn);
            db.change().put("a", "1").apply(txn);
        },[&]{
            db.change().put("a", "1").apply(txn);
        });

        equivHeads("del overwrites put", [&]{
            db.change().put("a", "1").del("a").apply(txn);
        },[&]{
        });

        equivHeads("put overwrites del overwrites put", [&]{
            db.change().put("a", "1").del("a").put("a", "2").apply(txn);
        },[&]{
            db.change().put("a", "2").apply(txn);
        });
    });




    test("basic put/get integers", [&]{
        db.change()
          .put(0, "zero")
          .put(1, "one")
          .apply(txn);

        std::string_view val;
        verify(db.get(txn, 0, val));
        verify(val == "zero");

        verify(db.get(txn, 1, val));
        verify(val == "one");

        verify(!db.get(txn, 2, val));

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 2);
    });

    test("integer round-trips", [&]{
        for (uint i = 0; i < 100'000; i++) {
            verify(quadrable::Hash::fromInteger(i).toInteger() == i);
        }

        {
            uint64_t i = std::numeric_limits<uint64_t>::max() - 2;
            verify(quadrable::Hash::fromInteger(i).toInteger() == i);
        }

        {
            uint64_t i = std::numeric_limits<uint64_t>::max() - 1;
            verifyThrow(quadrable::Hash::fromInteger(i), "int range exceeded");
        }

        {
            uint64_t i = std::numeric_limits<uint64_t>::max();
            verifyThrow(quadrable::Hash::fromInteger(i), "int range exceeded");
        }
    });

    test("individual push", [&]{
        db.push(txn, "a");
        db.push(txn, "b");
        db.push(txn, "c");
        db.push(txn, "d");
        db.push(txn, "e");
        db.push(txn, "f");
        db.push(txn, "g");

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 8); // 7 plus the array size elem

        std::string_view val;

        verify(db.get(txn, 0, val));
        verify(val == "a");

        verify(db.get(txn, 1, val));
        verify(val == "b");

        verify(db.get(txn, 6, val));
        verify(val == "g");
    });

    test("batch push", [&]{
        db.change()
          .push(txn, "a")
          .push(txn, "b")
          .push(txn, "c")
          .push(txn, "d")
          .push(txn, "e")
          .push(txn, "f")
          .push(txn, "g")
          .apply(txn);

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 8); // 7 plus the array size elem

        std::string_view val;

        verify(db.get(txn, 0, val));
        verify(val == "a");

        verify(db.get(txn, 1, val));
        verify(val == "b");

        verify(db.get(txn, 6, val));
        verify(val == "g");
    });



    test("empty heads", [&]{
        verify(Hash::nullHash() == db.root(txn));

        std::string_view val;
        verify(!db.get(txn, "hello", val));

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 0);

        db.change().put("a", "1").apply(txn);
        verify(Hash::nullHash() != db.root(txn));

        db.change().del("a").apply(txn);
        verify(Hash::nullHash() == db.root(txn));
    });



    test("batch insert", [&]{
        db.change()
          .put("a", "1")
          .put("b", "2")
          .put("c", "3")
          .apply(txn);

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 3);

        std::string_view val;
        verify(db.get(txn, "b", val));
        verify(val == "2");
    });


    test("getMulti", [&]{
        auto changes = db.change();
        for (int i=0; i<100; i++) {
            std::string s = std::to_string(i);
            changes.put(s, std::string("N = ") + s);
        }
        changes.apply(txn);

        auto query = db.get(txn, { "30", "31", "32", "blah", "nope", });

        verify(query["30"].exists && query["30"].val == "N = 30");
        verify(query["31"].exists && query["31"].val == "N = 31");
        verify(query["32"].exists && query["32"].val == "N = 32");

        verify(!query["blah"].exists);
        verify(!query["nope"].exists);
    });


    test("del", [&]{
        {
            auto changes = db.change();
            changes.put("a", "1");
            changes.put("b", "2");
            changes.put("c", "3");
            changes.apply(txn);
        }

        db.change()
          .del("b")
          .apply(txn);

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 2);

        std::string_view val;
        verify(!db.get(txn, "b", val));
    });


    test("del bubble", [&]{
        equivHeads("bubble right", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);
            db.change().del("b").apply(txn);
        },[&]{
            db.change().put("a", "1").apply(txn);
        });

        equivHeads("bubble left", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);
            db.change().del("a").apply(txn);
        },[&]{
            db.change().put("b", "2").apply(txn);
        });

        equivHeads("delete both sides of a branch in same update, leaving empty node", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);
            db.change().del("a").del("b").apply(txn);
        },[&]{
        });

        equivHeads("delete both sides of a branch in same update, which causes sibling leaf to bubble up", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("a").del("c").apply(txn);
        },[&]{
            db.change().put("b", "2").apply(txn);
        });

        equivHeads("delete one side of a branch and a sibling leaf in same update, which causes remaining side of branch to bubble up", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("b").del("c").apply(txn);
        },[&]{
            db.change().put("a", "1").apply(txn);
        });

        equivHeads("same as previous, but other side of the branch", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("b").del("a").apply(txn);
        },[&]{
            db.change().put("c", "3").apply(txn);
        });

        equivHeads("long bubble", [&]{
            db.change()
              .put("146365204598", "A") // 11111111...
              .put("967276293879", "B") // 11111110...
              .apply(txn);

            db.change()
              .del("146365204598")
              .apply(txn);
        },[&]{
            db.change()
              .put("967276293879", "B") // 11111110...
              .apply(txn);
        });

        equivHeads("long bubble, double deletion", [&]{
            db.change()
              .put("146365204598", "A") // 11111111...
              .put("967276293879", "B") // 11111110...
              .put("948464225881", "C") // 1110...
              .apply(txn);

            db.change()
              .del("967276293879")
              .del("948464225881")
              .apply(txn);
        },[&]{
            db.change()
              .put("146365204598", "A") // 11111110...
              .apply(txn);
        });
    });


    test("mix del and put", [&]{
        equivHeads("left", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("a").put("c", "4").apply(txn);
        },[&]{
            db.change().put("b", "2").put("c", "4").apply(txn);
        });

        equivHeads("right", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("a").put("d", "4").apply(txn);
        },[&]{
            db.change().put("b", "2").put("c", "3").put("d", "4").apply(txn);
        });
    });

    test("del non-existent", [&]{
        equivHeads("empty root", [&]{
            db.change().del("a").apply(txn);
        },[&]{
        });

        equivHeads("simple", [&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
            db.change().del("d").apply(txn);
        },[&]{
            db.change().put("a", "1").put("b", "2").put("c", "3").apply(txn);
        });

        equivHeads("delete a node, and try to delete a non-existent node underneath it", [&]{
            db.change().put("a", "1").apply(txn);
            db.change().del("a").del("849686319312").apply(txn); // 01...
        },[&]{
        });

        equivHeads("same as previous, but requires bubbling", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);
            db.change().del("a").del("849686319312").apply(txn); // 01...
        },[&]{
            db.change().put("b", "2").apply(txn);
        });
    });


    test("leaf splitting while deleting/updating split leaf", [&]{
        equivHeads("first", [&]{
            db.change().put("a", "1").apply(txn); // 0...
            db.change().del("a").put("849686319312", "2").apply(txn); // 01...
        },[&]{
            db.change().put("849686319312", "2").apply(txn); // 01...
        });

        equivHeads("second", [&]{
            db.change().put("a", "1").apply(txn); // 0...
            db.change().put("a", "3").put("849686319312", "2").apply(txn); // 01...
        },[&]{
            db.change().put("a", "3").put("849686319312", "2").apply(txn); // 01...
        });
    });


    test("bunch of strings", [&]{
        int n = 1000;

        auto changes = db.change();
        for (int i=0; i<n; i++) {
            std::string s = std::to_string(i);
            changes.put(s, s+s);
        }
        changes.apply(txn); // in batch

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == (uint64_t)n);

        for (int i=0; i<n; i++) {
            std::string s = std::to_string(i);

            std::string_view val;
            verify(db.get(txn, s, val));
            verify(val == s+s);
        }


        auto origRoot = db.root(txn);



        db.checkout("bunch of ints, added one by one");
        verify(db.root(txn) == std::string(32, '\0'));

        for (int i=0; i<n; i++) {
            std::string s = std::to_string(i);
            db.put(txn, s, s+s); // not in batch
        }

        stats = db.stats(txn);
        verify(stats.numLeafNodes == (uint64_t)n);

        verify(db.root(txn) == origRoot);




        db.checkout("bunch of ints, added one by one in reverse");
        verify(db.root(txn) == std::string(32, '\0'));

        for (int i=n-1; i>=0; i--) {
            std::string s = std::to_string(i);
            db.change().put(s, s+s).apply(txn); // not in batch
        }

        stats = db.stats(txn);
        verify(stats.numLeafNodes == (uint64_t)n);

        verify(db.root(txn) == origRoot);
    });



    test("large mixed update/del", [&]{
        equivHeads("", [&]{
            auto changes = db.change();

            for (int i=0; i<600; i++) {
                std::string s = std::to_string(i);
                changes.put(s, s+s);
            }

            changes.apply(txn);

            // delete 0-99
            for (int i=0; i<100; i++) changes.del(std::to_string(i));

            // update 100-199
            for (int i=100; i<200; i++) {
                std::string s = std::to_string(i);
                changes.put(s, s+s+"updated");
            }

            // add 600-699
            for (int i=600; i<700; i++) {
                std::string s = std::to_string(i);
                changes.put(s, s+s);
            }

            changes.apply(txn);
        },[&]{
            auto changes = db.change();

            for (int i=100; i<200; i++) {
                std::string s = std::to_string(i);
                changes.put(s, s+s+"updated");
            }

            for (int i=200; i<700; i++) {
                std::string s = std::to_string(i);
                changes.put(s, s+s);
            }

            changes.apply(txn);
        });
    });



    test("back up start of iterator window", [&]{
        db.change()
          .put("a", "A")
          .put("b", "B")
          .apply(txn);

        std::string_view val;

        verify(db.get(txn, "a", val));
        verify(val == "A");

        verify(db.get(txn, "b", val));
        verify(val == "B");

        auto stats = db.stats(txn);
        verify(stats.numLeafNodes == 2);
    });



    test("fork", [&]{
        std::string_view val;

        db.change()
          .put("a", "A")
          .put("b", "B")
          .put("c", "C")
          .put("d", "D")
          .apply(txn);

        auto origNodeId = db.getHeadNodeId(txn);

        db.fork(txn);

        db.change()
          .put("e", "E")
          .apply(txn);

        auto newNodeId = db.getHeadNodeId(txn);

        verify(db.get(txn, "a", val));
        verify(val == "A");
        verify(db.get(txn, "e", val));
        verify(val == "E");

        {
            auto stats = db.stats(txn);
            verify(stats.numLeafNodes == 5);
        }

        db.checkout(origNodeId);

        verify(db.get(txn, "a", val));
        verify(val == "A");
        verify(!db.get(txn, "e", val));

        {
            auto stats = db.stats(txn);
            verify(stats.numLeafNodes == 4);
        }

        db.checkout(newNodeId);

        verify(db.get(txn, "a", val));
        verify(val == "A");
        verify(db.get(txn, "e", val));
        verify(val == "E");
    });




    test("basic proof", [&]{
        auto changes = db.change();
        for (int i=0; i<100; i++) {
            std::string s = std::to_string(i);
            changes.put(s, s + "val");
        }
        changes.put("long", std::string(789, 'A')); // test varints
        changes.apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProof(txn, {
            "99",
            "68",
            "long",
            "asdf",
        }));

        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;

        verify(db.get(txn, "99", val));
        verify(val == "99val");

        verify(db.get(txn, "68", val));
        verify(val == "68val");

        verify(db.get(txn, "long", val));
        verify(val == std::string(789, 'A'));

        verify(!db.get(txn, "asdf", val));

        verifyThrow(db.get(txn, "0", val), "incomplete tree");
    });


    test("use same empty node for multiple keys", [&]{
        db.change()
          .put("735838777414", "A") // 000...
          .put("367300200150", "B") // 001...
          .apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProof(txn, {
            "582086612140", // 010
            "37481825503",  // 011
        }));

        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;
        verify(!db.get(txn, "582086612140", val));
        verify(!db.get(txn, "37481825503", val));
        verify(!db.get(txn, "915377487270", val)); // another 011... (uses empty)

        verifyThrow(db.get(txn, "735838777414", val), "incomplete tree"); // exists as a witness only
        verifyThrow(db.get(txn, "367300200150", val), "incomplete tree"); // exists as a witness only
    });


    test("more proofs", [&]{
        db.change()
          .put("983467173326", "A") // 10...
          .put("50728759955", "B")  // 11...
          .put("679040280359", "C")  // 01...
          .put("685903554406", "D")  // 000...
          .put("66727828072", "E")  // 00001...
          .apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProof(txn, {
            "983467173326",
            "50728759955",
            "836336493412", // 00..
            "826547358742", // 001..
            "231172376960", // 001..
        }));


        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;

        verify(db.get(txn, "983467173326", val));
        verify(val == "A");
        verify(db.get(txn, "50728759955", val));
        verify(val == "B");
        verifyThrow(db.get(txn, "679040280359", val), "incomplete tree"); // exists as a witness only

        verify(!db.get(txn, "826547358742", val));
        verify(!db.get(txn, "836336493412", val));
        verify(!db.get(txn, "231172376960", val));
    });


    test("push proofs", [&]{
        auto change = db.change();

        uint64_t n = 1000;

        for (uint64_t i = 0; i < n; i++) {
            change.push(txn, "asdf " + std::to_string(i));
        }

        change.apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProofInteger(txn, {
            n - 20,
        }));


        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;

        verify(db.get(txn, n - 20, val));
        verify(val == "asdf " + std::to_string(n - 20));
        verifyThrow(db.get(txn, n - 9, val), "incomplete tree");
        verifyThrow(db.get(txn, n - 1, val), "incomplete tree");
        verifyThrow(db.get(txn, n, val), "incomplete tree");
        verifyThrow(db.get(txn, n + 1, val), "incomplete tree");
    });

    test("pushable and individual", [&]{
        auto change = db.change();

        uint64_t n = 1000;

        for (uint64_t i = 0; i < n; i++) {
            change.push(txn, "asdf " + std::to_string(i));
        }

        change.apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProofPushable(txn, {
            600,
            601,
            602,
        }));


        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;

        verify(db.get(txn, 600, val));
        verify(val == "asdf 600");
        verify(db.get(txn, 601, val));
        verify(val == "asdf 601");
        verify(db.get(txn, 602, val));
        verify(val == "asdf 602");

        verifyThrow(db.get(txn, 603, val), "incomplete tree");
        verifyThrow(db.get(txn, n-1, val), "incomplete tree");

        // all >=n are available (not-present)
        for (uint i = n; i < n*100; i++) verify(!db.get(txn, i, val));
    });



    test("big proof test", [&]{
        auto changes = db.change();
        for (int i=0; i<1000; i++) {
            std::string s = std::to_string(i);
            changes.put(s, s + "val");
        }
        changes.apply(txn);

        auto origRoot = db.root(txn);

        std::set<std::string> keys;
        for (int i=-500; i<500; i++) {
            keys.insert(std::to_string(i));
        }

        auto proof = proofRoundtrip(db.exportProof(txn, keys));


        db.checkout();

        db.importProof(txn, proof, origRoot);

        quadrable::GetMultiQuery query{};
        for (int i=-500; i<500; i++) query.emplace(std::to_string(i), GetMultiResult{});
        db.getMulti(txn, query);

        for (int i=-500; i<500; i++) {
            auto str = std::to_string(i);
            if (i < 0) {
                verify(!query[str].exists);
            } else {
                verify(query[str].exists && query[str].val == str+"val");
            }
        }
    });



    test("sub-proof test", [&]{
        std::string_view val;

        auto changes = db.change();
        for (int i=0; i<100; i++) {
            std::string s = std::to_string(i);
            changes.put(s, s + "val");
        }
        changes.apply(txn);

        auto origRoot = db.root(txn);

        quadrable::Proof proof, proof2;

        {
            std::set<std::string> keys;
            for (int i=-50; i<50; i++) {
                keys.insert(std::to_string(i));
            }

            proof = proofRoundtrip(db.exportProof(txn, keys));
        }

        db.checkout();
        db.importProof(txn, proof, origRoot);

        verify(db.get(txn, "33", val));
        verify(val == "33val");

        {
            std::set<std::string> keys;
            for (int i=-10; i<10; i++) {
                keys.insert(std::to_string(i));
            }

            proof2 = proofRoundtrip(db.exportProof(txn, keys));
        }

        db.checkout();
        db.importProof(txn, proof2, origRoot);

        quadrable::GetMultiQuery query{};
        for (int i=-10; i<10; i++) query.emplace(std::to_string(i), GetMultiResult{});
        db.getMulti(txn, query);

        for (int i=-10; i<10; i++) {
            auto str = std::to_string(i);
            if (i < 0) {
                verify(!query[str].exists);
            } else {
                verify(query[str].exists && query[str].val == str+"val");
            }
        }

        verifyThrow(db.get(txn, "33", val), "incomplete tree");
    });



    test("no unnecessary empty witnesses", [&]{
        db.change()
          .put("983467173326", "A") // 10...
          .put("50728759955", "B")  // 11...
          .apply(txn);

        auto origRoot = db.root(txn);

        auto proof = proofRoundtrip(db.exportProof(txn, {
            "983467173326",
            "14864808866", // 00...
        }));

        verify(proof.strands.size() == 1); // No separate WitnessEmpty is needed because a HashEmpty cmd is on existing node's path

        db.checkout();

        db.importProof(txn, proof, origRoot);

        std::string_view val;

        verify(db.get(txn, "983467173326", val));
        verify(val == "A");

        verifyThrow(db.get(txn, "50728759955", val), "incomplete tree");

        verify(!db.get(txn, "14864808866", val));
    });





    test("update proof", [&]{
        auto setupDb = [&]{
            db.change()
              .put("388662362962", "A")  // 01...
              .put("947167210798", "B")  // 1000...
              .put("363565948405", "C")  // 101...
              .put("287625867965", "D")  // 1001...
              .apply(txn);
        };

        quadrable::Proof proof;
        std::string origRoot, newRoot;
        std::string_view val;


        equivHeads("update leaf, fail trying to update witness", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "388662362962",
            }));
            origRoot = db.root(txn);

            db.change().put("388662362962", "A2").apply(txn);
            newRoot = db.root(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().put("388662362962", "A2").apply(txn);

            verify(db.root(txn) == newRoot); // also checked by equivHeads

            verifyThrow(db.change().put("947167210798", "B2").apply(txn), "encountered witness during update");
        });


        equivHeads("update 2 leafs at different levels", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "947167210798",
                "363565948405",
            }));
            origRoot = db.root(txn);

            db.change().put("947167210798", "B2").apply(txn);
            db.change().put("363565948405", "C2").apply(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().put("947167210798", "B2").apply(txn);
            db.change().put("363565948405", "C2").apply(txn);
        });


        equivHeads("split leaf", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "947167210798",
            }));
            origRoot = db.root(txn);

            db.change().put("762909246408", "E").apply(txn); // 1000...
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().put("762909246408", "E").apply(txn); // 1000...
        });




        equivHeads("no change to witness leaf", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "627438066816", // 00...
            }));
            origRoot = db.root(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            verifyThrow(db.get(txn, "388662362962", val), "incomplete tree");

            auto nodeId = db.getHeadNodeId(txn);
            db.change().put("388662362962", "A").apply(txn);

            verify(nodeId != db.getHeadNodeId(txn)); // must use new nodes, since upgrading WitnessLeaf to Leaf

            verify(db.get(txn, "388662362962", val));
            verify(val == "A");
        });



        equivHeads("no change to witness leaf", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "627438066816", // 00...
            }));
            origRoot = db.root(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            verifyThrow(db.get(txn, "388662362962", val), "incomplete tree");

            auto nodeId = db.getHeadNodeId(txn);
            db.change().put("388662362962", "A").apply(txn);

            verify(nodeId != db.getHeadNodeId(txn)); // must use new nodes, since upgrading WitnessLeaf to Leaf

            verify(db.get(txn, "388662362962", val));
            verify(val == "A");
        });




        equivHeads("update witness leaf", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "627438066816", // 00...
            }));
            origRoot = db.root(txn);

            db.change().put("388662362962", "A2").apply(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().put("388662362962", "A2").apply(txn);
        });



        equivHeads("split witness leaf", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProof(txn, {
                "627438066816", // 00...
            }));
            origRoot = db.root(txn);

            db.change().put("627438066816", "NEW").apply(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().put("627438066816", "NEW").apply(txn);
        });





        equivHeads("can bubble up a witnessLeaf", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);

            proof = proofRoundtrip(db.exportProof(txn, {
                "a", // 0...
                "d", // 1...
            }));
            origRoot = db.root(txn);

            db.change().del("a").apply(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            db.change().del("a").apply(txn);
        });




        equivHeads("can't bubble up a witness", [&]{
            db.change().put("a", "1").put("b", "2").apply(txn);

            proof = proofRoundtrip(db.exportProof(txn, {
                "a",
            }));

            origRoot = db.root(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            // FIXME: support creating a proof with enough information to do a deletion
            //db.change().del("a").apply(txn);
            verifyThrow(db.change().del("a").apply(txn), "can't bubble a witness node");
        });


    });



    test("update push proof", [&]{
        auto setupDb = [&]{
            auto change = db.change();

            uint64_t n = 1000;

            for (uint64_t i = 0; i < n; i++) {
                change.push(txn, "asdf " + std::to_string(i));
            }

            change.apply(txn);
        };

        quadrable::Proof proof;
        std::string origRoot, newRoot;
        std::string_view val;


        equivHeads("push on a bunch of extras", [&]{
            setupDb();

            proof = proofRoundtrip(db.exportProofPushable(txn));
            origRoot = db.root(txn);

            auto changes = db.change();
            for (uint i = 0; i < 5000; i++) changes.push(txn, "new elem " + std::to_string(i));
            changes.apply(txn);

            newRoot = db.root(txn);
        }, [&]{
            db.importProof(txn, proof, origRoot);

            auto changes = db.change();
            for (uint i = 0; i < 5000; i++) changes.push(txn, "new elem " + std::to_string(i));
            changes.apply(txn);

            verify(db.root(txn) == newRoot); // also checked by equivHeads
        });
    });


    txn.abort();

    /*
    for(size_t i=0; i<=256; i++) {
        Hash h = Hash::existingHash(from_hex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
        h.keepPrefixBits(i);
        std::cout << i << " : " << to_hex(h.str()) << std::endl;
    }
    */
}



}



int main() {
    try {
        quadrable::doTests();
    } catch (const std::runtime_error& error) {
        std::cerr << "Test failure: " << error.what() << std::endl;
        return 1;
    }

    std::cout << "\nAll tests OK" << std::endl;
}
