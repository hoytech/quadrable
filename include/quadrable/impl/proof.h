private:

struct ProofGenItem {
    uint64_t nodeId;
    uint64_t parentNodeId;
    ProofStrand strand;
};

using ProofGenItems = std::vector<ProofGenItem>;
using ProofHashes = std::map<Key, std::string>; // keyHash -> key
using ProofReverseNodeMap = std::map<uint64_t, uint64_t>; // child -> parent

public:

// Export interface

Proof exportProof(lmdb::txn &txn, const std::vector<std::string> &keys) {
    ProofHashes keyHashes;

    for (auto &key : keys) {
        keyHashes.emplace(Key::hash(key), key);
    }

    return exportProofAux(txn, keyHashes);
}

Proof exportProofRaw(lmdb::txn &txn, const std::vector<Key> &keys) {
    ProofHashes keyHashes;

    for (auto &key : keys) {
        keyHashes.emplace(key, "");
    }

    return exportProofAux(txn, keyHashes);
}

Proof exportProofRange(lmdb::txn &txn, const Key &begin, const Key &end) {
    auto headNodeId = getHeadNodeId(txn);

    return exportProofRange(txn, headNodeId, begin, end);
}

Proof exportProofRange(lmdb::txn &txn, uint64_t nodeId, const Key &begin, const Key &end) {
    ProofGenItems items;
    ProofReverseNodeMap reverseMap;
    Key currPath = Key::null();
    uint64_t depthLimit = std::numeric_limits<uint64_t>::max();
    bool expandLeaves = true;

    exportProofRangeAux(txn, 0, nodeId, 0, depthLimit, expandLeaves, currPath, begin, end, items, reverseMap);

    Proof output;

    output.cmds = exportProofCmds(txn, items, reverseMap, nodeId);

    for (auto &item : items) {
        output.strands.emplace_back(std::move(item.strand));
    }

    return output;
}




// Import interface

BuiltNode importProof(lmdb::txn &txn, Proof &proof, std::string expectedRoot = "") {
    if (getHeadNodeId(txn)) throw quaderr("can't importProof into non-empty head");

    auto rootNode = importProofInternal(txn, proof);

    if (expectedRoot.size() && rootNode.nodeHash != expectedRoot) throw quaderr("proof invalid");

    setHeadNodeId(txn, rootNode.nodeId);

    return rootNode;
}

BuiltNode mergeProof(lmdb::txn &txn, Proof &proof) {
    auto rootNode = importProofInternal(txn, proof);

    if (rootNode.nodeHash != root(txn)) throw quaderr("different roots, unable to merge proofs");

    auto updatedRoot = mergeProofInternal(txn, getHeadNodeId(txn), rootNode.nodeId);

    setHeadNodeId(txn, updatedRoot.nodeId);

    return rootNode;
}



private:


Proof exportProofAux(lmdb::txn &txn, ProofHashes &keyHashes) {
    auto headNodeId = getHeadNodeId(txn);

    ProofGenItems items;
    ProofReverseNodeMap reverseMap;

    exportProofAux(txn, 0, headNodeId, 0, keyHashes.begin(), keyHashes.end(), items, reverseMap);

    Proof output;

    output.cmds = exportProofCmds(txn, items, reverseMap, headNodeId);

    for (auto &item : items) {
        output.strands.emplace_back(std::move(item.strand));
    }

    return output;
}

void exportProofAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, ProofHashes::iterator begin, ProofHashes::iterator end, ProofGenItems &items, ProofReverseNodeMap &reverseMap) {
    if (begin == end) {
        return;
    }

    ParsedNode node(this, txn, nodeId);

    if (node.isEmpty()) {
        Key h = begin->first;
        h.keepPrefixBits(depth);

        items.emplace_back(ProofGenItem{
            nodeId,
            parentNodeId,
            ProofStrand{ ProofStrand::Type::WitnessEmpty, depth, h.str(), },
        });
    } else if (node.isLeaf()) {
        if (std::any_of(begin, end, [&](auto &i){ return i.first == node.leafKeyHash(); })) {
            if (node.nodeType == NodeType::WitnessLeaf) {
                throw quaderr("incomplete tree, missing leaf to make proof");
            }

            std::string_view leafKey;
            getLeafKey(txn, node.nodeId, leafKey);

            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofStrand{ ProofStrand::Type::Leaf, depth, std::string(node.leafKeyHash()), std::string(node.leafVal()), std::string(leafKey) },
            });
        } else {
            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofStrand{ ProofStrand::Type::WitnessLeaf, depth, std::string(node.leafKeyHash()), node.leafValHash(), },
            });
        }
    } else if (node.isBranch()) {
        auto middle = begin;
        while (middle != end && !middle->first.getBit(depth)) ++middle;

        assertDepth(depth);

        if (node.leftNodeId) reverseMap.emplace(node.leftNodeId, nodeId);
        if (node.rightNodeId) reverseMap.emplace(node.rightNodeId, nodeId);

        // If one side is empty and the other side has strands to prove, don't go down the empty side.
        // This avoids unnecessary empty witnesses, since they will be satisfied with HashEmpty cmds from the other side.

        if (node.leftNodeId || middle == end) exportProofAux(txn, depth+1, node.leftNodeId, nodeId, begin, middle, items, reverseMap);
        if (node.rightNodeId || begin == middle) exportProofAux(txn, depth+1, node.rightNodeId, nodeId, middle, end, items, reverseMap);
    } else if (node.nodeType == NodeType::Witness) {
        throw quaderr("encountered witness node: incomplete tree");
    } else {
        throw quaderr("unrecognized nodeType: ", int(node.nodeType));
    }
}



void exportProofRangeAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, uint64_t depthLimit, bool expandLeaves, Key &currPath, const Key &begin, const Key &end, ProofGenItems &items, ProofReverseNodeMap &reverseMap) {
    ParsedNode node(this, txn, nodeId);

    if (node.isEmpty()) {
        Key h = currPath;

        items.emplace_back(ProofGenItem{
            nodeId,
            parentNodeId,
            ProofStrand{ ProofStrand::Type::WitnessEmpty, depth, h.str(), },
        });
    } else if (node.isLeaf()) {
        if (node.nodeType == NodeType::WitnessLeaf) {
            throw quaderr("incomplete tree, missing leaf to make proof");
        }

        std::string_view leafKey;
        getLeafKey(txn, node.nodeId, leafKey);

        if (expandLeaves || node.leafVal().size() <= 32) {
            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofStrand{ ProofStrand::Type::Leaf, depth, std::string(node.leafKeyHash()), std::string(node.leafVal()), std::string(leafKey) },
            });
        } else {
            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofStrand{ ProofStrand::Type::WitnessLeaf, depth, std::string(node.leafKeyHash()), node.leafValHash(), },
            });
        }
    } else if (node.isBranch()) {
        assertDepth(depth);

        if (node.leftNodeId) reverseMap.emplace(node.leftNodeId, nodeId);
        if (node.rightNodeId) reverseMap.emplace(node.rightNodeId, nodeId);

        if (depthLimit == 0) {
            items.emplace_back(ProofGenItem{
                nodeId,
                parentNodeId,
                ProofStrand{ ProofStrand::Type::Witness, depth, currPath.str(), std::string(node.nodeHash()), },
            });

            return;
        }

        if (node.nodeType == NodeType::BranchBoth) depthLimit--;

        currPath.setBit(depth, 1);
        bool doLeft = begin < currPath;
        bool doRight = end >= currPath;

        currPath.setBit(depth, 0);
        if (doLeft) exportProofRangeAux(txn, depth+1, node.leftNodeId, nodeId, depthLimit, expandLeaves, currPath, begin, end, items, reverseMap);

        currPath.setBit(depth, 1);
        if (doRight) exportProofRangeAux(txn, depth+1, node.rightNodeId, nodeId, depthLimit, expandLeaves, currPath, begin, end, items, reverseMap);

        currPath.setBit(depth, 0);
    } else if (node.nodeType == NodeType::Witness) {
        throw quaderr("encountered witness node: incomplete tree");
    } else {
        throw quaderr("unrecognized nodeType: ", int(node.nodeType));
    }
}



struct GenProofItemAccum {
    size_t index;
    uint64_t depth;
    uint64_t nodeId;
    ssize_t next;

