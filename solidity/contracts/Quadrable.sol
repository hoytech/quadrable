pragma solidity ^0.6.0;

//import "@nomiclabs/buidler/console.sol";


// Strand state (128 bytes):
//     uint256: [0 padding...] [1 byte: depth] [1 byte: merged] [4 bytes: next] [4 bytes: nodeAddr]
//     [32 bytes: keyHash]
//     [64 bytes: possibly containing leaf node for this strand]

// Node (64 bytes):
//     uint256 nodeContents: [0 padding...] [nodeType specific (see below)] [1 byte: nodeType]
//                Leaf: [4 bytes: valAddr] [4 bytes: valLen] [4 bytes: keyHashAddr]
//         WitnessLeaf: [4 bytes: keyHashAddr]
//             Witness: unused
//              Branch: [4 bytes: parentNodeAddr] [4 bytes: leftNodeAddr] [4 bytes: rightNodeAddr]
//     bytes32 nodeHash



library Quadrable {
    // Proof import

    enum StrandType {
        Leaf, // = 0,
        Invalid, // = 1,
        WitnessLeaf, // = 2,
        WitnessEmpty // = 3,
    }

    enum NodeType { // Internal values (different from C++ implementation)
        Empty, // = 0
        Leaf, // = 1
        Witness, // = 2
        WitnessLeaf, // = 3
        Branch // = 4
    }

    struct ProofState {
        bytes encoded;

        uint256 numStrands;
        uint256 strandStateAddr; // memory address where strand states start
        uint256 startOfCmds; // offset within encoded where cmds start

        uint256 rootNodeAddr;
    }

    function saveStrandState(ProofState memory proof, uint256 strandIndex, uint256 newStrandState) private pure {
        uint256 strandStateAddr = proof.strandStateAddr;

        assembly {
            let addr := add(strandStateAddr, shl(7, strandIndex)) // strandIndex * 128
            mstore(addr, newStrandState)
        }
    }

    function getStrandState(ProofState memory proof, uint256 strandIndex) private pure returns (uint256 strandState) {
        uint256 strandStateAddr = proof.strandStateAddr;

        assembly {
            let addr := add(strandStateAddr, shl(7, strandIndex)) // strandIndex * 128
            strandState := mload(addr)
        }
    }

    function getStrandKeyHash(ProofState memory proof, uint256 strandIndex) private pure returns (bytes32 keyHash) {
        uint256 strandStateAddr = proof.strandStateAddr;

        assembly {
            let addr := add(strandStateAddr, shl(7, strandIndex)) // strandIndex * 128
            keyHash := mload(add(addr, 32))
        }
    }

    function packStrandState(uint256 depth, uint256 merged, uint256 next, uint256 nodeAddr) private pure returns (uint256 strandState) {
        strandState = (depth << (9*8)) |
                      (merged << (8*8)) |
                      (next << (4*8)) |
                      nodeAddr;
    }

    function unpackStrandState(uint256 strandState) private pure returns (uint256 depth, uint256 merged, uint256 next, uint256 nodeAddr) {
        depth = strandState >> (9*8);
        merged = (strandState >> (8*8)) & 0xFF;
        next = (strandState >> (4*8)) & 0xFFFFFFFF;
        nodeAddr = strandState & 0xFFFFFFFF;
    }

    function strandStateDepth(uint256 strandState) private pure returns (uint256) {
        return (strandState >> (9*8)) & 0xFF;
    }

    function strandStateNext(uint256 strandState) private pure returns (uint256) {
        return (strandState >> (4*8)) & 0xFFFFFFFF;
    }

    function strandStateNodeAddr(uint256 strandState) private pure returns (uint256) {
        return strandState & 0xFFFFFFFF;
    }

    function buildNodeWitness(bytes32 nodeHash) private pure returns (uint256 nodeAddr) {
        uint256 nodeContents = uint256(NodeType.Witness);

        assembly {
            nodeAddr := mload(0x40)
            mstore(nodeAddr, nodeContents)
            mstore(add(nodeAddr, 32), nodeHash)
            mstore(0x40, add(nodeAddr, 64))
        }
    }

    function buildNodeBranch(uint256 leftNodeAddr, uint256 rightNodeAddr) private pure returns (uint256 nodeAddr) {
        uint256 nodeContents = (leftNodeAddr << (5*8)) | (rightNodeAddr << (1*8)) | uint256(NodeType.Branch);

        assembly {
            nodeAddr := mload(0x40)
            mstore(nodeAddr, nodeContents)
            mstore(0x40, add(nodeAddr, 64))
        }

        hashNodeBranch(nodeAddr, leftNodeAddr, rightNodeAddr);
    }

    function hashNodeBranch(uint256 nodeAddr, uint256 leftNodeAddr, uint256 rightNodeAddr) private pure {
        assembly {
            mstore(32, 0) // If leftNodeAddr or rightNodeAddr is 0, then this makes their nodeHash = 0x00...
            let leftNodeHash := mload(add(leftNodeAddr, 32))
            let rightNodeHash := mload(add(rightNodeAddr, 32))
            mstore(0, leftNodeHash)
            mstore(32, rightNodeHash)

            let nodeHash := keccak256(0, 64)
            mstore(add(nodeAddr, 32), nodeHash)
        }
    }

    function buildNodeContentsLeaf(uint256 valAddr, uint256 valLen, uint256 keyHashAddr) private pure returns (uint256 nodeContents) {
        nodeContents = valAddr << (9*8) |
                       valLen << (5*8) |
                       keyHashAddr << (1*8) |
                       uint256(NodeType.Leaf);
    }

    function buildNodeLeaf(uint256 valAddr, uint256 valLen, uint256 keyHashAddr) private pure returns (uint256 nodeAddr) {
        uint256 nodeContents = buildNodeContentsLeaf(valAddr, valLen, keyHashAddr);

        assembly {
            let keyHash := mload(keyHashAddr)
            let valHash := keccak256(valAddr, valLen)

            mstore(0, keyHash)
            mstore(32, valHash)
            let nodeHash := keccak256(0, 65) // relies on most-significant byte of free space pointer being '\0'

            nodeAddr := mload(0x40)
            mstore(nodeAddr, nodeContents)
            mstore(add(nodeAddr, 32), nodeHash)
            mstore(0x40, add(nodeAddr, 64))
        }
    }

    function getNodeType(uint256 nodeAddr) private pure returns (NodeType nodeType) {
        if (nodeAddr == 0) return NodeType.Empty;

        assembly {
            nodeType := and(mload(nodeAddr), 0xFF)
        }
    }

    function getNodeHash(uint256 nodeAddr) internal pure returns (bytes32 nodeHash) {
        if (nodeAddr == 0) return bytes32(uint256(0));

        assembly {
            nodeHash := mload(add(nodeAddr, 32))
        }
    }

    function getNodeBranchLeft(uint256 nodeAddr) private pure returns (uint256) {
        uint256 nodeContents;

        assembly {
            nodeContents := mload(nodeAddr)
        }

        return (nodeContents >> (5*8)) & 0xFFFFFFFF;
    }

    function getNodeBranchRight(uint256 nodeAddr) private pure returns (uint256) {
        uint256 nodeContents;

        assembly {
            nodeContents := mload(nodeAddr)
        }

        return (nodeContents >> (1*8)) & 0xFFFFFFFF;
    }

    function getNodeBranchParent(uint256 nodeAddr) private pure returns (uint256) {
        uint256 nodeContents;

        assembly {
            nodeContents := mload(nodeAddr)
        }

        return nodeContents >> (9*8);
    }

    function setNodeBranchLeft(uint256 nodeAddr, uint256 newAddr) private pure {
        assembly {
            let nodeContents := mload(nodeAddr)
            nodeContents := and(not(shl(mul(5, 8), 0xFFFFFFFF)), nodeContents)
            nodeContents := or(nodeContents, shl(mul(5, 8), newAddr))
            mstore(nodeAddr, nodeContents)
        }
    }

    function setNodeBranchRight(uint256 nodeAddr, uint256 newAddr) private pure {
        assembly {
            let nodeContents := mload(nodeAddr)
            nodeContents := and(not(shl(mul(1, 8), 0xFFFFFFFF)), nodeContents)
            nodeContents := or(nodeContents, shl(mul(1, 8), newAddr))
            mstore(nodeAddr, nodeContents)
        }
    }

    function setNodeBranchParent(uint256 nodeAddr, uint256 newAddr) private pure {
        assembly {
            let nodeContents := mload(nodeAddr)
            nodeContents := and(not(shl(mul(9, 8), 0xFFFFFFFF)), nodeContents)
            nodeContents := or(nodeContents, shl(mul(9, 8), newAddr))
            mstore(nodeAddr, nodeContents)
        }
    }

    function getNodeLeafKeyHash(uint256 nodeAddr) private pure returns (bytes32 keyHash) {
        assembly {
            let nodeContents := mload(nodeAddr)
            let keyHashAddr := and(shr(mul(1, 8), nodeContents), 0xFFFFFFFF)
            keyHash := mload(keyHashAddr)
        }
    }

    function getNodeLeafVal(uint256 nodeAddr) private pure returns (uint256 valAddr, uint256 valLen) {
        assembly {
            let nodeContents := mload(nodeAddr)
            valAddr := and(shr(mul(9, 8), nodeContents), 0xFFFFFFFF)
            valLen := and(shr(mul(5, 8), nodeContents), 0xFFFFFFFF)
        }
    }


    function importProof(bytes memory encoded) internal pure returns (uint256 rootNodeAddr) {
        ProofState memory proof;

        proof.encoded = encoded;

        parseStrands(proof);
        processCmds(proof);

        uint256 strandState = getStrandState(proof, 0);
        uint256 depth = strandStateDepth(strandState);
        uint256 next = strandStateNext(strandState);

        require(next == proof.numStrands, "next linked list not empty");
        require(depth == 0, "strand depth not at root");

        return strandStateNodeAddr(strandState);
    }


    // This function must not call anything that allocates memory, since it builds a
    // contiguous array of strand states starting from the initial free memory pointer.

    function parseStrands(ProofState memory proof) private pure {
        bytes memory encoded = proof.encoded;
        uint256 offset = 0; // into proof.encoded
        uint256 numStrands = 0;

        require(mloadUint8(encoded, offset++) == 0, "Only CompactNoKeys encoding supported");

        uint256 strandStateAddr;
        assembly {
            strandStateAddr := mload(0x40)
        }

        // Setup strand state and embedded leaf nodes

        while (true) {
            StrandType strandType = StrandType(mloadUint8(encoded, offset++));
            if (strandType == StrandType.Invalid) break;

            uint8 depth = mloadUint8(encoded, offset++);

            uint256 keyHashAddr;
            assembly { keyHashAddr := add(add(encoded, 32), offset) }
            bytes32 keyHash = mloadBytes32(encoded, offset);
            offset += 32;

            uint256 nodeContents;
            bytes32 valHash;

            if (strandType == StrandType.Leaf) {
                uint256 valLen = 0;
                uint8 b;
                do {
                    b = mloadUint8(encoded, offset++); 
                    valLen = (valLen << 7) | (b & 0x7F);
                } while ((b & 0x80) != 0);

                uint256 valAddr;

                assembly {
                    valAddr := add(add(encoded, 32), offset)
                    valHash := keccak256(valAddr, valLen)
                }

                nodeContents = buildNodeContentsLeaf(valAddr, valLen, keyHashAddr);

                offset += valLen;
            } else if (strandType == StrandType.WitnessLeaf) {
                nodeContents = keyHashAddr << (1*8) |
                               uint256(NodeType.WitnessLeaf);

                valHash = mloadBytes32(encoded, offset);
                offset += 32;
            }

            uint256 strandState = packStrandState(depth, 0, numStrands+1, 0);

            assembly {
                let strandStateMemOffset := add(mload(0x40), shl(7, numStrands)) // numStrands * 128

                if iszero(eq(strandType, 3)) { // Only if *not* StrandType.WitnessEmpty
                    mstore(0, keyHash)
                    mstore(32, valHash)
                    let nodeHash := keccak256(0, 65) // relies on most-significant byte of free space pointer being '\0'

                    mstore(add(strandStateMemOffset, 64), nodeContents)
                    mstore(add(strandStateMemOffset, 96), nodeHash)

                    strandState := or(strandState, add(strandStateMemOffset, 64)) // add the following nodeAddr to the strandState
                }

                mstore(strandStateMemOffset, strandState)
                mstore(add(strandStateMemOffset, 32), keyHash)
            }

            numStrands++;
        }

        // Bump free memory pointer over strand states we've just setup

        assembly {
            mstore(0x40, add(mload(0x40), shl(7, numStrands))) // numStrands * 128
        }

        // Output

        proof.numStrands = numStrands;
        proof.strandStateAddr = strandStateAddr;
        proof.startOfCmds = offset;
    }

    function processCmds(ProofState memory proof) private pure {
        bytes memory encoded = proof.encoded;
        uint256 offset = proof.startOfCmds;

        uint256 currStrand = proof.numStrands - 1;

        while (offset < encoded.length) {
            uint8 cmd = mloadUint8(encoded, offset++);

            if ((cmd & 0x80) == 0) {
                uint256 strandState = getStrandState(proof, currStrand);
                bytes32 keyHash = getStrandKeyHash(proof, currStrand);
                (uint256 depth, uint256 merged, uint256 next, uint256 nodeAddr) = unpackStrandState(strandState);
                require(merged == 0, "can't operate on merged strand");

                if (cmd == 0) {
                    // merge

                    require(next != proof.numStrands, "no next strand");
                    uint256 nextStrandState = getStrandState(proof, next);
                    require(depth == strandStateDepth(nextStrandState), "strands at different depths");

                    nodeAddr = buildNodeBranch(nodeAddr, strandStateNodeAddr(nextStrandState));

                    saveStrandState(proof, next, nextStrandState | (1 << (8*8))); // set merged in next strand

                    next = strandStateNext(nextStrandState);
                    depth--;
                } else {
                    // hashing

                    bool started = false;

                    for (uint i=0; i<7; i++) {
                        if (started) {
                            require(depth > 0, "can't hash depth below 0");

                            uint256 witnessNodeAddr;

                            if ((cmd & 1) != 0) {
                                // HashProvided
                                bytes32 witness = mloadBytes32(encoded, offset);
                                offset += 32;
                                witnessNodeAddr = buildNodeWitness(witness);
                            } else {
                                // HashEmpty
                                witnessNodeAddr = 0;
                            }

                            if ((uint256(keyHash) & (1 << (256 - depth))) == 0) {
                                nodeAddr = buildNodeBranch(nodeAddr, witnessNodeAddr);
                            } else {
                                nodeAddr = buildNodeBranch(witnessNodeAddr, nodeAddr);
                            }

                            depth--;
                        } else {
                            if ((cmd & 1) != 0) started = true;
                        }

                        cmd >>= 1;
                    }
                }

                uint256 newStrandState = packStrandState(depth, merged, next, nodeAddr);
                saveStrandState(proof, currStrand, newStrandState);
            } else {
                // jump
                uint256 action = cmd >> 5;
                uint256 distance = cmd & 0x1F;

                if (action == 4) { // short jump fwd
                    currStrand += distance + 1;
                } else if (action == 5) { // short jump rev
                    currStrand -= distance + 1;
                } else if (action == 6) { // long jump fwd
                    currStrand += 1 << (distance + 6);
                } else if (action == 7) { // long jump rev
                    currStrand -= 1 << (distance + 6);
                }

                require(currStrand < proof.numStrands, "jumped outside of proof strands");
            }
        }
    }



    // get and put

    function get(uint256 nodeAddr, bytes32 keyHash) internal pure returns (bool found, bytes memory) {
        uint256 depthMask = 1 << 255;
        NodeType nodeType;

        while(true) {
            nodeType = getNodeType(nodeAddr);

            if (nodeType == NodeType.Branch) {
                if ((uint256(keyHash) & depthMask) == 0) {
                    nodeAddr = getNodeBranchLeft(nodeAddr);
                } else {
                    nodeAddr = getNodeBranchRight(nodeAddr);
                }

                depthMask >>= 1;
                continue;
            }

            break;
        }

        if (nodeType == NodeType.Leaf) {
            bytes32 leafKeyHash = getNodeLeafKeyHash(nodeAddr);

            if (leafKeyHash == keyHash) {
                (uint256 valAddr, uint256 valLen) = getNodeLeafVal(nodeAddr);

                bytes memory val;
                uint256 copyDest;

                assembly {
                    val := mload(0x40)
                    mstore(val, valLen)
                    copyDest := add(val, 32)
                    mstore(0x40, add(val, add(valLen, 32)))
                }

                memcpy(copyDest, valAddr, valLen);

                return (true, val);
            } else {
                return (false, "");
            }
        } else if (nodeType == NodeType.WitnessLeaf) {
            bytes32 leafKeyHash = getNodeLeafKeyHash(nodeAddr);

            require(leafKeyHash != keyHash, "incomplete tree (WitnessLeaf)");

            return (false, "");
        } else if (nodeType == NodeType.Empty) {
            return (false, "");
        } else {
            require(false, "incomplete tree (Witness)");
        }
    }

    function put(uint256 nodeAddr, bytes32 keyHash, bytes memory val) internal pure returns (uint256) {
        uint256 depthMask = 1 << 255;
        NodeType nodeType;
        uint256 parentNodeAddr = 0;

        while(true) {
            nodeType = getNodeType(nodeAddr);

            if (nodeType == NodeType.Branch) {
                parentNodeAddr = nodeAddr;

                if ((uint256(keyHash) & depthMask) == 0) {
                    nodeAddr = getNodeBranchLeft(nodeAddr);
                } else {
                    nodeAddr = getNodeBranchRight(nodeAddr);
                }

                setNodeBranchParent(nodeAddr, parentNodeAddr);

                depthMask >>= 1;
                continue;
            }

            break;
        }


        // Do splitting

        if (nodeType == NodeType.Leaf || nodeType == NodeType.WitnessLeaf) {
            bytes32 foundKeyHash = getNodeLeafKeyHash(nodeAddr);

            if (foundKeyHash != keyHash) {
                uint256 leafNodeAddr = nodeAddr;

                while (true) {
                    nodeAddr = buildNodeBranch(0, 0);
                    setNodeBranchParent(nodeAddr, parentNodeAddr);

                    parentNodeAddr = nodeAddr;
                    if ((uint256(keyHash) & depthMask) != (uint256(foundKeyHash) & depthMask)) break;
                    depthMask >>= 1;
                }

                if ((uint256(keyHash) & depthMask) == 0) {
                    setNodeBranchRight(nodeAddr, leafNodeAddr);
                } else {
                    setNodeBranchLeft(nodeAddr, leafNodeAddr);
                }

                depthMask >>= 1;
            }
        } else if (nodeType == NodeType.Empty) {
            // fall through
        } else {
            require(false, "incomplete tree (Witness)");
        }


        // Construct new Leaf

        uint256 keyHashAddr;
        uint256 valLen;
        uint256 valAddr;

        assembly {
            keyHashAddr := mload(0x40)
            mstore(keyHashAddr, keyHash)
            mstore(0x40, add(keyHashAddr, 32))

            valLen := mload(val)
            valAddr := add(val, 32)
        }

        nodeAddr = buildNodeLeaf(valAddr, valLen, keyHashAddr);


        // Update path back up the tree

        while (parentNodeAddr != 0) {
            depthMask <<= 1;

            uint256 leftNodeAddr;
            uint256 rightNodeAddr;

            if ((uint256(keyHash) & depthMask) == 0) {
                leftNodeAddr = nodeAddr;
                rightNodeAddr = getNodeBranchRight(parentNodeAddr);
            } else {
                leftNodeAddr = getNodeBranchLeft(parentNodeAddr);
                rightNodeAddr = nodeAddr;
            }

            nodeAddr = parentNodeAddr;
            setNodeBranchLeft(nodeAddr, leftNodeAddr);
            setNodeBranchRight(nodeAddr, rightNodeAddr);
            hashNodeBranch(nodeAddr, leftNodeAddr, rightNodeAddr);

            parentNodeAddr = getNodeBranchParent(parentNodeAddr);
        }

        return nodeAddr;
    }


    // Memory utils

    function memcpy(uint dest, uint src, uint len) private pure {
        while (len >= 32) {
            assembly {
                mstore(dest, mload(src))
            }
            dest += 32;
            src += 32;
            len -= 32;
        }

        assembly {
            let sepMask := shl(shl(sub(32, len), 3), 1)
            let srcPart := and(mload(src), not(sepMask))
            let destPart := and(mload(dest), sepMask)
            mstore(dest, or(destPart, srcPart))
        }
    }

    function mloadUint8(bytes memory p, uint256 offset) private pure returns (uint8 output) {
        require(p.length >= (offset + 1), "proof ends prematurely");

        assembly {
            output := mload(add(add(p, 1), offset))
        }
    }

    function mloadBytes32(bytes memory p, uint256 offset) private pure returns (bytes32 output) {
        require(p.length >= (offset + 32), "proof ends prematurely");

        assembly {
            output := mload(add(add(p, 32), offset))
        }
    }
}
