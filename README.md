![Quadrable Logo](docs/logo.svg)

Quadrable is an authenticated multi-version embedded database. It is implemented as a sparse binary merkle tree.

## Introduction

* *Authenticated*: The state of the database can be digested down to a 32-byte value, known as the "root". This represents the complete contents of the database, and any modifications to the database will generate a new root. Anyone who knows a root value can perform remote queries on the database and be confident that the responses are authentic. To accomplish this, the remote server provides "proofs" along with the responses, which are validated against the root.
* *Multi-version*: Many different versions of the database can exist at the same time. Deriving one version from another doesn't require copying the database. Instead, all of the data that is common between the versions is shared. This "copy-on-write" behaviour allows very inexpensive database snapshots or checkpoints, so these can be used liberally and for many purposes.
* *Embedded*: The main functionality exists in a C++ header-only library intended to be used by applications such as the `quadb` command-line tool. Quadrable uses [LMDB](https://symas.com/lmdb/), an efficient database library which embeds the database's backing storage into your process by memory-mapping a file.

Although not required to use the library, it may help to understand the core data-structure used by Quadrable:

* *Merkle tree*: Each version of the database is represented by a tree. The leaves of this tree are the inserted records, and they are combined together with a cryptographic hash function to create a level of intermediate nodes. These intermediate nodes are then combined in a similar way to create a smaller set of intermediate nodes, and this procedure continues until a single node is left, which is the root. These "hash trees" are commonly called merkle trees, and they provide the mechanism for Quadrable's authentication. See my presentation on merkle trees for a detailed overview FIXME: link.
* *Binary*: This style of merkle tree combines together exactly two nodes to create the node in the next layer. There are alternative designs such as N-ary radix trees, AVL trees, and tries, but they are more complicated to implement and typically have a higher authentication overhead (in terms of proof size). With a few optimisations and an attention to implementation detail, binary merkle trees enjoy almost all the benefits of these more complicated designs, as described in FIXME.
* *Sparse*: A traditional binary merkle tree does not have a concept of an "empty" leaf. This means that keys must be in a sequence, say 1 through N (with no gaps). This opens the question about what to do when N is not a power of two. Furthermore, adding new records in a "path-independent" way, where insertion order doesn't matter, is difficult to do efficiently. Quadrable uses a sparse merkle tree structure, where there *is* a concept of an empty leaf, and leaf nodes can be placed anywhere inside a large (256-bit) key-space.


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

If you wish to insert multiple records into the DB, running `quadb put` multiple times is inefficient. This is because each time it is run it will need to discard and create new intermediate nodes.

A better way to do it is to use `quadb import` which can put multiple records with a single traversal of the tree. This command reads comma-separated `key,value` pairs standard input, one per line. The separator can be changed with the `--sep` option. On success there is no output:

    $ perl -E 'for my $i (1..1000) { say "key $i,value $i" }' | quadb import

### quadb export

This is the complement to `quadb import`. It dumps the contents of the database as a comma-separated (again, customisable with `--sep`) list of lines:

    $ quadb export|head
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
