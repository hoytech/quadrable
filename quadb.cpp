#include <stdlib.h> // getenv
#include <unistd.h> // access, read
#include <string.h> // strerror
#include <errno.h> // strerror
#include <sys/stat.h> // mkdir
#include <sys/types.h> // mkdir

#include <iostream>
#include <string>
#include <random>

#include <docopt.h>

#include "quadrable.h"
#include "quadrable/utils.h"
#include "quadrable/proof.h"


using quadrable::quaderr;


static const char USAGE[] =
R"(
    Usage:
      quadb [options] init
      quadb [options] put [--] <key> <val>
      quadb [options] del [--] <key>
      quadb [options] get [--] <key>
      quadb [options] export [--sep=<sep>]
      quadb [options] import [--sep=<sep>]
      quadb [options] root
      quadb [options] stats
      quadb [options] status
      quadb [options] diff <head> [--sep=<sep>]
      quadb [options] patch [--sep=<sep>]
      quadb [options] head
      quadb [options] head rm [<head>]
      quadb [options] checkout [<head>]
      quadb [options] fork [<head>] [--from=<from>]
      quadb [options] gc
      quadb [options] exportProof [--format=(noKeys|withKeys)] [--hex] [--dump] [--] <keys>...
      quadb [options] importProof [--root=<root>] [--hex] [--dump]
      quadb [options] mergeProof [--hex]
      quadb [options] dump-tree
      quadb [options] mineHash <prefix>

    Options:
      --db=<dir>     Database directory (default $ENV{QUADB_DIR} || "./quadb-dir/")
      --noTrackKeys  Don't store keys in DB (default $ENV{QUADB_NOTRACKKEYS} || false)
      -h --help      Show this screen.
      --version      Show version.
)";





static std::string slurpStdin() {
    std::string output;
    char buf[4096];

    while (1) {
        ssize_t res = ::read(0, buf, sizeof(buf));
        if (res == 0) break;
        else if (res < 0 && errno == EINTR) continue;
        else if (res < 0) throw quaderr("error reading from stdin: ", strerror(errno));
        output += std::string(buf, static_cast<size_t>(res));
    }

    return output;
}


