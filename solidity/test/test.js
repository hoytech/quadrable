const bre = require("@nomiclabs/buidler");
const { expect } = require("chai");

const child_process = require('child_process');
const fs = require('fs');

const quadb = '../quadb';

if (!fs.existsSync(quadb)) {
    console.error();
    console.error(`The quadb binary does not exist: ${quadb}`);
    console.error(`Please compile it, and then try running tests again`);
    console.error(`  https://github.com/hoytech/quadrable#building`);
    console.error();
    process.exit(1);
}



let testSpecs = [];

testSpecs.push({
    desc: 'single branch inclusion',
    data: {
        a: 'b',
        b: 'c',
    },
    inc: ['a'],
    non: [],
    err: ['b'],
});



testSpecs.push({
    desc: '1000 records, both types of non-inclusion',
    data: makeData(1000, i => [i+1, i+1]),
    inc: ['3', '4'],
    non: [
        '5000', // WitnessEmpty
        '5001', // WitnessLeaf
    ],
    err: ['b'],
});



testSpecs.push({
    desc: 'larger number of strands',
    data: makeData(1000, i => [i+1, i+1]),
    inc: Array.from(Array(50).keys()).map(i => `${i+1}`),
});



testSpecs.push({
    desc: 'long key/value',
    data: {
        ['key'.repeat(100)]: 'value'.repeat(1000),
        ['key'.repeat(101)]: 'value'.repeat(1004),
    },
    inc: [
        'key'.repeat(100),
        'key'.repeat(101),
    ],
});




testSpecs.push({
    desc: 'put update left side',
    data: {
        a: 'b',
        b: 'c',
    },
    inc: ['a'],
    put: [
        ['a', 'hello'],
    ],
});

testSpecs.push({
    desc: 'put update right side',
    data: {
        a: 'b',
        b: 'c',
    },
    inc: ['b'],
    put: [
        ['b', 'hello'],
    ],
});

testSpecs.push({
    desc: 'put both sides',
    data: {
        a: 'b',
        b: 'c',
    },
    inc: ['a', 'b'],
    put: [
        ['a', 'hello'],
        ['b', 'hello'],
    ],
});

testSpecs.push({
    desc: 'put both sides, other order',
    data: {
        a: 'b',
        b: 'c',
    },
    inc: ['a', 'b'],
    put: [
        ['b', 'hello'],
        ['a', 'hello'],
    ],
});


testSpecs.push({
    desc: '1000 records, update a few of them',
    data: makeData(1000, i => [i+1, i+1]),
    inc: ['200', '201', '202'],
    put: [
        ['200', 'new value for 200'],
        ['201', 'new value for 201'],
        ['202', 'new value for 202'],
    ],
});


testSpecs.push({
    desc: 'add record to WitnessEmpty',
    data: makeData(1000, i => [i+1, i+1]),
    non: ['5000'],
    put: [
        ['5000', 'new value for 5000'],
    ],
});

testSpecs.push({
    desc: 'add record, split WitnessLeaf',
    data: {
        a: 'b',
        b: 'c',
    },
    non: ['x'],
    put: [
        ['x', 'new value for x'],
    ],
});

testSpecs.push({
    desc: 'add record, split WitnessLeaf, with extra branch',
    data: {
        a: 'b',
        b: 'c',
    },
    non: ['y'],
    put: [
        ['y', 'new value for y'],
    ],
});

testSpecs.push({
    desc: '1000 records, add record, split WitnessLeaf',
    data: makeData(1000, i => [i+1, i+1]),
    non: ['5001'],
    put: [
        ['5001', 'new value for 5001'],
    ],
});


testSpecs.push({
    desc: 'basic push test',
    intData: ['a', 'b', 'c'],
    intInc: [1],
    push: ['d'],
});


testSpecs.push({
    desc: 'push from empty',
    intData: [],
    intInc: [],
    push: ['a', 'b', 'c', 'd', 'e', 'f', 'g'],
});