    uint64_t mergedOrder = 0;
    std::vector <ProofCmd> proofCmds;
};

std::vector<ProofCmd> exportProofCmds(lmdb::txn &txn, ProofGenItems &items, ProofReverseNodeMap &reverseMap, uint64_t headNodeId, uint64_t startDepth = 0) {
    if (items.size() == 0) return {};

    std::vector<GenProofItemAccum> accums;
    uint64_t maxDepth = 0;

    for (size_t i = 0; i < items.size(); i++) {
        auto &item = items[i];
        maxDepth = std::max(maxDepth, item.strand.depth);
        accums.emplace_back(GenProofItemAccum{ i, item.strand.depth, item.nodeId, static_cast<ssize_t>(i+1), });
    }

    accums.back().next = -1;
    uint64_t currDepth = maxDepth;
    uint64_t currMergeOrder = 0;

    // Complexity: O(N*D) = O(N*log(N))

    for (; currDepth > startDepth; currDepth--) {
        for (ssize_t i = 0; i != -1; i = accums[i].next) {
            auto &curr = accums[i];
            if (curr.depth != currDepth) continue;

            auto currParent = curr.nodeId ? reverseMap[curr.nodeId] : items[i].parentNodeId;

            if (curr.next != -1) {
                auto &next = accums[curr.next];

                auto nextParent = next.nodeId ? reverseMap[next.nodeId] : items[curr.next].parentNodeId;

                if (currParent == nextParent) {
                    curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::Merge, static_cast<uint64_t>(i), });
                    next.mergedOrder = currMergeOrder++;
                    curr.next = next.next;
                    curr.nodeId = currParent;
                    curr.depth--;
                    continue;
                }
            }

            ParsedNode parentNode(this, txn, currParent);
            uint64_t siblingNodeId = parentNode.leftNodeId == curr.nodeId ? parentNode.rightNodeId : parentNode.leftNodeId;

            if (siblingNodeId) {
                ParsedNode siblingNode(this, txn, siblingNodeId);
                curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashProvided, static_cast<uint64_t>(i), std::string(siblingNode.nodeHash()), });
            } else {
                curr.proofCmds.emplace_back(ProofCmd{ ProofCmd::Op::HashEmpty, static_cast<uint64_t>(i), });
            }

            curr.nodeId = currParent;
            curr.depth--;
        }
    }

    assert(accums[0].depth == startDepth);
    assert(accums[0].nodeId == headNodeId);
    assert(accums[0].next == -1);
    accums[0].mergedOrder = currMergeOrder;

    std::sort(accums.begin(), accums.end(), [](auto &a, auto &b){ return a.mergedOrder < b.mergedOrder; });

    std::vector<ProofCmd> proofCmds;
    for (auto &a : accums) {
        proofCmds.insert(proofCmds.end(), a.proofCmds.begin(), a.proofCmds.end());
    }

    return proofCmds;
}









struct ImportProofItemAccum {
    uint64_t depth;
    uint64_t nodeId;
    ssize_t next;
    Key keyHash;
    Key nodeHash;

    bool merged = false;
};

