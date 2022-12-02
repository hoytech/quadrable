public:

struct Diff {
    std::string keyHash;
    std::string key;
    std::string val; // new value if insertion, old value if deletion
    bool deletion = false;
};

std::vector<Diff> diff(lmdb::txn &txn, uint64_t nodeIdA, uint64_t nodeIdB) {
    std::vector<Diff> output;

    diffAux(txn, nodeIdA, nodeIdB, output);

    return output;
}


private:

void diffPush(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output, bool deletion) {
    std::string_view key;
    getLeafKey(txn, node.nodeId, key);

    output.emplace_back(Diff{
        std::string(node.leafKeyHash()),
        std::string(key),
        std::string(node.leafVal()),
        deletion,
    });
}

void diffPushAdd(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output) {
    diffPush(txn, node, output, false);
}

void diffPushDel(lmdb::txn &txn, ParsedNode &node, std::vector<Diff> &output) {
    diffPush(txn, node, output, true);
}

void diffWalk(lmdb::txn &txn, uint64_t nodeId, std::function<void(ParsedNode &)> cb) {
    walkTree(txn, nodeId, [&](ParsedNode &node, uint64_t depth){
        if (node.isWitnessAny()) throw quaderr("encountered witness during diffWalk");
        if (node.isLeaf()) cb(node);
        return true;
    });
}

void diffAux(lmdb::txn &txn, uint64_t nodeIdA, uint64_t nodeIdB, std::vector<Diff> &output) {
    if (nodeIdA == nodeIdB) return;

    ParsedNode nodeA(txn, dbi_node, nodeIdA);
    ParsedNode nodeB(txn, dbi_node, nodeIdB);

    if (nodeA.isWitnessAny() || nodeB.isWitnessAny()) throw quaderr("encountered witness during diff");

    if (nodeA.isBranch() && nodeB.isBranch()) {
        diffAux(txn, nodeA.leftNodeId, nodeB.leftNodeId, output);
        diffAux(txn, nodeA.rightNodeId, nodeB.rightNodeId, output);
    } else if (!nodeA.isBranch() && nodeB.isBranch()) {
        // All keys in B were added (except maybe if A is a leaf)
        bool foundLeaf = false;
        diffWalk(txn, nodeIdB, [&](ParsedNode &node){
            if (nodeA.isLeaf() && node.leafKeyHash() == nodeA.leafKeyHash()) {
                foundLeaf = true;
                if (node.leafVal() != nodeA.leafVal()) {
                    diffPushDel(txn, nodeA, output);
                    diffPushAdd(txn, node, output);
                }
            } else {
                diffPushAdd(txn, node, output);
            }
        });
        if (nodeA.isLeaf() && !foundLeaf) diffPushDel(txn, nodeA, output);
    } else if (nodeA.isBranch() && !nodeB.isBranch()) {
        // All keys in A were deleted (except maybe if B is a leaf)
        bool foundLeaf = false;
        diffWalk(txn, nodeIdA, [&](ParsedNode &node){
            if (nodeB.isLeaf() && node.leafKeyHash() == nodeB.leafKeyHash()) {
                foundLeaf = true;
                if (node.leafVal() != nodeB.leafVal()) {
                    diffPushDel(txn, nodeB, output);
                    diffPushAdd(txn, node, output);
                }
            } else {
                diffPushDel(txn, node, output);
            }
        });
        if (nodeB.isLeaf() && !foundLeaf) diffPushAdd(txn, nodeB, output);
    } else if (nodeA.isLeaf() && nodeB.isLeaf()) {
        if (nodeA.leafKeyHash() != nodeB.leafKeyHash() || nodeA.leafVal() != nodeB.leafVal()) {
            diffPushDel(txn, nodeA, output);
            diffPushAdd(txn, nodeB, output);
        }
    } else if (nodeA.isLeaf()) {
        diffPushDel(txn, nodeA, output);
    } else if (nodeB.isLeaf()) {
        diffPushAdd(txn, nodeB, output);
    }
}