testSpecs.push({
    desc: 'bigger push test',
    intData: [...Array(1000).keys()].map(i => `item${i}`),
    intInc: [500, 501, 502],
    push: ['another item', 'another'],
});


if (process.env.GAS_USAGE) {
    let firstRow = true;

    for (let n of [1, 10, 100, 1000, 10000, 100000, 1000000]) {
        testSpecs.push({
            desc: `GAS_USAGE: ${n} records in db`,
            gasUsageTest: true,
            data: makeData(n, i => [i+1, i+1]),
            inc: ['1'],
            put: [['1', '2']],

            preAction: () => {
                if (!firstRow) return;
                firstRow = false;
                console.error("\n\n\n");
                console.error(`| DB Size | Average Depth | Calldata (gas) | Import (gas) | Query (gas) | Update (gas) | Total (gas) |`);
                console.error(`| --- | --- | --- | --- | --- | --- | --- |`);
            },
            logGas: (r, proofSize) => {
                let calldata = proofSize * 16;
                let total = calldata + parseInt(r[0]) + parseInt(r[1]) + parseInt(r[2]);
                console.error(`| ${n} | ${Math.round(Math.log2(n) * 10) / 10} | ${calldata} | ${r[0]} | ${r[1]} | ${r[2]} | ${total} |`);
            },
        });
    }
}

if (process.env.GAS_USAGE) {
    let dbSize = 1000000;
    let avgDepth = Math.log2(dbSize);

    let firstRow = true;

    for (let n of [1, 2, 4, 8, 16, 32, 64, 128]) {
        let keys = Array.from(Array(n).keys()).map(i => `${i+1}`);

        testSpecs.push({
            desc: `GAS_USAGE: ${n} strands`,
            gasUsageTest: true,
            data: makeData(dbSize, i => [i+1, i+1]),
            inc: keys,
            put: keys.map(k => [k, `${k} ${n}`]),

            manualDbInit: true,
            preAction: (quadb) => {
                if (!firstRow) {
                    quadb('fork --from gas-usage-strand-test');
                    return;
                }
                firstRow = false;

                quadb('checkout gas-usage-strand-test');

                {
                    let input = '';

                    for (let i = 0; i < dbSize; i++) {
                        input += `${i+1},${i+1}\n`;
                    }

                    quadb('import', { input, });
                }

                quadb('fork');

                console.error("\n\n\n");
                console.error(`| Num Strands | Approx Witnesses | Calldata (gas) | Import (gas) | Query (gas) | Update (gas) | Total (gas) |`);
                console.error(`| --- | --- | --- | --- | --- | --- | --- |`);
            },
            logGas: (r, proofSize) => {
                let calldata = proofSize * 16;
                let total = calldata + parseInt(r[0]) + parseInt(r[1]) + parseInt(r[2]);
                console.error(`| ${n} | ${Math.round(n * avgDepth * 10) / 10} | ${calldata} | ${r[0]} | ${r[1]} | ${r[2]} | ${total} |`);
            },
        });
    }
}




