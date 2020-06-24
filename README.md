![Quadrable Logo](docs/logo.svg)

Quadrable is an authenticated multi-version embedded database based on a sparse binary merkle tree.

## Introduction

* *Authenticated*: The entire state of the database can be digested down to a 32-byte value, known as the "root". This represents the entire contents of the database, and any modifications will change this root. Furthermore, anyone who knows the root value but does not have a copy of the database can perform queries on the database remotely, so long as somebody provides "proofs" along with the results. These proofs are compared against the root to authenticate the query results.
* *Multi-version*: Many different versions of the database can exist at the same time. Versions that are derived from other versions don't result in the entire database being copied. Instead, all of the database that is common between the versions is shared, in a copy-on-write manner. With this functionality, database snapshots or checkpoints are very inexpensive so they can be used liberally, and for many purposes.
* *Embedded*: The main functionality exists in a C++ library that is intended to be used by applications, such as the included command-line application `quadb`. Quadrable uses [LMDB](https://lmdb.tech), which is an efficient database library that embeds the database into your process by memory-mapping a file.

Although not necessary for simple usage, it can help to understand the data-structure used by Quadrature:

* *Merkle tree*: Each version of the database is represented by a tree. The leaves of this tree are combined together with a cryptographic hash function to create a level of intermediate nodes. These intermediate nodes are then combined in the same way to create a smaller set of intermediate nodes, and this continues until a single node is left, which is called the root. These "hash trees" are commonly called merkle trees, and they provide the mechanism for Quadrable's authentication.
* *Binary*: This style of merkle tree combines together exactly two nodes to create the node in the next layer. There are alternative merkle tree designs such as radix trees, AVL trees, or tries, but they are much more complicated to implement and typically have a much larger authentication overhead (proof size). With a few optimisations and attention to implementation detail, binary merkle trees can enjoy almost all the benefits of these more complicated designs, as we will describe in FIXME.
* *Sparse*: A traditional binary merkle tree does not have a concept of an "empty" leaf. The consequence of this is that keys must be "dense" (without gaps), for example the sequence of integers from 1 to N. This opens the question about what to do when N is not a power of two. Furthermore, adding new records in a "path-independent" way, where insertion order doesn't matter, is difficult to do efficiently. Quadrable solves this by using a sparse merkle tree structure, where there *is* a concept of an empty leaf, and keys can be anywhere inside a large (256-bit) keyspace.


## Building

### Dependencies

The LMDB library and header files are required. On Ubuntu/Debian run this:

    sudo apt install -y liblmdb-dev

### Compilation

Clone the repo, `cd` into it, and run these commands:

    git submodule update --init
    make



## Command-line

The `quadb` command can be used to interact with a Quadrable database. It is very roughly modeled after `git`, so it requires sub-commands to activate its various functions. You can run `quadb` with no arguments to see a short help summary and a list of the available sub-commands.

### quadb init

Before you can use any other commands, you must initialise a directory to contain the Quadrable database. By default it inits `./quadb-dir/`:

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

Unless the value was the same as a previously existing one, the current head will be updated to have a new root:

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

### quadb head

A database can have many heads. You can view the list of heads with the `head` command:

    $ quadb head
    => master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

The `=>` arrow indicates that `master` is the current head. The heads are sorted by `nodeId` (the number in parentheses), so the most recently updated heads will appear at the top.

The `head rm` command can be used to delete a head (or do nothing if it doesn't exist):

    $ quadb head rm headToRemove

### quadb checkout

The `checkout` command can be used to change the current head. If we switch to a brand-new head, then this head will start out as the empty tree. For example, let's switch to a brand-new head called `temp`. On success there is no output:

    $ quadb checkout temp

This new head will not appear in the `quadb head` list until we have completed a write operation, like so:

    $ quadb put tempKey tempVal
    $ quadb head
    => temp : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

The `tempKey` record that we just inserted only exists in the `temp` head, and if we switched back to master it would not be visible there.

Running `quadb checkout` with no head name will result in a detached head pointing to an empty tree (see FIXME).

### quadb fork

When we created a new head with checkout, it was initialized to an empty tree. Instead, we may choose to use `fork` to copy the current head to the new head:

    $ quadb fork temp2
    $ ./quadb head
       temp : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
    => temp2 : 0x11bf4b644c4ad1c9e18a96c1f35cdd161941d2355742aaa3577dcefef0382a16 (2)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

Our new `temp2` head starts off with the same root as `temp`. We can now modify `temp2` and it will not affect the `temp` tree.

Although semantically `fork` acts like it copies the tree pointed to by the current head, no copying actually occurs. In fact, the two trees share the same structure so forking is a very inexpensive operation. Cheap database snapshots is an important feature of Quadrable, and is useful for a variety of tasks.

`quadb fork` can take a second argument which represents the head to be copied from, instead of using the current head. Or it can take no arguments, in which case the current head is forked to a detached head (see FIXME).


## C++ Library

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




## Author and Copyright

Quadrable © 2020 Doug Hoyte.

2-clause BSD license. See the LICENSE file.
