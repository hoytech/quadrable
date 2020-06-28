#pragma once

#include <iostream>

#include "quadrable.h"

#include "hoytech/hex.h"

using hoytech::to_hex;
using hoytech::from_hex;



namespace quadrable {


static inline std::string renderNode(quadrable::Quadrable &db, lmdb::txn &txn, ParsedNode &node, size_t abbrev = 0) {
    std::string nodeHash = to_hex(node.nodeHash(), true);
    if (abbrev) nodeHash = nodeHash.substr(0, abbrev+2) + "...";
    return nodeHash + " (" + std::to_string(node.nodeId) + ")";
}

static inline std::string renderNode(quadrable::Quadrable &db, lmdb::txn &txn, uint64_t nodeId) {
    ParsedNode node(txn, db.dbi_node, nodeId);
    return renderNode(db, txn, node);
}


static inline std::string renderUnknown(std::string_view hash) {
    return std::string("H(?)=0x") + to_hex(hash.substr(0, 6)) + "...";
}


static inline void dumpDbAux(quadrable::Quadrable &db, lmdb::txn &txn, uint64_t nodeId, size_t depth) {
    quadrable::ParsedNode node(txn, db.dbi_node, nodeId);

    std::cout << std::string(depth*2, ' '); 

    std::cout << renderNode(db, txn, node, 8) << " ";

    if (node.nodeType == quadrable::NodeType::Empty) {
        std::cout << "empty";
        std::cout << "\n";
    } else if (node.nodeType == quadrable::NodeType::Leaf) {
        std::cout << "leaf: ";

        std::string_view leafKey;
        if (db.getLeafKey(txn, node.nodeId, leafKey)) {
            std::cout << leafKey;
        } else {
            std::cout << renderUnknown(node.leafKeyHash());
        }

        std::cout << " = " << node.leafVal();
        std::cout << "\n";
    } else if (node.nodeType == quadrable::NodeType::WitnessLeaf) {
        std::cout << "witness leaf: " << to_hex(node.leafKeyHash(), true) << " hash(val) = " << to_hex(node.leafValHash(), true);
        std::cout << "\n";
    } else if (node.nodeType == quadrable::NodeType::Witness) {
        std::cout << "witness";
        std::cout << "\n";
    } else {
        std::cout << "branch:";
        std::cout << "\n";

        dumpDbAux(db, txn, node.leftNodeId, depth+1);
        dumpDbAux(db, txn, node.rightNodeId, depth+1);
    }
}

static inline void dumpDb(quadrable::Quadrable &db, lmdb::txn &txn) {
    std::cout << "-----------------\n";
    uint64_t nodeId = db.getHeadNodeId(txn);

    dumpDbAux(db, txn, nodeId, 0);

    std::cout << "-----------------\n";

    std::cout << std::flush;
}



static inline void dumpStats(quadrable::Quadrable &db, lmdb::txn &txn) {
    auto stats = db.stats(txn);

    std::cout << "numNodes:        " << stats.numNodes << "\n";
    std::cout << "numLeafNodes:    " << stats.numLeafNodes << "\n";
    std::cout << "numBranchNodes:  " << stats.numBranchNodes << "\n";
    std::cout << "numWitnessNodes: " << stats.numWitnessNodes << "\n";
    std::cout << "maxDepth:        " << stats.maxDepth << "\n";
    std::cout << "numBytes:        " << stats.numBytes << "\n";

    std::cout << std::flush;
}



static inline void dumpProof(const Proof &p) {
    std::cout << "ITEMS (" << p.strands.size() << "):\n";

    for (size_t i=0; i<p.strands.size(); i++) {
        auto &strand = p.strands[i];

        std::string strandType = strand.strandType == ProofStrand::Type::Leaf ? "Leaf" :
                               strand.strandType == ProofStrand::Type::WitnessLeaf ? "WitnessLeaf" :
                               strand.strandType == ProofStrand::Type::WitnessEmpty ? "WitnessEmpty" :
                               "?";

        std::cout << "  ITEM " << i << ": " << to_hex(strand.keyHash, true) << ")\n";
        std::cout << "    " << strandType << "  depth=" << strand.depth << "\n";

        if (strand.strandType == ProofStrand::Type::Leaf) {
            if (strand.key.size()) std::cout << "    Key: " << strand.key << "\n";
            std::cout << "    Val: " << strand.val << "\n";
        } else if (strand.strandType == ProofStrand::Type::WitnessLeaf) {
            std::cout << "    Val hash: " << to_hex(strand.val, true) << "\n";
        }
    }

    std::cout << "CMDS (" << p.cmds.size() << "):\n";

    for (size_t i=0; i<p.cmds.size(); i++) {
        auto &cmd = p.cmds[i];

        std::string op = cmd.op == ProofCmd::Op::HashEmpty ? "HashEmpty" :
                         cmd.op == ProofCmd::Op::HashProvided ? "HashProvided" :
                         cmd.op == ProofCmd::Op::Merge ? "Merge" :
                         "?";

        std::cout << "  CMD " << i << ": " << op << " -> " << cmd.nodeOffset << "\n";

        if (cmd.op == ProofCmd::Op::HashProvided) {
            std::cout << "    Sibling hash: " << to_hex(cmd.hash, true) << "\n";
        }
    }

    std::cout << std::flush;
}


}