describe("Quadrable Test Suite", function() {
    let quadb_dir = './quadb-test-dir';
    let quadb_cmd = `${quadb} --db ${quadb_dir}`;

    child_process.execSync(`mkdir -p ${quadb_dir}`);
    child_process.execSync(`rm -f ${quadb_dir}/*.mdb`);


    let specsDevMode = testSpecs.filter(s => s.dev);
    if (specsDevMode.length) {
        testSpecs = specsDevMode;
        console.log("RUNNING IN DEV MODE");
    }

    let origSpecsLen = testSpecs.length;
    testSpecs = testSpecs.filter(s => !s.skip);
    if (origSpecsLen !== testSpecs.length) console.log("SKIPPING ONE OR MORE TESTS");

    if (process.env.GAS_USAGE) testSpecs = testSpecs.filter(s => s.gasUsageTest);


    for (let spec of testSpecs) {
        it(spec.desc, async function() {
            const TestHarness = await ethers.getContractFactory("TestHarness");
            const testHarness = await TestHarness.deploy();
            await testHarness.deployed();

            let quadb = (cmd, opts) => {
                if (!opts) opts = {};
                return child_process.execSync(`${quadb_cmd} ${cmd}`, { ...opts, });
            };

            if (!spec.manualDbInit) quadb(`checkout`);
            if (spec.preAction) spec.preAction(quadb);

            let rootHex, proofHex;

            let logGas = (res) => {
                if (process.env.GAS_USAGE) console.log("      GAS:", res[3].map(g => g.toNumber()));
                if (spec.logGas) spec.logGas(res[3], proofHex.length - 2);
            };

            {
                if (!spec.manualDbInit) {
                    if (spec.intData) {
                        let input = '';

                        for (let item of spec.intData) {
                            input += `${item}\n`;
                        }

                        quadb(`push --stdin`, { input, });
                    } else {
                        let input = '';

                        for (let key of Object.keys(spec.data)) {
                            input += `${key},${spec.data[key]}\n`;
                        }

                        quadb(`import`, { input, });
                    }
                }

                rootHex = quadb(`root`).toString().trim();

                let proofKeys;

                if (spec.intData) proofKeys = spec.intInc.join(' ');
                else proofKeys = (spec.inc || []).concat(spec.non || []).join(' ');

                proofHex = quadb(`exportProof --hex ${spec.intData ? '--int --pushable' : ''} -- ${proofKeys}`).toString().trim();
            }

            let updateKeys = [];
            let updateVals = [];

            for (let p of (spec.put || [])) {
                updateKeys.push(Buffer.from(p[0]));
                updateVals.push(Buffer.from(p[1]));
            }

            for (let p of (spec.push || [])) {
                updateVals.push(Buffer.from(p));
            }

            let res = await testHarness.testProof(proofHex, (spec.inc || []).map(i => Buffer.from(i)), spec.intInc || [], updateKeys, updateVals);
            expect(res[0]).to.equal(rootHex);
            logGas(res);
            for (let i = 0; i < (spec.inc || []).length; i++) {
                let valHex = res[1][i];
                valHex = valHex.substr(2); // remove 0x prefix
                expect(Buffer.from(valHex, 'hex').toString()).to.equal(spec.data[spec.inc[i]]);
            }

            for (let i = 0; i < (spec.intInc || []).length; i++) {
                let valHex = res[1][i];
                valHex = valHex.substr(2); // remove 0x prefix
                expect(Buffer.from(valHex, 'hex').toString()).to.equal(spec.intData[spec.intInc[i]]);
            }

            if (spec.non && spec.non.length) {
                let res = await testHarness.testProof(proofHex, spec.non.map(i => Buffer.from(i)), spec.intNon || [], [], []);
                logGas(res);
                for (let i = 0; i < spec.non.length; i++) {
                    expect(res[1][i]).to.equal('0x');
                }
            }

            for (let e of (spec.err || [])) {
                let threw;
                try {
                    await testHarness.testProof(proofHex, [Buffer.from(e)], [], [], []);
                } catch (e) {
                    threw = '' + e;
                }
                expect(threw).to.not.be.undefined;
                expect(threw).to.contain("incomplete tree");
            }

            if (spec.push && spec.push.length) {
                let input = '';

                for (let p of (spec.push || [])) {
                    input += `${p}\n`;
                }

                quadb(`push --stdin`, { input, });
            } else if (spec.put && spec.put.length) {
                let input = '';

                for (let p of (spec.put || [])) {
                    input += `${p[0]},${p[1]}\n`;
                }

                quadb(`import`, { input, });
            }

            let newRootHex = quadb(`root`).toString().trim();
            expect(res[2]).to.equal(newRootHex);
        });
    }
});


function makeData(n, cb) {
    let output = {};
    for (let i of Array.from(Array(n).keys())) {
        let [k, v] = cb(i);
        output[k] = '' + v;
    }
    return output;
}
