![Quadrable Logo](docs/logo.svg)

## Introduction

Quadrable is an authenticated multi-version embedded database. It is implemented as a sparse binary merkle tree with compact partial-tree proofs.

* *Authenticated*: The state of the database can be digested down to a 32-byte value, known as the "root". This represents the complete contents of the database, and any modifications to the database will generate a new root. Anyone who knows a root value can perform remote queries on the database and be confident that the responses are authentic. To accomplish this, the remote server provides "proofs" along with the responses, which are validated against the root.
* *Multi-version*: Many different versions of the database can exist at the same time. Deriving one version from another doesn't require copying the database. Instead, all of the data that is common between the versions is shared. This "copy-on-write" behaviour allows very inexpensive database snapshots or checkpoints, so these can be used liberally and for many purposes.
* *Embedded*: Quadrable's main functionality is in a C++ header-only library that is intended to be used by applications such as the `quadb` command-line tool. For persistent storage, it uses [LMDB](https://symas.com/lmdb/), an efficient database library that embeds the backing storage into your process by memory-mapping a file.

Although not required to use the library, it may help to understand the core data-structure used by Quadrable:

* *Merkle tree*: Each version of the database is represented by a tree. The leaves of this tree are the inserted records, and they are combined together with a cryptographic hash function to create a level of intermediate nodes. These intermediate nodes are then combined in a similar way to create a smaller set of intermediate nodes, and this procedure continues until a single node is left, which is the root. These "hash trees" are commonly called merkle trees, and they provide the mechanism for Quadrable's authentication. See my presentation on merkle trees for a detailed overview FIXME: link.
* *Binary*: The style of merkle tree used by Quadrable combines together exactly two nodes to create a node in the next layer. There are alternative designs such as N-ary radix trees, AVL trees, and tries, but they are more complicated to implement and typically have a higher authentication overhead (in terms of proof size). With a few optimisations and an attention to implementation detail, binary merkle trees enjoy almost all the benefits of these more complicated designs, as described in FIXME.
* *Sparse*: A traditional binary merkle tree does not have a concept of an "empty" leaf. This means that the leaves must be in a sequence, say 1 through N (with no gaps). This raises the question about what to do when N is not a power of two. Furthermore, adding new records in a "path-independent" way, where insertion order doesn't matter, is difficult to do efficiently. Quadrable uses a sparse merkle tree structure, where there *is* a concept of an empty leaf, and leaf nodes can be placed anywhere inside a large (256-bit) leaf location. This means that hashes of the database keys can correspond directly to each leaf's location in the tree.

Values are authenticated by generating and importing proofs:

* *Compact proofs*: In the classic description of a merkle tree, a value is proved to exist in the tree by providing a list of hashes as a proof. The value is hashed and then combined with this list in order to reconstruct the hashes of the intermediate nodes. If at the end of the list you end up with the root hash, the value is considered authenticated. However, if you wish to authenticate multiple values in the tree at the same time then these linear proofs can have a lot of space overhead due to duplicated hashes. Also, some hashes that would need to be included with a proof for a single value can instead be calculated by the verifier. Quadrable's compact proof encoding never transmits redundant sibling hashes, or ones that could be calculated during verification. It does this with a low overhead (approximately 0-4 bytes per proved item).
* *Partial-trees*: Since the process of verifying a merkle proof reconstructs some intermediate nodes of the tree, Quadrable takes advantage of this by building a partial-tree when authenticating a set of values. This partial-tree can be queried in the same way as if you had the full tree locally, although it will throw errors if you try to access values not in the authenticated set. You can also make modifications on a partial-tree, so long as you don't modify a non-authenticated value. The new root of the partial-tree will be the same as the root would be if you had made the same modifications on the full tree. New compact proofs can be generated *from* a partial-tree, as long as the values are a subset of the original authenticated values.


## Building

### Dependencies

The LMDB library and header files are required. On Ubuntu/Debian run this:

    sudo apt install -y liblmdb-dev

### Compilation

Clone the repo, `cd` into it, and run these commands:

    git submodule update --init
    make -j



## Command-line

