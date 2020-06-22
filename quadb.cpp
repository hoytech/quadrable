#include <iostream>
#include <random>

#include <docopt/docopt.h>

#include "quadrable.h"
#include "quadrable/utils.h"



static const char USAGE[] =
R"(
    Usage:
      quadb [--db=<dir>] dump
      quadb [--db=<dir>] put <key> <val>
      quadb [--db=<dir>] proof <key>
      quadb mineHash <prefix>

    Options:
      --db=<dir>            LMDB dir
      -h --help             Show this screen.
      --version             Show version.
)";




std::string dbDir;

void parse_command_line(int argc, char **argv) {
    std::map<std::string, docopt::value> args = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "quadb 0.0.1", true);

    if (args["--db"]) {
        dbDir = args["--db"].asString();
    } else {
        dbDir = "./db/";
    }



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


    if (args["dump"].asBool()) {
        quadrable::dumpDb(db, txn);
    } else if (args["put"].asBool()) {
        std::string k = args["<key>"].asString();
        std::string v = args["<val>"].asString();

        db.change().put(k, v).apply(txn);
    } else if (args["proof"].asBool()) {
        std::string k = args["<key>"].asString();
        // FIXME
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


    txn.commit();
}



int main(int argc, char **argv) {
    try {
        parse_command_line(argc, argv);
    } catch (std::exception &e) {
        std::cerr << "CAUGHT EXCEPTION, ABORTING: " << e.what() << std::endl;
        ::exit(1);
    }

    return 0;
}
