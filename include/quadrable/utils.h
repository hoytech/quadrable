#pragma once

#include <iostream>

#include "quadrable.h"

#include "hoytech/hex.h"

using hoytech::to_hex;



namespace quadrable {


static inline std::string renderUnknown(std::string_view hash) {
    return std::string("H(?)=0x") + to_hex(hash.substr(0, 6)) + "...";
}


static inline void dumpDbAux(quadrable::Quadrable &db, lmdb::txn &txn, uint64_t nodeId, size_t depth) {
    quadrable::ParsedNode node(txn, db.dbi_node, nodeId);

    std::cout << std::string(depth*2, ' '); 

    std::cout << nodeId << ": ";
    std::cout << to_hex(node.nodeHash(), true) << ": ";

    if (node.nodeType == quadrable::NodeType::Empty) {
        std::cout << "empty";
        std::cout << "\n";
    } else if (node.nodeType == quadrable::NodeType::Leaf) {
        std::cout << "leaf: " << to_hex(node.leafKeyHash(), true) << " val = " << node.leafVal();
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



static inline void dumpProof(Proof &p) {
    std::cout << "ITEMS (" << p.elems.size() << "):\n";

    for (size_t i=0; i<p.elems.size(); i++) {
        auto &elem = p.elems[i];

        std::string proofType = elem.proofType == ProofElem::Type::Leaf ? "Leaf" :
                                elem.proofType == ProofElem::Type::WitnessLeaf ? "WitnessLeaf" :
                                elem.proofType == ProofElem::Type::WitnessEmpty ? "WitnessEmpty" :
                                "?";

        std::cout << "  ITEM " << i << ": " << to_hex(elem.keyHash, true) << ")\n";
        std::cout << "    " << proofType << "  depth=" << elem.depth << "\n";

        if (elem.proofType == ProofElem::Type::Leaf) {
            std::cout << "    Val: " << elem.val << "\n";
        } else if (elem.proofType == ProofElem::Type::WitnessLeaf) {
            std::cout << "    Val hash: " << to_hex(elem.val, true) << "\n";
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
            std::cout << "    Sibling hash: " << to_hex(cmd.h.sv(), true) << "\n";
        }
    }

    std::cout << std::flush;
}


static inline void dumpProofSimple(std::vector<std::string> &proof) {
    for (auto &p : proof) {
        std::cout << to_hex(p, true) << "\n";
    }

    std::cout << std::flush;
}


}
