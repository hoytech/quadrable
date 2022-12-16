public:


SyncResponses handleSyncRequests(lmdb::txn &txn, uint64_t nodeId, SyncRequests &reqs, uint64_t bytesBudget = std::numeric_limits<uint64_t>::max()) {
    if (bytesBudget == 0) throw quaderr("bytesBudget can't be 0");
    if (reqs.size() == 0) throw quaderr("empty fragments request");

    for (size_t i = 1; i < reqs.size() - 1; i++) {
        if (reqs[i].path <= reqs[i - 1].path) throw quaderr("fragments request out of order");
    }

    SyncResponses resps;
    Key currPath = Key::null();

    handleSyncRequestsAux(txn, 0, nodeId, 0, currPath, reqs.begin(), reqs.end(), resps, bytesBudget);

    return resps;
}



enum class DiffType {
    Added = 0,
    Deleted = 1,
    Changed = 2,
};

using SyncedDiffCb = std::function<void(DiffType, const ParsedNode &)>;

class Sync {
  public:
    Quadrable *db;
    uint64_t nodeIdLocal;
    uint64_t nodeIdShadow;
    uint64_t initialRequestDepth = 4;
    uint64_t laterRequestDepth = 4;

  private:
    bool inited = false;
    std::unordered_set<uint64_t> finishedNodes;

  public:
    Sync(Quadrable *db_, lmdb::txn &txn, uint64_t nodeIdLocal_) : db(db_), nodeIdLocal(nodeIdLocal_) {
        auto node = BuiltNode::newWitness(db, txn, Key::null()); // initial stub node
        nodeIdShadow = node.nodeId;
    }

    SyncRequests getReqs(lmdb::txn &txn, uint64_t bytesBudget = std::numeric_limits<uint64_t>::max()) {
        if (bytesBudget == 0) throw quaderr("bytesBudget can't be 0");

        if (!inited) {
            return { SyncRequest{
                Key::null(),
                0,
                initialRequestDepth,
                false,
            } };
        }

        SyncRequests output;

        Key currPath = Key::null();

        reconcileTrees(txn, nodeIdLocal, nodeIdShadow, 0, currPath, bytesBudget, output);

        return output;
    }

    void addResps(lmdb::txn &txn, SyncRequests &reqs, SyncResponses &resps) {
        auto newNodeShadow = db->importSyncResponses(txn, nodeIdShadow, reqs, resps);

        if (inited && db->root(txn, nodeIdShadow) != db->root(txn, newNodeShadow.nodeId)) throw quaderr("hash mismatch after addResps");

        inited = true;
        nodeIdShadow = newNodeShadow.nodeId;
    }

    void diff(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs, const SyncedDiffCb &cb) {
        ParsedNode nodeOurs(db, txn, nodeIdOurs);
        ParsedNode nodeTheirs(db, txn, nodeIdTheirs);

        if (nodeOurs.nodeHash() == nodeTheirs.nodeHash()) return;

        if (nodeOurs.isBranch() && nodeTheirs.isBranch()) {
            diff(txn, nodeOurs.leftNodeId, nodeTheirs.leftNodeId, cb);
            diff(txn, nodeOurs.rightNodeId, nodeTheirs.rightNodeId, cb);
        } else if (nodeTheirs.isBranch()) {
            ParsedNode found(db, txn, 0);
            diffAux(txn, nodeTheirs.leftNodeId, nodeOurs, found, DiffType::Added, cb);
            diffAux(txn, nodeTheirs.rightNodeId, nodeOurs, found, DiffType::Added, cb);
            if (nodeOurs.nodeId) {
                if (found.nodeId) {
                    if (found.nodeHash() != nodeOurs.nodeHash()) cb(DiffType::Changed, found);
                } else {
                    cb(DiffType::Deleted, nodeOurs);
                }
            }
        } else if (nodeOurs.isBranch()) {
            ParsedNode found(db, txn, 0);
            diffAux(txn, nodeOurs.leftNodeId, nodeTheirs, found, DiffType::Deleted, cb);
            diffAux(txn, nodeOurs.rightNodeId, nodeTheirs, found, DiffType::Deleted, cb);
            if (nodeTheirs.nodeId) {
                if (found.nodeId) {
                    if (found.nodeHash() != nodeTheirs.nodeHash()) cb(DiffType::Changed, nodeTheirs);
                } else {
                    cb(DiffType::Added, nodeTheirs);
                }
            }
        } else {
            if (nodeOurs.isLeaf() && nodeTheirs.isLeaf() && nodeOurs.leafKeyHash() == nodeTheirs.leafKeyHash()) {
                cb(DiffType::Changed, nodeTheirs);
            } else {
                if (nodeOurs.nodeId) cb(DiffType::Deleted, nodeOurs);
                if (nodeTheirs.nodeId) cb(DiffType::Added, nodeTheirs);
            }
        }
    }

  private:

    void diffAux(lmdb::txn &txn, uint64_t nodeId, ParsedNode &searchNode, ParsedNode &found, DiffType dt, const SyncedDiffCb &cb) {
        ParsedNode node(db, txn, nodeId);

        if (node.isBranch()) {
            diffAux(txn, node.leftNodeId, searchNode, found, dt, cb);
            diffAux(txn, node.rightNodeId, searchNode, found, dt, cb);
        } else {
            if (searchNode.nodeId != 0 && node.nodeId != 0 && node.leafKeyHash() == searchNode.leafKeyHash()) found = node;
            else if (node.nodeId != 0) cb(dt, node);
        }
    }