The `quadb` command can be used to interact with a Quadrable database. It is very roughly modeled after `git`, so it requires sub-commands to activate its various functions. You can run `quadb` with no arguments to see a short help summary and a list of the available sub-commands.

### quadb init

Before you can use other commands, you must initialise a directory to contain the Quadrable database. By default it inits `./quadb-dir/`:

    $ quadb init
    Quadrable directory init'ed: ./quadb-dir/

You can specify an alternate directory with the `--db` flag:

    $ quadb --db=$HOME/.quadrable init
    Quadrable directory init'ed: /home/doug/.quadrable/

Or the `QUADB_DIR` environment variable:

    $ QUADB_DIR=/path/to/quadrable quadb init
    Quadrable directory init'ed: /path/to/quadrable

### quadb status

The status command shows you some basic information about your current database tree:

    $ quadb init
    Head: master
    Root: 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

*Head* is like your current branch in git, and can be thought of as a symbolic link that is updated to point to the latest version of the tree as it is modified. The default head is `master`. Quadrable doesn't call these links "branches" because it has a concept of branches internally, and this would confuse the code too much.

*Root* is the hash of the root node in your database. Provided the hash function is cryptographically secure, this is a globally unique identifier for the current state of the tree pointed to by your head. For an empty tree, a special all-zero value is used (see FIXME).

The number in parentheses after the root hash is the *nodeId*. This is an internal value used by Quadrable and is shown here for informational purposes only (see FIXME).

### quadb put

This adds a new record to the database, or updates an existing one. On success there is no output:

    $ quadb put key val

Unless the value was the same as a previously existing one, the current head will be updated to have a new root, and a new nodeId will be allocated for it:

    $ quadb status
    Head: master
    Root: 0x7b46238caa66f0646e29cec43dab1d010001e7cac6ee3371363b90a31e6c34bd (1)

### quadb get

This is the complement to `put`, and is used to retrieve previously set values:

    $ quadb get key
    val

An error will be thrown if you try to get a key that is not present in the tree:

    $ quadb get no-such-key
    quadb error: key not found in db

### quadb del

This deletes a key from the database. If there was no such key in the database, it does nothing. On success there is no output:

    $ quadb del key

If we run `status` again, we can see the root has changed back to the all-zeros value, signifying an empty tree:

    $ quadb status
    Head: master
    Root: 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

This is an important property of Quadrable: Identical trees have identical roots. "Path dependencies" such as the order in which records were inserted, or whether any deletions or modifications occurred along the way, do not affect the resulting roots.

### quadb import

If you wish to insert multiple records into the DB, running `quadb put` multiple times is inefficient. This is because each time it is run it will need to create new intermediate nodes and discard the previously created ones.

A better way to do it is to use `quadb import` which can put multiple records with a single traversal of the tree. This command reads comma-separated `key,value` pairs from standard input, one per line. The separator can be changed with the `--sep` option. On success there is no output:

    $ perl -E 'for $i (1..1000) { say "key $i,value $i" }' | quadb import

### quadb export

This is the complement to `quadb import`. It dumps the contents of the database as a comma-separated (again, customisable with `--sep`) list of lines:

    $ quadb export
    key 915,value 915
    key 116,value 116
    key 134,value 134
    key 957,value 957
    key 459,value 459
    ...

Note that the output is *not* sorted by the key. It is sorted by the hash of the key, because that is the way records are stored in the tree (FIXME see section on adversarial). You can pipe this output to the `sort` command if you would like it sorted by key.

### quadb head

A database can have many heads. You can view the list of heads with `quadb head`:

    $ quadb head
    => master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

The `=>` arrow indicates that `master` is the current head. The heads are sorted by `nodeId` (the number in parentheses), so the most recently updated heads will appear at the top (except for empty trees, which always have a nodeId of 0).

