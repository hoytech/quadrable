pragma solidity ^0.6.0;
pragma experimental ABIEncoderV2;

import "./Quadrable.sol";

contract TestHarness {
    function testProof(bytes calldata encodedProof,
                       bytes[] memory queries,
                       uint[] memory intQueries,
                       bytes[] memory updateKeys,
                       bytes[] memory updateVals) external view
                 returns (bytes32 origRoot, bytes[] memory queryResults, bytes32 updatedRoot, uint256[3] memory gasUsage) {

        uint256 g = gasleft();
        uint256 rootNodeAddr = Quadrable.importProof(encodedProof);
        gasUsage[0] = g - gasleft();

        origRoot = Quadrable.getNodeHash(rootNodeAddr);

        uint numQueries = queries.length > 0 ? queries.length : intQueries.length;
        queryResults = new bytes[](numQueries);

        g = gasleft();

        for (uint256 i = 0; i < numQueries; i++) {
            bytes32 query;

            if (queries.length > 0) query = keccak256(abi.encodePacked(queries[i]));
            else query = Quadrable.encodeInt(intQueries[i]);

            // For test purposes, return empty string as "not found" sentinel
            (bool found, bytes memory res) = Quadrable.get(rootNodeAddr, query);

            if (found) require(res.length != 0, "unexpected empty string when record found (test problem)");
            else require(res.length == 0, "unexpected non-empty string when record not found (code problem)");

            queryResults[i] = res;
        }

        gasUsage[1] = g - gasleft();


        if (updateKeys.length == 0) {
            // push test path

            g = gasleft();

            for (uint i = 0; i < updateVals.length; i++) {
                rootNodeAddr = Quadrable.push(rootNodeAddr, updateVals[i]);
            }

            gasUsage[2] = g - gasleft();
        } else {
            // put test path
            require(updateKeys.length == updateVals.length, "parallel update arrays size mismatch");

            g = gasleft();

            for (uint i = 0; i < updateKeys.length; i++) {
                rootNodeAddr = Quadrable.put(rootNodeAddr, keccak256(abi.encodePacked(updateKeys[i])), updateVals[i]);
            }

            gasUsage[2] = g - gasleft();
        }


        updatedRoot = Quadrable.getNodeHash(rootNodeAddr);
    }
}
