public:

struct Stats {
    uint64_t numNodes = 0;
    uint64_t numLeafNodes = 0;
    uint64_t numBranchNodes = 0;
    uint64_t numWitnessNodes = 0;

    uint64_t maxDepth = 0;
    uint64_t numBytes = 0;
};

Stats stats(lmdb::txn &txn) {
    Stats output;

    walkTree(txn, [&](ParsedNode &node, uint64_t depth){
        output.numNodes++;
        output.maxDepth = std::max(output.maxDepth, depth);
        output.numBytes += node.raw.size();

        if (node.nodeType == NodeType::Leaf) {
            output.numLeafNodes++;
        } else if (node.isBranch()) {
            output.numBranchNodes++;
        } else if (node.isWitness()) {
            output.numWitnessNodes++;
        }

        return true;
    });

    return output;
}
