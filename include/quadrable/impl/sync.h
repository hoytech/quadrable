public:

struct ProofFragmentRequest {
    Key path;
    uint64_t startDepth;
    uint64_t depthLimit;
    bool expandLeaves;
};

using ProofFragmentRequests = std::vector<ProofFragmentRequest>;
using ProofFragmentResponses = std::vector<Proof>;


ProofFragmentResponses exportProofFragments(lmdb::txn &txn, uint64_t nodeId, ProofFragmentRequests &reqs, uint64_t bytesBudget) {
    if (reqs.size() == 0) throw quaderr("empty fragments request");

    for (size_t i = 1; i < reqs.size() - 1; i++) {
        if (reqs[i].path <= reqs[i - 1].path) throw quaderr("fragments request out of order");
    }

    ProofFragmentResponses resps;
    Key currPath = Key::null();

    exportProofFragmentsAux(txn, 0, nodeId, 0, currPath, reqs.begin(), reqs.end(), resps);

    return resps;
}


class Sync {
  public:
    Quadrable *db;
    uint64_t nodeIdOurs;
    uint64_t nodeIdShadow;
    bool inited = false;

    Sync(Quadrable *db_, lmdb::txn &txn, uint64_t nodeIdOurs_, const Key &hashShadow) : db(db_), nodeIdOurs(nodeIdOurs_) {
        auto node = BuiltNode::newWitness(db, txn, hashShadow);
        nodeIdShadow = node.nodeId;
    }

    ProofFragmentRequests getReqs(lmdb::txn &txn) {
        if (!inited) {
            inited = true;
            return { ProofFragmentRequest{
                Key::null(),
                0,
                4,
                false,
            } };
        }

        return db->reconcileTrees(txn, nodeIdOurs, nodeIdShadow);
    }

    void addResps(lmdb::txn &txn, ProofFragmentRequests &reqs, ProofFragmentResponses &resps) {
        if (resps.size()) inited = true;

        auto newNodeShadow = db->importProofFragments(txn, nodeIdShadow, reqs, resps);
        // FIXME: make sure hashes match
        nodeIdShadow = newNodeShadow.nodeId;
    }
};





private:


void exportProofFragmentsAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, Key &currPath, ProofFragmentRequests::iterator begin, ProofFragmentRequests::iterator end, ProofFragmentResponses &resps) {
    if (begin == end) {
        return;
    }

    ParsedNode node(txn, dbi_node, nodeId);

    // FIXME: detect and report error condition where a fragment ends on the path of another fragment
    if (begin != end && std::next(begin) == end && begin->startDepth == depth) {
        resps.emplace_back(exportProofFragmentSingle(txn, nodeId, currPath, *begin));
        return;
    }

    if (node.isBranch()) {
        auto middle = begin;
        while (middle != end && !middle->path.getBit(depth)) ++middle;

        assertDepth(depth);

        if (node.leftNodeId || middle == end) {
            exportProofFragmentsAux(txn, depth+1, node.leftNodeId, nodeId, currPath, begin, middle, resps);
        }

        if (node.rightNodeId || begin == middle) {
            currPath.setBit(depth, 1);
            exportProofFragmentsAux(txn, depth+1, node.rightNodeId, nodeId, currPath, middle, end, resps);
            currPath.setBit(depth, 0);
        }
    } else {
        throw quaderr("fragment path not available");
    }
}

Proof exportProofFragmentSingle(lmdb::txn &txn, uint64_t nodeId, Key currPath, const ProofFragmentRequest &req) {
    uint64_t depth = req.startDepth;

    currPath.keepPrefixBits(depth);

    ProofGenItems items;
    ProofReverseNodeMap reverseMap;

    exportProofRangeAux(txn, depth, nodeId, 0, req.depthLimit, req.expandLeaves, currPath, Key::null(), Key::max(), items, reverseMap);

    Proof output;

    output.cmds = exportProofCmds(txn, items, reverseMap, nodeId, depth);

    for (auto &item : items) {
        output.strands.emplace_back(std::move(item.strand));
    }

    return output;
}