BuiltNode importProofInternal(lmdb::txn &txn, Proof &proof, uint64_t expectedDepth = 0) {
    std::vector<ImportProofItemAccum> accums;

    for (size_t i = 0; i < proof.strands.size(); i++) {
        auto &strand = proof.strands[i];
        auto keyHash = Key::existing(strand.keyHash);
        auto next = static_cast<ssize_t>(i+1);

        if (strand.strandType == ProofStrand::Type::Leaf) {
            auto info = BuiltNode::newLeaf(this, txn, keyHash, strand.val, strand.key);
            accums.emplace_back(ImportProofItemAccum{ strand.depth, info.nodeId, next, keyHash, info.nodeHash, });
        } else if (strand.strandType == ProofStrand::Type::WitnessLeaf) {
            auto info = BuiltNode::newWitnessLeaf(this, txn, keyHash, Key::existing(strand.val));
            accums.emplace_back(ImportProofItemAccum{ strand.depth, info.nodeId, next, keyHash, info.nodeHash, });
        } else if (strand.strandType == ProofStrand::Type::WitnessEmpty) {
            accums.emplace_back(ImportProofItemAccum{ strand.depth, 0, next, keyHash, Key::null(), });
        } else if (strand.strandType == ProofStrand::Type::Witness) {
            auto info = BuiltNode::newWitness(this, txn, Key::existing(strand.val));
            accums.emplace_back(ImportProofItemAccum{ strand.depth, info.nodeId, next, keyHash, info.nodeHash, });
        } else {
            throw quaderr("unrecognized ProofItem type: ", int(strand.strandType));
        }
    }

    if (accums.size() == 0) throw quaderr("empty proof");

    accums.back().next = -1;


    for (auto &cmd : proof.cmds) {
        if (cmd.nodeOffset >= proof.strands.size()) throw quaderr("nodeOffset in cmd is out of range");
        auto &accum = accums[cmd.nodeOffset];

        if (accum.merged) throw quaderr("strand already merged");
        if (accum.depth == 0) throw quaderr("node depth underflow");

        BuiltNode siblingInfo;

        if (cmd.op == ProofCmd::Op::HashProvided) {
            siblingInfo = BuiltNode::newWitness(this, txn, Key::existing(cmd.hash));
        } else if (cmd.op == ProofCmd::Op::HashEmpty) {
            siblingInfo = BuiltNode::empty();
        } else if (cmd.op == ProofCmd::Op::Merge) {
            if (accum.next < 0) throw quaderr("no nodes left to merge with");
            auto &accumNext = accums[accum.next];

            if (accum.depth != accumNext.depth) throw quaderr("merge depth mismatch");

            accum.next = accumNext.next;
            accumNext.merged = true;

            siblingInfo = BuiltNode::stubbed(accumNext.nodeId, accumNext.nodeHash);
        } else {
            throw quaderr("unrecognized ProofCmd op: ", int(cmd.op));
        }

        BuiltNode branchInfo;

        if (cmd.op == ProofCmd::Op::Merge || !accum.keyHash.getBit(accum.depth - 1)) {
            branchInfo = BuiltNode::newBranch(this, txn, BuiltNode::stubbed(accum.nodeId, accum.nodeHash), BuiltNode::stubbed(siblingInfo.nodeId, siblingInfo.nodeHash));
        } else {
            branchInfo = BuiltNode::newBranch(this, txn, BuiltNode::stubbed(siblingInfo.nodeId, siblingInfo.nodeHash), BuiltNode::stubbed(accum.nodeId, accum.nodeHash));
        }

        accum.depth--;
        accum.nodeId = branchInfo.nodeId;
        accum.nodeHash = branchInfo.nodeHash;
    }

    if (accums[0].next != -1) throw quaderr("not all proof strands were merged");
    if (accums[0].depth != expectedDepth) throw quaderr("proof didn't reach expected depth");

    return BuiltNode::stubbed(accums[0].nodeId, accums[0].nodeHash);
}

BuiltNode mergeProofInternal(lmdb::txn &txn, uint64_t origNodeId, uint64_t newNodeId) {
    ParsedNode origNode(this, txn, origNodeId);
    ParsedNode newNode(this, txn, newNodeId);

    if ((origNode.isWitnessAny() && !newNode.isWitnessAny()) ||
        (origNode.nodeType == NodeType::Witness && newNode.nodeType == NodeType::WitnessLeaf)) {

        // FIXME: if a key is available on one of the nodes and key tracking is desired, we could import it
        return BuiltNode::reuse(newNode);
    } else if (origNode.isBranch() && newNode.isBranch()) {
        auto newLeftNode = mergeProofInternal(txn, origNode.leftNodeId, newNode.leftNodeId);
        auto newRightNode = mergeProofInternal(txn, origNode.rightNodeId, newNode.rightNodeId);

        if (origNode.leftNodeId == newLeftNode.nodeId && origNode.rightNodeId == newRightNode.nodeId) {
            return BuiltNode::reuse(origNode);
        } else if (newNode.leftNodeId == newLeftNode.nodeId && newNode.rightNodeId == newRightNode.nodeId) {
            return BuiltNode::reuse(newNode);
        } else {
            return BuiltNode::newBranch(this, txn, newLeftNode, newRightNode);
        }
    } else {
        return BuiltNode::reuse(origNode);
    }
}
