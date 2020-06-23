#include <stdlib.h> // getenv
#include <unistd.h> // access
#include <string.h> // strerror
#include <errno.h> // strerror
#include <sys/stat.h> // mkdir
#include <sys/types.h> // mkdir

#include <iostream>
#include <random>

#include <docopt.h>

#include "quadrable.h"
#include "quadrable/utils.h"


using quadrable::quaderr;


static const char USAGE[] =
R"(
    Usage:
      quadb [options] init
      quadb [options] put <key> <val>
      quadb [options] get <key>
      quadb [options] status
      quadb [options] proof <key>
    #
    # Development/testing
    #
      quadb [options] dump
      quadb [options] mineHash <prefix>

    Options:
      --db=<dir>  Database directory (default $ENV{QUADB_DIR} || "./quadb-dir/")
      -h --help   Show this screen.
      --version   Show version.
)";




std::string dbDir;

void parse_command_line(int argc, char **argv) {
    std::map<std::string, docopt::value> args = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "quadb 0.0.1", true);

    if (args["--db"]) {
        dbDir = args["--db"].asString();
    } else if (getenv("QUADB_DIR")) {
        dbDir = std::string(getenv("QUADB_DIR"));
    } else {
        dbDir = "./quadb-dir";
    }

    dbDir += "/";

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

    {
        auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);
        db.init(txn);
        txn.commit();
    }



    auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);


    if (args["init"].asBool()) {
        std::cout << "Quadrable directory init'ed: " << dbDir << std::endl;
    } else if (args["dump"].asBool()) {
        quadrable::dumpDb(db, txn);
    } else if (args["put"].asBool()) {
        std::string k = args["<key>"].asString();
        std::string v = args["<val>"].asString();

        db.change().put(k, v).apply(txn);
    } else if (args["get"].asBool()) {
        std::string k = args["<key>"].asString();
        std::string_view v;
        if (!db.get(txn, k, v)) throw quaderr("key not found in db");
        std::cout << v << std::endl;
    } else if (args["status"].asBool()) {
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
        std::cerr << "quadb error: " << e.what() << std::endl;
        ::exit(1);
    }

    return 0;
}