struct ProofFragmentItem {
    ProofFragmentRequest *req;
    Proof *proof;
};

using ProofFragmentItems = std::vector<ProofFragmentItem>;

BuiltNode importProofFragments(lmdb::txn &txn, uint64_t nodeId, ProofFragmentRequests &reqs, ProofFragmentResponses &resps) {
    ProofFragmentItems fragItems;

    if (resps.size() > reqs.size()) throw quaderr("too many resps when importing fragments");

    for (size_t i = 0; i < resps.size(); i++) {
        fragItems.emplace_back(ProofFragmentItem{ &reqs[i], &resps[i] });
    }

    if (fragItems.size() == 0) throw quaderr("no fragments to import");

    return importProofFragmentsAux(txn, nodeId, 0, fragItems.begin(), fragItems.end());
}

BuiltNode importProofFragmentsAux(lmdb::txn &txn, uint64_t nodeId, uint64_t depth, ProofFragmentItems::iterator begin, ProofFragmentItems::iterator end) {
    ParsedNode origNode(txn, dbi_node, nodeId);

    if (begin != end && std::next(begin) == end && begin->req->startDepth == depth) {
        if (!origNode.isWitnessAny()) throw quaderr("import proof fragment tried to expand non-witness, ", nodeId);

        auto newNode = importProofInternal(txn, *begin->proof, depth);

        if (newNode.nodeHash != origNode.nodeHash()) throw quaderr("import proof fragment incompatible tree");

        return newNode;
    }

    if (origNode.isBranch()) {
        auto middle = begin;
        while (middle != end && !middle->req->path.getBit(depth)) ++middle;

        assertDepth(depth);

        BuiltNode newLeftNode, newRightNode;

        if (origNode.leftNodeId || middle == end) {
            newLeftNode = importProofFragmentsAux(txn, origNode.leftNodeId, depth + 1, begin, middle);
        } else {
            newLeftNode = BuiltNode::reuse(ParsedNode(txn, dbi_node, origNode.leftNodeId));
        }

        if (origNode.rightNodeId || begin == middle) {
            newRightNode = importProofFragmentsAux(txn, origNode.rightNodeId, depth + 1, middle, end);
        } else {
            newRightNode = BuiltNode::reuse(ParsedNode(txn, dbi_node, origNode.rightNodeId));
        }

        return BuiltNode::newBranch(this, txn, newLeftNode, newRightNode);
    } else {
        return BuiltNode::reuse(origNode);
    }
}



ProofFragmentRequests reconcileTrees(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs) {
    ProofFragmentRequests output;

    Key currPath = Key::null();

    reconcileTreesAux(txn, nodeIdOurs, nodeIdTheirs, 0, currPath, output);

    return output;
}

void reconcileTreesAux(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs, uint64_t depth, Key &currPath, ProofFragmentRequests &output) {
    ParsedNode nodeOurs(txn, dbi_node, nodeIdOurs);
    ParsedNode nodeTheirs(txn, dbi_node, nodeIdTheirs);

    if (nodeOurs.nodeHash() == nodeTheirs.nodeHash()) return;

    if (nodeTheirs.isBranch()) {
        reconcileTreesAux(txn, nodeOurs.isBranch() ? nodeOurs.leftNodeId : nodeIdOurs, nodeTheirs.leftNodeId, depth+1, currPath, output);
        currPath.setBit(depth, 1);
        reconcileTreesAux(txn, nodeOurs.isBranch() ? nodeOurs.rightNodeId : nodeIdOurs, nodeTheirs.rightNodeId, depth+1, currPath, output);
        currPath.setBit(depth, 0);
    } else if (nodeTheirs.isWitnessLeaf()) {
        output.emplace_back(ProofFragmentRequest{
            currPath,
            depth,
            1,
            true,
        });
    } else if (nodeTheirs.isWitness()) {
        output.emplace_back(ProofFragmentRequest{
            currPath,
            depth,
            4,
            false,
        });
    }
}
