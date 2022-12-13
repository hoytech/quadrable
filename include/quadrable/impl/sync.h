public:

struct SyncRequest {
    Key path;
    uint64_t startDepth;
    uint64_t depthLimit;
    bool expandLeaves;
};

using SyncRequests = std::vector<SyncRequest>;
using SyncResponses = std::vector<Proof>;


SyncResponses handleSyncRequests(lmdb::txn &txn, uint64_t nodeId, SyncRequests &reqs, uint64_t bytesBudget) {
    if (reqs.size() == 0) throw quaderr("empty fragments request");

    for (size_t i = 1; i < reqs.size() - 1; i++) {
        if (reqs[i].path <= reqs[i - 1].path) throw quaderr("fragments request out of order");
    }

    SyncResponses resps;
    Key currPath = Key::null();

    handleSyncRequestsAux(txn, 0, nodeId, 0, currPath, reqs.begin(), reqs.end(), resps);

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

    SyncRequests getReqs(lmdb::txn &txn) {
        if (!inited) {
            inited = true;
            return { SyncRequest{
                Key::null(),
                0,
                4,
                false,
            } };
        }

        return db->reconcileTrees(txn, nodeIdOurs, nodeIdShadow);
    }

    void addResps(lmdb::txn &txn, SyncRequests &reqs, SyncResponses &resps) {
        if (resps.size()) inited = true;

        auto newNodeShadow = db->importSyncResponses(txn, nodeIdShadow, reqs, resps);
        // FIXME: make sure hashes match
        nodeIdShadow = newNodeShadow.nodeId;
    }
};




enum class DiffType {
    Added = 0,
    Deleted = 1,
    Changed = 2,
};

using SyncedDiffCb = std::function<void(DiffType, const ParsedNode &)>;

void syncedDiff(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs, const SyncedDiffCb &cb) {
    ParsedNode nodeOurs(txn, dbi_node, nodeIdOurs);
    ParsedNode nodeTheirs(txn, dbi_node, nodeIdTheirs);

    if (nodeOurs.nodeHash() == nodeTheirs.nodeHash()) return;

    if (nodeOurs.isBranch() && nodeTheirs.isBranch()) {
        syncedDiff(txn, nodeOurs.leftNodeId, nodeTheirs.leftNodeId, cb);
        syncedDiff(txn, nodeOurs.rightNodeId, nodeTheirs.rightNodeId, cb);
    } else if (nodeTheirs.isBranch()) {
        bool found = false;
        syncedDiffAux(txn, nodeTheirs.leftNodeId, nodeOurs, found, cb);
        syncedDiffAux(txn, nodeTheirs.rightNodeId, nodeOurs, found, cb);
        if (!found) cb(DiffType::Deleted, nodeOurs);
    } else if (nodeOurs.isBranch()) {
        bool found = false;
        syncedDiffAux(txn, nodeOurs.leftNodeId, nodeTheirs, found, cb);
        syncedDiffAux(txn, nodeOurs.rightNodeId, nodeTheirs, found, cb);
        if (!found) cb(DiffType::Added, nodeTheirs);
    } else {
        if (nodeOurs.isLeaf() && nodeTheirs.isLeaf() && nodeOurs.leafKeyHash() == nodeTheirs.leafKeyHash()) {
            cb(DiffType::Changed, nodeTheirs);
        } else {
            if (nodeOurs.nodeId) cb(DiffType::Deleted, nodeOurs);
            if (nodeTheirs.nodeId) cb(DiffType::Added, nodeTheirs);
        }
    }
}

void syncedDiffAux(lmdb::txn &txn, uint64_t nodeId, ParsedNode &searchNode, bool &found, const SyncedDiffCb &cb) {
    ParsedNode node(txn, dbi_node, nodeId);

    if (node.isBranch()) {
        syncedDiffAux(txn, node.leftNodeId, searchNode, found, cb);
        syncedDiffAux(txn, node.rightNodeId, searchNode, found, cb);
    } else {
        if (node.nodeHash() == searchNode.nodeHash()) found = true;
        else if (node.nodeId != 0) cb(DiffType::Added, node); // FIXME: needs to be Deleted sometimes right?
    }
}



private:


void handleSyncRequestsAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, Key &currPath, SyncRequests::iterator begin, SyncRequests::iterator end, SyncResponses &resps) {
    if (begin == end) {
        return;
    }

    ParsedNode node(txn, dbi_node, nodeId);

    // FIXME: detect and report error condition where a fragment ends on the path of another fragment
    if (begin != end && std::next(begin) == end && begin->startDepth == depth) {
        resps.emplace_back(exportProofFragment(txn, nodeId, currPath, *begin));
        return;
    }

    if (node.isBranch()) {
        auto middle = begin;
        while (middle != end && !middle->path.getBit(depth)) ++middle;

        assertDepth(depth);

        if (node.leftNodeId || middle == end) {
            handleSyncRequestsAux(txn, depth+1, node.leftNodeId, nodeId, currPath, begin, middle, resps);
        }

        if (node.rightNodeId || begin == middle) {
            currPath.setBit(depth, 1);
            handleSyncRequestsAux(txn, depth+1, node.rightNodeId, nodeId, currPath, middle, end, resps);
            currPath.setBit(depth, 0);
        }
    } else {
        throw quaderr("fragment path not available");
    }
}

Proof exportProofFragment(lmdb::txn &txn, uint64_t nodeId, Key currPath, const SyncRequest &req) {
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




struct SyncRequestAndResponse {
    SyncRequest *req;
    Proof *proof;
};

using SyncRequestAndResponses = std::vector<SyncRequestAndResponse>;

BuiltNode importSyncResponses(lmdb::txn &txn, uint64_t nodeId, SyncRequests &reqs, SyncResponses &resps) {
    SyncRequestAndResponses fragItems;

    if (resps.size() > reqs.size()) throw quaderr("too many resps when importing fragments");

    for (size_t i = 0; i < resps.size(); i++) {
        fragItems.emplace_back(SyncRequestAndResponse{ &reqs[i], &resps[i] });
    }

    if (fragItems.size() == 0) throw quaderr("no fragments to import");

    return importSyncResponsesAux(txn, nodeId, 0, fragItems.begin(), fragItems.end());
}

BuiltNode importSyncResponsesAux(lmdb::txn &txn, uint64_t nodeId, uint64_t depth, SyncRequestAndResponses::iterator begin, SyncRequestAndResponses::iterator end) {
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
            newLeftNode = importSyncResponsesAux(txn, origNode.leftNodeId, depth + 1, begin, middle);
        } else {
            newLeftNode = BuiltNode::reuse(ParsedNode(txn, dbi_node, origNode.leftNodeId));
        }

        if (origNode.rightNodeId || begin == middle) {
            newRightNode = importSyncResponsesAux(txn, origNode.rightNodeId, depth + 1, middle, end);
        } else {
            newRightNode = BuiltNode::reuse(ParsedNode(txn, dbi_node, origNode.rightNodeId));
        }

        return BuiltNode::newBranch(this, txn, newLeftNode, newRightNode);
    } else {
        return BuiltNode::reuse(origNode);
    }
}



SyncRequests reconcileTrees(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs) {
    SyncRequests output;

    Key currPath = Key::null();

    reconcileTreesAux(txn, nodeIdOurs, nodeIdTheirs, 0, currPath, output);

    return output;
}

void reconcileTreesAux(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs, uint64_t depth, Key &currPath, SyncRequests &output) {
    ParsedNode nodeOurs(txn, dbi_node, nodeIdOurs);
    ParsedNode nodeTheirs(txn, dbi_node, nodeIdTheirs);

    if (nodeOurs.nodeHash() == nodeTheirs.nodeHash()) return;

    if (nodeTheirs.isBranch()) {
        reconcileTreesAux(txn, nodeOurs.isBranch() ? nodeOurs.leftNodeId : nodeIdOurs, nodeTheirs.leftNodeId, depth+1, currPath, output);
        currPath.setBit(depth, 1);
        reconcileTreesAux(txn, nodeOurs.isBranch() ? nodeOurs.rightNodeId : nodeIdOurs, nodeTheirs.rightNodeId, depth+1, currPath, output);
        currPath.setBit(depth, 0);
    } else if (nodeTheirs.isWitnessLeaf()) {
        output.emplace_back(SyncRequest{
            currPath,
            depth,
            1,
            true,
        });
    } else if (nodeTheirs.isWitness()) {
        output.emplace_back(SyncRequest{
            currPath,
            depth,
            4,
            false,
        });
    }
}