    void reconcileTrees(lmdb::txn &txn, uint64_t nodeIdOurs, uint64_t nodeIdTheirs, uint64_t depth, Key &currPath, uint64_t &bytesBudget, SyncRequests &output) {
        ParsedNode nodeOurs(db, txn, nodeIdOurs);
        ParsedNode nodeTheirs(db, txn, nodeIdTheirs);

        if (nodeOurs.nodeHash() == nodeTheirs.nodeHash() || finishedNodes.count(nodeIdOurs) || bytesBudget == 0) return;

        auto reduceBytesBudget = [&]{
            uint64_t estimate = 16;
            if (bytesBudget > estimate) bytesBudget -= estimate;
            else bytesBudget = 0;
        };

        if (nodeTheirs.isBranch()) {
            uint64_t outputSizeBefore = output.size();

            reconcileTrees(txn, nodeOurs.isBranch() ? nodeOurs.leftNodeId : nodeIdOurs, nodeTheirs.leftNodeId, depth+1, currPath, bytesBudget, output);
            currPath.setBit(depth, 1);
            reconcileTrees(txn, nodeOurs.isBranch() ? nodeOurs.rightNodeId : nodeIdOurs, nodeTheirs.rightNodeId, depth+1, currPath, bytesBudget, output);
            currPath.setBit(depth, 0);

            if (output.size() == outputSizeBefore && nodeIdOurs) finishedNodes.insert(nodeIdOurs);
        } else if (nodeTheirs.isWitnessLeaf()) {
            output.emplace_back(SyncRequest{
                currPath,
                depth,
                1,
                true,
            });

            reduceBytesBudget();
        } else if (nodeTheirs.isWitness()) {
            output.emplace_back(SyncRequest{
                currPath,
                depth,
                laterRequestDepth,
                false,
            });

            reduceBytesBudget();
        }
    }

};



private:


void handleSyncRequestsAux(lmdb::txn &txn, uint64_t depth, uint64_t nodeId, uint64_t parentNodeId, Key &currPath, SyncRequests::iterator begin, SyncRequests::iterator end, SyncResponses &resps, uint64_t &bytesBudget) {
    if (begin == end || bytesBudget == 0) {
        return;
    }

    ParsedNode node(this, txn, nodeId);

    // If a fragment ends on the path of another fragment in the SyncRequests list,
    // then the following will terminate early and the results will be incorrect. So,
    // it is important the sync requests creator not create requests like this.

    if (begin != end && std::next(begin) == end && begin->startDepth == depth) {
        resps.emplace_back(exportProofFragment(txn, nodeId, currPath, *begin));
        uint64_t estimate = estimateSizeProof(resps.back());
        if (bytesBudget > estimate) bytesBudget -= estimate;
        else bytesBudget = 0;
        return;
    }

    if (node.isBranch()) {
        auto middle = begin;
        while (middle != end && !middle->path.getBit(depth)) ++middle;

        assertDepth(depth);

        if (node.leftNodeId || middle == end) {
            handleSyncRequestsAux(txn, depth+1, node.leftNodeId, nodeId, currPath, begin, middle, resps, bytesBudget);
        }

        if (node.rightNodeId || begin == middle) {
            currPath.setBit(depth, 1);
            handleSyncRequestsAux(txn, depth+1, node.rightNodeId, nodeId, currPath, middle, end, resps, bytesBudget);
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
    ParsedNode origNode(this, txn, nodeId);

    if (begin != end && std::next(begin) == end && begin->req->startDepth == depth) {
        if (!origNode.isWitnessAny()) throw quaderr("import proof fragment tried to expand non-witness, ", nodeId);

        auto newNode = importProofInternal(txn, *begin->proof, depth);

        if (newNode.nodeHash != origNode.nodeHash()) {
            bool isInitialStubNode = depth == 0 && origNode.nodeHash() == Key::null().sv() && origNode.isWitness();
            if (!isInitialStubNode) throw quaderr("import proof fragment incompatible tree");
        }

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
            newLeftNode = BuiltNode::reuse(ParsedNode(this, txn, origNode.leftNodeId));
        }

        if (origNode.rightNodeId || begin == middle) {
            newRightNode = importSyncResponsesAux(txn, origNode.rightNodeId, depth + 1, middle, end);
        } else {
            newRightNode = BuiltNode::reuse(ParsedNode(this, txn, origNode.rightNodeId));
        }

        return BuiltNode::newBranch(this, txn, newLeftNode, newRightNode);
    } else {
        return BuiltNode::reuse(origNode);
    }
}





uint64_t estimateSizeProof(const Proof &proof) {
    uint64_t output = 0;

    output += proof.strands.size() * 10;

    for (const auto &strand : proof.strands) {
        output += strand.val.size();
        output += strand.key.size();
    }

    output += proof.cmds.size();

    for (const auto &cmd : proof.cmds) {
        if (cmd.op == ProofCmd::Op::HashProvided) output += 32;
    }

    return output;
}