void run(int argc, char **argv) {
    std::map<std::string, docopt::value> args = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "quadb " QUADRABLE_VERSION);


    std::string dbDir;

    if (args["--db"]) {
        dbDir = args["--db"].asString();
    } else if (getenv("QUADB_DIR")) {
        dbDir = std::string(getenv("QUADB_DIR"));
    } else {
        dbDir = "./quadb-dir";
    }

    dbDir += "/";


    bool noTrackKeys = false;

    if (args["--noTrackKeys"].asBool()) {
        noTrackKeys = true;
    } else if (getenv("QUADB_NOTRACKKEYS")) {
        noTrackKeys = true;
    }


    if (access(dbDir.c_str(), F_OK)) {
        if (args["init"].asBool()) {
            if (mkdir(dbDir.c_str(), 0755)) throw quaderr("Unable to create directory '", dbDir, "': ", strerror(errno));
        } else {
            throw quaderr("Could not access directory '", dbDir, "': ", strerror(errno));
        }
    } else {
        std::string dbFile = dbDir + "data.mdb";

        if (access(dbDir.c_str(), F_OK)) {
            throw quaderr("Directory '", dbDir, "' not init'ed");
        } else {
            if (args["init"].asBool()) throw quaderr("Directory '", dbDir, "' already init'ed");
        }
    }



    lmdb::env lmdb_env = lmdb::env::create();

    lmdb_env.set_max_dbs(64);
    lmdb_env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL * 1024UL);

    lmdb_env.open(dbDir.c_str(), MDB_CREATE, 0664);

    lmdb_env.reader_check();


    quadrable::Quadrable db;

    db.trackKeys = !noTrackKeys;



    auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);

    db.init(txn);
    lmdb::dbi dbi_quadb_state = lmdb::dbi::open(txn, "quadrable_quadb_state", MDB_CREATE);



    {
        std::string_view v;

        if (dbi_quadb_state.get(txn, "detachedHead", v)) {
            db.checkout(lmdb::from_sv<uint64_t>(v));
        } else if (dbi_quadb_state.get(txn, "currHead", v)) {
            db.checkout(v);
        } else {
            db.checkout("master");
        }
    }



    if (args["init"].asBool()) {
        std::cout << "Quadrable directory init'ed: " << dbDir << std::endl;
    } else if (args["dump-tree"].asBool()) {
        quadrable::dumpDb(db, txn);
    } else if (args["put"].asBool()) {
        std::string k = args["<key>"].asString();
        std::string v = args["<val>"].asString();

        db.change().put(k, v).apply(txn);
    } else if (args["del"].asBool()) {
        std::string k = args["<key>"].asString();
        db.change().del(k).apply(txn);
    } else if (args["get"].asBool()) {
        std::string k = args["<key>"].asString();
        std::string_view v;
        if (!db.get(txn, k, v)) throw quaderr("key not found in db");
        std::cout << v << std::endl;
    } else if (args["head"].asBool()) {
        bool isDetachedHead = db.isDetachedHead();
        std::string currHead;
        if (!isDetachedHead) currHead = db.getHead();

        if (args["rm"].asBool()) {
            if (args["<head>"]) {
                db.dbi_head.del(txn, args["<head>"].asString());
            } else {
                if (isDetachedHead) {
                    db.checkout();
                } else {
                    db.dbi_head.del(txn, currHead);
                }
            }
        } else {
            struct HeadElem {
                std::string head;
                uint64_t nodeId;
            };
            std::vector<HeadElem> headElems;

            std::string_view k, v;
            auto cursor = lmdb::cursor::open(txn, db.dbi_head);
            for (bool found = cursor.get(k, v, MDB_FIRST); found; found = cursor.get(k, v, MDB_NEXT)) {
                headElems.emplace_back(HeadElem{ std::string(k), lmdb::from_sv<uint64_t>(v), });
            }

            std::sort(headElems.begin(), headElems.end(), [](auto &a, auto &b){
                if (a.nodeId == b.nodeId) return a.head < b.head;
                return a.nodeId > b.nodeId;
            });

            if (isDetachedHead) {
                uint64_t nodeId = db.getHeadNodeId(txn);
                std::cout << "D> [detached] : " << quadrable::renderNode(db, txn, nodeId) << "\n";
            }

            for (auto &e : headElems) {
                if (!isDetachedHead && currHead == e.head) std::cout << "=> ";
                else std::cout << "   ";

                std::cout << e.head << " : " << quadrable::renderNode(db, txn, e.nodeId) << "\n";
            }

            std::cout << std::flush;
        }
    } else if (args["export"].asBool()) {
        std::string sep = ",";
        if (args["--sep"]) sep = args["--sep"].asString();

        db.walkTree(txn, [&](quadrable::ParsedNode &node, uint64_t depth){
            if (!node.isLeaf()) return true;

            std::string_view leafKey;
            if (db.getLeafKey(txn, node.nodeId, leafKey)) {
                std::cout << leafKey;
            } else {
                std::cout << quadrable::renderUnknown(node.leafKeyHash());
            }

            std::cout << sep;

            if (node.nodeType == quadrable::NodeType::Leaf) std::cout << node.leafVal();
            else std::cout << quadrable::renderUnknown(node.leafValHash());

            std::cout << "\n";

            return true;
        });

        std::cout << std::flush;
    } else if (args["import"].asBool()) {
        std::string sep = ",";
        if (args["--sep"]) sep = args["--sep"].asString();

        auto changes = db.change();
        std::string line;

        while (std::getline(std::cin, line)) {
            size_t delimOffset = line.find(sep);
            if (delimOffset == std::string::npos) throw quaderr("couldn't find separator in input line");
            changes.put(line.substr(0, delimOffset), line.substr(delimOffset + sep.size()));
        }

        changes.apply(txn);
    } else if (args["checkout"].asBool()) {
        if (args["<head>"]) {
            std::string newHead = args["<head>"].asString();
            db.checkout(newHead);
            dbi_quadb_state.put(txn, "currHead", newHead);
            dbi_quadb_state.del(txn, "detachedHead");
        } else {
            db.checkout();
            dbi_quadb_state.del(txn, "currHead");
        }
    } else if (args["fork"].asBool()) {
        if (args["--from"]) db.checkout(args["--from"].asString());

        if (args["<head>"]) {
            std::string newHead = args["<head>"].asString();
            db.fork(txn, newHead);
            dbi_quadb_state.put(txn, "currHead", newHead);
            dbi_quadb_state.del(txn, "detachedHead");
        } else {
            db.fork(txn);
            dbi_quadb_state.del(txn, "currHead");
        }
    } else if (args["root"].asBool()) {
        std::cout << to_hex(db.root(txn), true) << std::endl;
    } else if (args["stats"].asBool()) {
        quadrable::dumpStats(db, txn);
    } else if (args["status"].asBool()) {
        if (db.isDetachedHead()) {
            std::cout << "Detached head" << std::endl;
        } else {
            std::cout << "Head: " << db.getHead() << std::endl;
        }

        uint64_t nodeId = db.getHeadNodeId(txn);
        std::cout << "Root: " << quadrable::renderNode(db, txn, nodeId) << std::endl;
    } else if (args["diff"].asBool()) {
        std::string sep = ",";
        if (args["--sep"]) sep = args["--sep"].asString();

        uint64_t currNodeId = db.getHeadNodeId(txn);
        uint64_t otherNodeId = db.getHeadNodeId(txn, args["<head>"].asString());

        auto deltas = db.diff(txn, otherNodeId, currNodeId);

        for (auto &delta : deltas) {
            if (delta.deletion) std::cout << "-";
            else std::cout << "+";

            std::cout << (delta.key.size() ? delta.key : quadrable::renderUnknown(delta.keyHash));
            std::cout << sep;
            std::cout << delta.val << "\n";
        }

        std::cout << std::flush;
    } else if (args["patch"].asBool()) {
        std::string sep = ",";
        if (args["--sep"]) sep = args["--sep"].asString();

        auto changes = db.change();

        std::string origLine;
        while (std::getline(std::cin, origLine)) {
            if (origLine.size() < 1) throw quaderr("empty line in patch");
            if (origLine[0] == '#') continue;

            std::string_view line(origLine.data() + 1, origLine.size() - 1);

            size_t delimOffset = line.find(sep);
            if (delimOffset == std::string::npos) throw quaderr("couldn't find separator in input line");

            auto k = line.substr(0, delimOffset);
            auto v = line.substr(delimOffset + sep.size());

            if (origLine[0] == '+') changes.put(k, v);
            else if (origLine[0] == '-') changes.del(k);
            else throw quaderr("unexpected line in patch");
        }

        changes.apply(txn);
    } else if (args["gc"].asBool()) {
        quadrable::Quadrable::GarbageCollector gc(db);

        gc.markAllHeads(txn);

        if (db.isDetachedHead()) gc.markTree(txn, db.getHeadNodeId(txn));

        auto stats = gc.sweep(txn);

        std::cout << "Collected " << stats.collected << "/" << stats.total << " nodes" << std::endl;
    } else if (args["exportProof"].asBool()) {
        std::set<std::string> keys;

        for (auto &key : args["<keys>"].asStringList()) {
            keys.insert(key);
        }

        auto proof = db.exportProof(txn, keys);

        std::string format = "noKeys";
        if (args["--format"]) format = args["--format"].asString();

        if (args["--dump"].asBool()) {
            quadrable::dumpProof(proof);
        } else {
            std::string encoded;

            if (format == "noKeys") {
                encoded = quadrable::proofTransport::encodeProof(proof, quadrable::proofTransport::EncodingType::CompactNoKeys);
            } else if (format == "withKeys") {
                encoded = quadrable::proofTransport::encodeProof(proof, quadrable::proofTransport::EncodingType::CompactWithKeys);
            } else {
                throw quaderr("unknown proof format");
            }

            if (args["--hex"].asBool()) {
                std::cout << to_hex(encoded, true) << std::endl;
            } else {
                std::cout << encoded;
            }
        }
    } else if (args["importProof"].asBool()) {
        std::string input = slurpStdin();

        if (args["--hex"].asBool()) {
            input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());
            input = from_hex(input);
        }

        auto proof = quadrable::proofTransport::decodeProof(input);

        if (args["--dump"].asBool()) {
            quadrable::dumpProof(proof);
        } else {
            if (args["--root"]) {
                std::string origRoot = from_hex(args["--root"].asString());
                db.importProof(txn, proof, origRoot);
            } else {
                auto node = db.importProof(txn, proof);
                std::cout << "Imported UNAUTHENTICATED proof. Root: " << to_hex(node.nodeHash.sv(), true) << std::endl;
            }
        }
    } else if (args["mergeProof"].asBool()) {
        std::string input = slurpStdin();

        if (args["--hex"].asBool()) {
            input.erase(std::remove_if(input.begin(), input.end(), ::isspace), input.end());
            input = from_hex(input);
        }

        auto proof = quadrable::proofTransport::decodeProof(input);

        db.mergeProof(txn, proof);
    } else if (args["mineHash"].asBool()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint64_t> distrib(1, 1000000000000);

        std::string prefix = args["<prefix>"].asString();
        uint64_t r;

        while(1) {
            r = distrib(gen);
            auto h = quadrable::Hash::hash(std::to_string(r));

            size_t matched = 0;

            for (size_t i = 0; i < prefix.size(); i++) {
                bool bit = h.getBit(i);
                if (!bit && prefix[i] == '0') matched++;
                else if (bit && prefix[i] == '1') matched++;
                else break;
            }

            if (matched == prefix.size()) {
                std::cout << std::to_string(r) << " -> " << to_hex(h.sv()) << std::endl;
                break;
            }
        }
    }


    if (db.isDetachedHead()) {
        dbi_quadb_state.put(txn, "detachedHead", lmdb::to_sv<uint64_t>(db.getHeadNodeId(txn)));
    }


    txn.commit();
}



int main(int argc, char **argv) {
    try {
        run(argc, argv);
    } catch (std::exception &e) {
        std::cerr << "quadb error: " << e.what() << std::endl;
        ::exit(1);
    }

    return 0;
}