`head rm` deletes a head (or does nothing if the head doesn't exist):

    $ quadb head rm headToRemove

### quadb checkout

The `checkout` command can be used to change the current head. If we switch to a brand-new head, then this head will start out as the empty tree. For example, let's switch to a brand-new head called `temp`. On success there is no output:

    $ quadb checkout temp

This new head will not appear in the `quadb head` list until we have completed a write operation, like so:

    $ quadb put tempKey tempVal
    $ quadb head
    => temp : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

The `tempKey` record that we just inserted only exists in the `temp` head, and if we checkout back to master it would not be visible there.

Running `quadb checkout` with no head name will result in a detached head pointing to an empty tree (see FIXME).

### quadb fork

When we created a new head with checkout, it was initialized to an empty tree. Instead, we may choose to use `quadb fork` to copy the current head to the new head:

    $ quadb fork temp2
    $ ./quadb head
       temp : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
    => temp2 : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

Our new `temp2` head starts off with the same root as `temp`. We can now modify `temp2` and it will not affect the `temp` tree.

Although semantically `quadb fork` acts like it copies the tree pointed to by the current head, no copying actually occurs. In fact, the two trees share the same structure so forking is a very inexpensive operation. Cheap database snapshots is an important feature of Quadrable, and is useful for a variety of tasks.

`quadb fork` can take a second argument which represents the head to be copied from, instead of using the current head. Or it can take no arguments, in which case the current head is forked to a detached head (see FIXME).

### quadb exportProof

FIXME

### quadb importProof

FIXME





## Tree Structure


### Hash-tree

Why use trees to store data? The reason we use trees is because of the exponential growth in the number of nodes, as the depth is increased. The number of nodes that must be traversed to get to a leaf is related to the depth of the tree, which grows much slower than the number of nodes:

![](docs/exponential.svg)

In a merkle tree, each node has a `nodeHash` which is the formed by the concatenation of the `nodeHash`es of its children. In Quadrable the tree is binary, so there are always exactly two children (except for leaf nodes, which have none). The order of the hashes is important: The left child comes first, followed by the right child:

![](docs/hash-tree.svg)

The advantage of a merkle tree is that the `nodeHash` of the node at depth 0 (which has no parents) is a digest of all the other nodes and leaves. This top-level `nodeHash` is often just called the "root" of the tree. As long as the tree structure is carefully designed, and the hash function is secure, any changes to the tree or its contents will result in a new, distinct root.



### Keys

For all operations, keys are first hashed and then these hashes are used to traverse the tree to find the locations where the values are stored:

![](docs/path.svg)

Keys are hashed for multiple reasons:

* It puts a bound on the depth of the tree. Since Quadrable uses a 256-bit hash function, the maximum depth is 256 (although it will never actually get that deep since we collapse leaves, as described in FIXME).
* Since the hash function used by Quadrable is believed to be cryptographically secure (meaning among other things that it acts like a [random oracle](https://eprint.iacr.org/2015/140.pdf)), the keys should be fairly evenly distributed which reduces the average depth of the tree.
* It is computationally expensive to find two or more keys that have the same hash prefix, which an adversarial user might like to do to increase the cost of traversing the tree or, even worse, increase the proof sizes. See the FIXME section



### Sparseness

Obviously creating a full tree with 2^256 possible key paths is impossible. Fortunately, there is [an optimization](https://www.links.org/files/RevocationTransparency.pdf) that lets us avoid creating this number of nodes. If every empty leaf contains the the same value, then all of the nodes at the next level up will have the same hash. And since all these nodes have the same hashes, the nodes on the next level up from there will also have the same hashes.

So by caching the value of the empty sub-tree at depth N, we can easily compute the hash of the empty sub-tree at depth N-1. The technique of using cached values rather than re-computing them when needed is called [dynamic programming](https://skerritt.blog/dynamic-programming/) and has been successfully applied to many graph and tree problems.

Quadrable makes two minor changes to this model of sparseness that help simplify the implementation:

1. An empty leaf is given a `nodeHash` of 32 zero bytes. Because of the first pre-image resistance property of our hash function, it is computationally infeasible to find another value for a leaf or node that has an all zero hash.
1. The hash function used when combining two children nodes has a special case override: Given an input of 64 zero bytes, the output is 32 zero bytes. Any other input is hashed as usual. Again, it is computationally infeasible to find another input that has 32 zero bytes as an output.

The consequences of these changes is that empty sub-trees at all depths have 32 zero bytes as their `nodeHash`. This includes the root node, so a totally empty tree will have a root of 32 zeros.

* All zero roots are user-friendly: It's easy to recognize an empty tree.
* A run of zeros will compress better (or are cheaper in gas, if zero bytes are discounted), so if empty tree roots are transmitted frequently as the base case in some protocol, it may help for them to be all zeros.
* In some situations, like an Ethereum smart contract, using all zero values allows some minor optimizations (though it is possible to code without needing storage loads either way, by pre-computing and embedding the empty values in the contract code).


### Collapsed Leaves

Although using the sparseness optimisation described above makes it feasible to simulate a binary tree with a depth of 256, it still requires us to traverse 256 nodes to get to a leaf. And adding a new leaf requires creating 256 new nodes and 256 invocations of the hash function.

In order to avoid this overhead, Quadrable uses another optimisation called *collapsed leaves*. In this case, whenever a sub-tree contains exactly one non-empty leaf node (implying all the other leaves are empty), this sub-tree is not stored or computed. Instead, only the leaf is stored. If the leaf is collapsed to depth N, then only N intermediate nodes need to be traversed to get to it from the root.

The obvious issue with this approach is that we lose the ability to distinguish which of the many leaves in the sub-tree is the non-empty one. Even if we were to store the key hash alongside the leaf, a prover would not be able to trust this value if it was provided alongside a proof. The consequence is that a leaf could be "moved around" within a sub-tree, without affecting the root.

In order to prevent this, we need to hash the path information along with the leaf's value when computing a collapsed leaf's `nodeHash`:

    leafNodeHash = H(depth || H(key) || H(value))

* The `depth` is a single byte that indicates at what level the node was collapsed.
* The hashed key is 32 bytes and represents the path from the root to the leaf.
* The hashed value is also 32 bytes, and represents the value stored in this leaf.

There are two reasons for using the hash of the value rather than the value itself:

* For non-inclusion proofs (see FIXME) it is sometimes necessary to send a leaf along with the proof to show that a different leaf lies along the path where the queried leaf exists. In this case, where the verifier doesn't care about the contents of this "witness leaf", we can just include the hash of the value in the proof, which could potentially be much smaller than the full value. Note that the verifier still has enough information to move this witness leaf in their partial tree, if they wish to set the non-inclusion proved value.
* We can see that the input to the hash function when hashing a leaf is always 65 bytes. By contrast, the input to the hash function when combining nodes is always 64 bytes. This achieves a domain separation so that leaves cannot be reinterpreted as interior nodes, or vice versa.



## Storage

### LMDB

Because traversing Quadrable's tree data-structure requires reading many small records, and these reads cannot be parallelized or pipelined, it is very important to be able to read records quickly and efficiently.

Quadrable uses the [Lightning Memory-mapped Database](https://symas.com/lmdb/). LMDB works by memory mapping a file and using the page cache as shared memory between all the processes/threads accessing the database. When a node is accessed in the database, no copying or decoding of data needs to happen. The node is already "in memory" and in the format needed for the traversal.

LMDB is a B-tree database, unlike Log-Structured-Merge (LSM) databases such as LevelDB that are commonly used for merkle tree storage. Compared to LevelDB, LMDB has radically better read performance and uses the CPU more efficiently. It has instant recovery after a crash, doesn't suffer from write amplification, offers ACID transactions and multi-process concurrency (in addition to multi-thread), and is less likely to suffer data corruption.

### nodeId

Some implementations of hash trees store leaves and nodes in a database, keyed by the node's hash. This has the nice property that records are automatically de-duplicated. Since a collision-resistant hash function is used, if two values have the same hash they can be assumed to be identical. This is called [content-addressable storage](https://git-scm.com/book/en/v2/Git-Internals-Git-Objects).

Quadrable does not do this. Instead, every time a node is added, a numeric incrementing `nodeId` is allocated and the node is stored with this key. Although records are not de-duplicated, there are several advantages to this scheme:

* Nodes are clustered together in the database based on when they were created. This takes advantage of the phenomenon known as locality of reference (FIXME link). In particular, the top few levels of nodes in a tree are likely to reside in the same B-tree page.
* When garbage collecting unneeded nodes, no locking or reference counting is required. A list of collectable nodeIds can be assembled using an LMDB read-only transaction, which does not interfere with any other transactions. The nodeIds it finds can simply be deleted from the tree, since nodeIds are never reused.
* Intermediate nodes don't store the hashes of their two children nodes, but instead just the nodeIds. This means they only occupy 8+8 bytes, rather than 32+32.

## nodeType

Internally there are several different types of nodes stored.

* **Branches**: These are interior nodes that reference one or two children nodes. In the case where one of the nodes is an empty node, a branch left/right node is used. If the right child is empty, a branch left node is used, and vice versa. If neither are empty then a branch both node is used. Note that a branch implies that there are 2 or more leafs somewhere underneath this node, otherwise the branch node would be collapsed to a leaf or would be empty.
* **Empty**: Empty nodes are not stored in the database, but instead are implicit children of branch left/right nodes.
* **Leaf**: These are collapsed leaves, and contain enough information to satisfy get/put/del operations, and to be moved. A hash of the key is stored, but not the key itself. This is (optionally) stored in a separate table. See trackKeys FIXME
* **Witness** and **WitnessLeaf**: These are nodes that exist in partial-trees. A Witness node could be standing in for either a branch or a leaf, but a WitnessLeaf could only be a Leaf. The only difference between a WitnessLeaf and a Leaf is that the WitnessLeaf only stores a hash of the value, not the value itself. This means that it cannot be used to satisfy a get request. However, it still could be used for non-inclusion purposes, or for updating/deletion.

### Node layout in storage

Nodes are stored as byte strings in the `quadrable_node` table in LMDB.

The first 8 bytes are a combination of a tag value which indicates the nodeType, and an optional nodeId. These 8 bytes are a uint64_t stored in native byte order (which, yes, means that a Quadrable database file is *not* portable between machines of different endianness). The nodeId is shifted left 8 bits and a single byte tag is bitwise or'ed in.

The following 32 bytes are a cache of the nodeHash of this node. In principle, this value could be reconstructed by recrawling the sub-tree beneath this node and rehashing the leaves and combining upwards, however this would ruin performance since the nodeHash is frequently needed, for example during tree updates and proof generation.

The meaning of the remaining bytes depend on the nodeType:

    branch left:  [8 bytes: \x01 | leftNodeId << 8]  [32 bytes: nodeHash]
    branch right: [8 bytes: \x02 | rightNodeId << 8] [32 bytes: nodeHash]
    branch both:  [8 bytes: \x03 | leftNodeId << 8]  [32 bytes: nodeHash] [8 bytes: right nodeId]
    leaf:         [8 bytes: \x04 | 0]                [32 bytes: nodeHash[ [32 bytes: keyHash] [N bytes: val]
 
    witness:      [8 bytes: \x05 | 0]                [32 bytes: nodeHash]
    witnessLeaf:  [8 bytes: \x06 | 0]                [32 bytes: nodeHash] [32 bytes: keyHash] [32 bytes: valHash]




## Proofs




## C++ Library

### LMDB Environment

Quadrable uses the [lmdbxx C++ bindings for LMDB](https://github.com/hoytech/lmdbxx) so consult its documentation as needed.

All operations must be done inside of LMDB transactions. Some operations like `get` and `exportProof` can be done inside read-only transactions, whereas others require read-write transactions.

Here is an example of how to setup the LMDB environment and create a Quadrable `db`:

    lmdb::env lmdb_env = lmdb::env::create();
    lmdb_env.set_max_dbs(64);
    lmdb_env.set_mapsize(1UL * 1024UL * 1024UL * 1024UL * 1024UL);
    lmdb_env.open("/path/to/quadrable-dir", MDB_CREATE, 0664);
    lmdb_env.reader_check();

    quadrable::Quadrable db;
    db.trackKeys = true; // optional

    {
        auto txn = lmdb::txn::begin(lmdb_env, nullptr, 0);
        db.init(txn);
        txn.commit();
    }

* The mapsize should be a very large value. It will not actually use that much space initially, that is just the size of the virtual address space to allocate.
* `reader_check()` will check for any stale readers (processes that had an LMDB environment open and then crashed without cleaning up). It is good practice to do this periodically, and/or on app startup.
* Set `trackKeys` to `true` if you want to keep a record of the keys, so you can do things like `export` in the `quaddb` application. For some applications, enumerating keys is not necessary so it is disabled by default for efficiency reasons.
* `db.init()` must be called in a write transaction, at least the first time the environment is accessed. This is so that it can setup the necessary LMDB tables.


### Encoding

Quadrable allows arbitrary byte strings as either keys or values. There are no restrictions on character encodings, or included 0 bytes.

Keys and values can be arbitrarily long, with the one exception that the empty string is not allowed as a key.

Although in the `quadb` application some values are presented in hexadecimal encoding, the Quadrature library itself does not handle hexadecimal at all. You may find it convenient to use the `to_hex` and `from_hex` utilities used by `quadb` and the test-suite:

    #include "hoytech/hex.h"
    using hoytech::to_hex;
    using hoytech::from_hex;



### Heads

Many of the operations described in the `quadb` command-line application have counterparts in the C++ library.

Note that the command-line application stores its currently checked out head information and other things in a special `quadrable_quadb_state` table, which allows them to persist in between command-line invocations. The C++ library does not use this table. Instead, this information is stored in the `Quadrable` object's memory. Your application and the command-line app can have different heads checked out simultaneously.

* `db.isDetachedHead()`: Returns true if the current tree is detached, in other words there is no head checked out.
* `db.getHead()`: Returns the current head checked out, or throws an exception if it's a detached head.
* `db.root(txn)`: Returns the root of the current tree.
* `db.checkout()`: Changes the current head to another (either existing, new, or detached).
* `db.fork()`: Copies the current head to another (either overwriting, new, or detached).



### Operation Batching

The Quadrable library is designed so that all operations can be batched.

#### Batched Updates

Suppose you'd like to update two keys in the DB. You can do this by calling the `put` method two times, like so:

    db.put(txn, "key1", "val1");
    db.put(txn, "key2", "val2");

However, this is not optimal. Each call to `put` must traverse the DB tree to find the location where the updated value should live, and then construct new nodes along the entire path back to the root. If the hashes of the values share any common prefix bits, then intermediate nodes created by the first `put` will be thrown away and recreated by the second `put`. At the very least, a root node will be created and thrown away.

Instead, it is more efficient to use the `change()` method, which returns an `UpdateSet`, make the desired changes on this temporary object, and then `apply()` it to the database:

    db.change()
      .put("key1", "val1")
      .put("key2", "val2")
      .apply(txn);

In this case, all the modifications will be made with a single traversal of the tree, and the minimal amount of nodes will be created along the way. Deletions can also be made using the same UpdateSet:

    db.change()
      .del("oldKey")
      .put("newKey", "val")
      .apply(txn);

#### Batched Gets

Although the benefit isn't quite as significant as in the update case, Quadrable also supports batched gets. This allows us to retrieve multiple values from the DB in a single tree traversal.

So instead of:

    std::string_view key1Val, key2Val;
    bool key1Exists = db.get(txn, "key1", key1Val);
    bool key2Exists = db.get(txn, "key2", key2Val);
    if (key1Exists) std::cout << key1Val;

Use the following to avoid unnecessary tree traversals:

    auto recs = db.get(txn, { "key1", "key2", });
    if (recs["key1"].exists) std::cout << recs["key1"].val;

**Important**: In both of the above cases, the values are `string_view`s that point into the LMDB memory map. This means that they are valid only up until a write operation is done on the transaction, or the transaction is terminated with commit/abort. If you need to keep them around longer, copy them into a string:

    std::string key1ValCopy(recs["key1"].val);

### Garbage Collection

The `GarbageCollector` class can be used to deallocate unneeded nodes. See the implementation in `quadb.cpp`.

* `gc.markAllHeads()` will mark all the heads stored in the `quadrable_head` table. But if you have other roots stored you would like to preserve you can mark them with `gc.markTree()`. Both of these methods can be called inside a read-only transaction.
* When you are done marking nodes, call `gc.sweep()`. This must be done inside a read-write transaction. All nodes that weren't found during the mark phase are deleted.




## Author and Copyright

Quadrable © 2020 Doug Hoyte.

2-clause BSD license. See the LICENSE file.
