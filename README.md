![Quadrable Logo](docs/logo.svg)

<!-- TOC FOLLOWS -->
<!-- START OF TOC -->
* [Introduction](#introduction)
* [Building](#building)
  * [Dependencies](#dependencies)
  * [Compilation](#compilation)
  * [Tests](#tests)
* [Data Structure](#data-structure)
  * [Trees and Exponential Growth](#trees-and-exponential-growth)
  * [Merkle Trees](#merkle-trees)
  * [Keys](#keys)
  * [Sparseness](#sparseness)
  * [Collapsed Leaves](#collapsed-leaves)
  * [Splitting Leaves](#splitting-leaves)
  * [Bubbling](#bubbling)
* [Proofs](#proofs)
  * [Proofs and witnesses](#proofs-and-witnesses)
  * [Combined Proofs](#combined-proofs)
  * [Non-inclusion proofs](#non-inclusion-proofs)
  * [Strands](#strands)
  * [Commands](#commands)
  * [Proof encodings](#proof-encodings)
  * [Compact encoding](#compact-encoding)
  * [Proof bloating](#proof-bloating)
* [Storage](#storage)
  * [Copy-On-Write](#copy-on-write)
  * [LMDB](#lmdb)
  * [nodeId](#nodeid)
  * [nodeType](#nodetype)
  * [Node layout in storage](#node-layout-in-storage)
  * [Key tracking](#key-tracking)
* [Command-line](#command-line)
  * [quadb init](#quadb-init)
  * [quadb status](#quadb-status)
  * [quadb put](#quadb-put)
  * [quadb get](#quadb-get)
  * [quadb del](#quadb-del)
  * [quadb import](#quadb-import)
  * [quadb export](#quadb-export)
  * [quadb head](#quadb-head)
  * [quadb checkout](#quadb-checkout)
  * [quadb fork](#quadb-fork)
  * [quadb diff](#quadb-diff)
  * [quadb patch](#quadb-patch)
  * [quadb exportProof](#quadb-exportproof)
  * [quadb importProof](#quadb-importproof)
  * [quadb mergeProof](#quadb-mergeproof)
* [C++ Library](#c-library)
  * [LMDB Environment](#lmdb-environment)
  * [Encoding](#encoding)
  * [Heads](#heads)
  * [Operation Batching](#operation-batching)
    * [Batched Updates](#batched-updates)
    * [Batched Gets](#batched-gets)
  * [Garbage Collection](#garbage-collection)
* [Solidity](#solidity)
  * [Smart Contract Usage](#smart-contract-usage)
  * [Memory Layout](#memory-layout)
  * [Limitations](#limitations)
  * [Gas Usage](#gas-usage)
* [Author and Copyright](#author-and-copyright)
<!-- END OF TOC -->



## Introduction

Quadrable is an authenticated multi-version database. It is implemented as a sparse binary merkle tree with compact partial-tree proofs. There are [C++](#c-library) and [Solidity](#solidity) libraries, as well as a git-like [command-line tool](#command-line).

* *Authenticated*: The state of the database can be digested down to a 32-byte value, known as the "root". This represents the complete contents of the database, and any modifications to the database will result in a new root. Anyone who knows the root value can perform remote queries on the database and be confident that the responses are authentic. To accomplish this, the remote server provides [proofs](#proofs) along with the responses, which are validated against the root.
* *Multi-version*: Many different versions of the database can exist at the same time. Deriving one version from another doesn't require copying the database. Instead, all of the data that is common between the versions is shared. This [copy-on-write](#copy-on-write) behaviour allows very inexpensive database snapshots or checkpoints, so these can be used liberally.

Although not required to use the library, it may help to understand the core data-structure used by Quadrable:

* *Merkle tree*: Each version of the database is represented by a [tree](#trees-and-exponential-growth). The leaves of this tree are the inserted records, and they are combined together with calls to a cryptographic hash function, creating a smaller level of intermediate nodes. These intermediate nodes are then combined in a similar way to create a still smaller set of intermediate nodes, and this procedure continues until a single node is left, which is the root node. These "hash trees" are commonly called merkle trees, and they provide the mechanism for Quadrable's authentication.
* *Binary*: The style of merkle tree used by Quadrable combines together [exactly two](#merkle-trees) nodes to create a node in the next layer. There are alternative designs such as N-ary radix trees, AVL trees, and tries, but they are more complicated to implement and typically have a higher authentication overhead (in terms of proof size). With a few optimisations and an attention to implementation detail, binary merkle trees enjoy almost all the benefits of these more complicated designs.
* *Sparse*: A traditional binary merkle tree does not have a concept of an "empty" leaf. This means that the leaves must be in a sequence, say 1 through N (with no gaps). This raises the question about what to do when N is not a power of two. Furthermore, adding new records in a "path-independent" way, where insertion order doesn't matter, is difficult to do efficiently. Quadrable uses a [sparse](#sparseness) merkle tree structure, where there *is* a concept of an empty leaf, and leaf nodes can be placed anywhere inside a large (256-bit) address-space. This means that record keys can be hashed and used directly as each leaf's location in the tree.

Values are authenticated by generating and importing proofs:

* *Compact proofs*: In the classic description of a merkle tree, a value is proved to exist in the tree by providing a list of [witness](#proofs-and-witnesses) values as a proof. The value to be proved is hashed and then combined with the witnesses in order to reconstruct the hashes of the intermediate nodes along the path from the leaf to the root. If at the end of the list of witnesses you end up with the root hash, the value is considered authenticated. However, if you wish to authenticate multiple values in the tree at the same time then these linear proofs can have some space overhead due to duplicated hashes. Additionally, some hashes that would need to be included with a proof for a single value can instead be calculated by the verifier. Quadrable's compact proof encoding never transmits redundant sibling hashes, or ones that could be calculated during verification. It does this with a low overhead (approximately 0-4 bytes per proved item).
* *Partial-trees*: Since the process of verifying a merkle proof reconstructs some intermediate nodes of the tree, Quadrable constructs a [partial-tree](#combined-proofs) when authenticating a set of values. This partial-tree can be queried in the same way as if you had the full tree locally, although it will throw errors if you try to access non-authenticated values. You can also make modifications on a partial-tree, so long as you don't modify a non-authenticated value. The new root of the partial-tree will be the same as the root would be if you had made the same modifications on the full tree. After importing a proof, additional proofs exported from the same tree can be merged, expanding a partial-tree over time as new proofs are received. New proofs can also be generated *from* a partial-tree, as long as the values to prove are present (or were proved to *not* be present).


## Building

### Dependencies

The LMDB library and header files are required. On Ubuntu/Debian run this:

    sudo apt install -y liblmdb-dev

### Compilation

Clone the repo, `cd` into it, and run these commands:

    git submodule update --init
    make -j

### Tests

You can run the tests like so:

    make test

* If you `make clean` prior to `make test` then a version of the `check` binary will be compiled with address sanitiser support.

You can view a coverage report of the tests by running:

    make coverage

* You will need `lcov` installed.
* The report will be in this file: `quadrable/coverage-report/index.html`





## Data Structure


### Trees and Exponential Growth

The reason we use trees is because of the exponential growth in the number of nodes as the number of levels is increased. In other words, the number of intermediate nodes that must be traversed to get to a leaf grows much slower than the total number of nodes.

In computer science trees are usually drawn as growing in the downwards direction, for some reason. Because of this, we'll use the term "depth" to refer to how many levels down you are from the top node (the "root").

![](docs/exponential.svg)

### Merkle Trees

In a merkle tree, each node has a "nodeHash" which is the formed by hashing the concatenation of its children nodeHashes. In Quadrable the tree is binary, so there are always exactly two children (except for leaf nodes, which have none). The order of the concatenation is important: The left child's nodeHash comes first, followed by the right child's:

![](docs/hash-tree.svg)

The advantage of a merkle tree is that the nodeHash of the node at depth 0 (the top level) is a digest of all the other nodes and leaves. This top-level nodeHash is often just called the "root". As long as the tree structure is carefully designed, and the hash function is secure, any changes to the tree or its contents will result in a new, distinct root.



### Keys

In Quadrable's implementation of a merkle tree, keys are first hashed and then the bits of these hashes are used to traverse the tree to find the locations where the values are stored. A `0` bit means to use the left child of a node, and a `1` bit means use the right child:

![](docs/path.svg)

Keys are hashed for multiple reasons:

* It puts a bound on the depth of the tree. Since Quadrable uses a 256-bit hash function, the maximum depth is 256 (although it will never actually get that deep since we [collapse leaves](#collapsed-leaves)).
* Since the hash function used by Quadrable is believed to be cryptographically secure (meaning among other things that it acts like a [random oracle](https://eprint.iacr.org/2015/140.pdf)), the keys should be fairly evenly distributed which reduces the average depth of the tree.
* It is computationally expensive to find two or more keys that have the same hash prefix, which an adversarial user might like to do to increase the cost of traversing the tree or, even worse, [increase the proof sizes](#proof-bloating).



### Sparseness

Obviously creating a full tree with 2^256 possible key paths is impossible. Fortunately, there is [an optimization](https://www.links.org/files/RevocationTransparency.pdf) that lets us avoid creating this number of nodes. If every empty leaf contains the the same value, then all of the nodes at the next level up will have the same hash. And since all these nodes have the same hashes, the nodes on the next level up from there will also have the same hashes, and so on.

By caching the value of an empty sub-tree at depth N, we can easily compute the hash of the empty sub-tree at depth N-1. The technique of using cached values rather than re-computing them when needed is called [dynamic programming](https://skerritt.blog/dynamic-programming/) and has been successfully applied to many graph and tree problems.

![](docs/sparse.svg)

Quadrable makes two minor changes to this model of sparseness that help simplify the implementation:

1. An empty leaf is given a nodeHash of 32 zero bytes.
1. The hash function used when combining two child nodes has a special case override: Given an input of 64 zero bytes, the output is 32 zero bytes. Any other input is hashed as usual.

Because of the first pre-image resistance property of our hash function, it is computationally infeasible to find another value for a leaf that has an all zero hash. Because the override is not used when hashing leaves (and also because of the different hashing domains, see the next section), it is also computationally infeasible to find a non-empty node that is interpreted as a leaf, and vice versa.

The purpose of these changes is to make empty sub-trees at all depths have 32 zero bytes as their nodeHashes. This includes the root node, so a totally empty tree will have a root of 32 zeros.

* All zero roots are user-friendly: It's easy to recognize an empty tree.
* A run of zeros will compress better, so if empty tree roots are transmitted frequently as a degenerate case in some protocol, it may help for them to be all zeros.
* In some situations, like an Ethereum smart contract, using all zero values allows some minor optimizations. Specifically, 0 bytes in the calldata is cheaper on gas, contract code size is reduced, and "uninitialised" memory can be used for some operations. It does not save on storage loads though -- only a very naive implementation would store the cached empty values in storage as opposed to contract code.


### Collapsed Leaves

Although using the sparseness optimisation described above makes it feasible to simulate a binary tree with a depth of 256, it still would require us to traverse 256 nodes to get to a leaf. Adding a new leaf would require calling the hash function 256 times and creating 256 new nodes.

In order to avoid this overhead, Quadrable uses another optimisation called *collapsed leaves*. In this case, whenever a sub-tree contains exactly one non-empty leaf node (implying all the others are empty), this sub-tree is not stored or computed. Instead, only the non-empty leaf is stored. If the leaf is collapsed at depth N, then only N intermediate nodes need to be traversed to get to it from the root.

![](docs/collapsed-leaves.svg)

* Leaves must always be collapsed at the highest possible level to ensure that equivalent trees have equivalent roots.
* In Quadrable, there is no such thing as a non-collapsed leaf: All leaves are collapsed. For a leaf to reach the very bottom level, there would need to be two distinct keys with the same keyHash (except for the last bit). The collision-resistance property of our hash function allows us to assume this will never happen.

An issue with collapsing leaves is that we could lose the ability to distinguish which of the leaves in the sub-tree is the non-empty one. We could not create proofs for these leaves since other people would not be able to detect if we had "moved around" the leaf within the sub-tree.

In order to prevent this, Quadrable hashes the path information (that is, the keyHash) along with the leaf's value when computing a collapsed leaf's nodeHash:

    leafNodeHash = H(H(key) || H(value) || '\0')

* The hashed key is 32 bytes and represents the path from the root to the leaf.
* The hashed value is also 32 bytes, and represents the value stored in this leaf.
* `'\0'` is a single null byte (see below).

There are two reasons for using the hash of the value rather than the value itself:

* For [non-inclusion proofs](#non-inclusion-proofs) it is sometimes necessary to send a leaf along with the proof to show that a different leaf lies along the path where the queried leaf exists. In this case, where the verifier doesn't care about the contents of this "witness leaf", we can just include the hash of the value in the proof, which could potentially be much smaller than the full value. Note that the verifier still has enough information to move this witness leaf around in their partial-tree.
* Combined with the null byte, this ensures the input when hashing a leaf is always 65 bytes long. By contrast, the input when hashing two nodeHashes to get the parent's nodeHash is always 64 bytes. This achieves a domain separation so that leaves cannot be reinterpreted as interior nodes, nor vice versa.


### Splitting Leaves

Since a collapsed leaf is occupying a spot high up in the tree that could potentially be in the way of new leaves with the same key prefix, during an insertion it is sometimes necessary to "split" a collapsed leaf. A new branch node will be added in place of the collapsed leaf, and both leaves will be inserted further down underneath this branch. 

![](docs/split-leaf.svg)

Sometimes splitting a leaf will result in more than one branch being added. This happens when the leaf being added shares additional prefix bits with the leaf being split. These extra intermediate branches each an have empty node as one of their children.

![](docs/add-branch-empty.svg)

Quadrable does not store empty nodes. In the C++ implementation there are special [node types](#nodetype) to indicate if either of the children are empty sub-trees. These are the types of branch nodes:

* Branch Left: The right node is empty.
* Branch Right: The left node is empty.
* Branch Both: Neither are empty.


### Bubbling

Because of collapsed leaves, a branch implies that there are at least 2 leaves below the branch. Since an important requirement is that equivalent trees have equivalent roots, we must maintain this invariant when a leaf is deleted.

In order to keep all leaves collapsed to the lowest possible depth, a deletion may require moving a leaf several levels further up, potentially even up to the root (if it is the only remaining leaf in the tree). This is called "bubbling" the leaf back up:

![](docs/bubbling.svg)





## Proofs

So far the data-structure we've described is just an expensive way to store a tree of data. The reason why we're doing all this hashing in the first place is so that we can create *proofs* about the contents of our tree.

A proof is a record from the tree along with enough information for somebody who does not have a copy of the tree to reconstruct the root of the tree. This reconstructed root can then be compared with a version of the root acquired elsewhere, from a trusted source. Imagine that the root is published daily in a newspaper, or is embedded in a blockchain.

The purpose of using a tree structure is so that the information required in a proof is proportional to the depth of the tree, and not the total number of nodes.

### Proofs and witnesses

When you would like to query the database remotely, do the following steps:

* Acquire a copy of the root hash (32 bytes) from some trusted source.
* Hash the key of the record you would like to search for. Let's say you're looking for the record `"John Smith"` and it hashes to `1011` in binary (using a 4-bit hash for sake of explanation, normally this would be 256 bits). This is the path that will be used to traverse the tree to this record.
* Ask a provider who has the full data-set available to send you the value for the record that has this hash. Suppose they send you a JSON blob like `{"name":"John Smith","balance":"$200",...}`.

![](docs/proof1.svg)

At this point you have a value, but you can't be sure that the result wasn't tampered with. Maybe John's balance is actually "$0.05", or perhaps there isn't a record for John Smith at all.

In order to convince you that the record exists and is correct, the provider must send a proof along with the JSON. You can use this proof to re-compute the root hash and see if it matches the trusted root hash you acquired earlier. First compute the leaf hash. In Quadrable you do that by hashing the JSON value and combining it with the key's hash (see [collapsed leaves](#collapsed-leaves)).

Next, you must compute the hash of the leaf's parent node. To compute this you need to know the hash of the leaf's sibling node, since the parent is the hash of the concatenation of these two children. This is solved by sending this value (called a *witness*) as part of the proof:

![](docs/proof2.svg)

Now you need to compute the next parent's hash, which requires another witness. This continues on up the tree until you reach the top:

![](docs/proof3.svg)

* Whether the witness is the left child or the right child depends on the value of the path at that level. If it is a `1` then the witness is on the left, since the value is stored underneath the right node (and vice versa). You can think of a witness as a sub-tree that you don't care about, so you just need a summarised value that covers all of the nodes underneath it.
* There is a witness for every level of the tree. Since Quadrable uses collapsed leaves, this will be less than the full bit-length of the hash. [If we can assume](#proof-bloating) hashes are randomly distributed, then this will be roughly log2(N): That is, if there are a million items in the DB there will be around 20 witnesses. If there are a billion, 30 witnesses (this slow growth in witnesses relative to nodes illustrates the beauty of trees and logarithmic growth).
* The final computed hash is called the "candidate root". If this matches the trusted root, then the proof was successful and we have verified the JSON value is accurate. It is helpful to consider why this is the case: For a parent hash to be the same as another parent hash, the children hashes must be the same also, because we assume nobody can find collisions with our hash function. The same property then follows inductively to the next set of child nodes, all the way until you get to the leaves. So if there is any alteration in the leaf content or the structure of the tree, the candidate root will be different from the trusted root.


### Combined Proofs

The previous section described the simple implementation of merkle tree proofs. The proof sent would be those 4 blue witness nodes, usually in order from deepest to the root since this is how they will be accessed. To do the verification, it's just a matter of concatenating (using the corresponding bit from the path to determine the order) and hashing until you get to the root. The yellow nodes in the above diagrams are computed as part of verifying the proof.

This is pretty much as good as you can do with the proof for a single leaf (except perhaps to indicate empty sub-trees somehow so you don't need to send them along with the proof).

However, suppose we want to prove multiple values at the same time. Trivially, we could request separate proofs for each of them. Here are the two proofs for different leaves:

![](docs/proof4.svg)

To prove both of these values independently, the proofs would need 8 witnesses in total. However, observe the following:

* Since we are authenticating values from the same tree, the top 2 witnesses will be the same, and are therefore redundant.
* On the third level, the node that was sent as a witness in one proof is a computed node in the other proof, and vice versa. Since the verifier is going to be computing these values anyway, there is no need for the proof to contain *any* witnesses for this level.

After taking these observations into account, we see that if we are sending a combined proof for these two leaves, we actually only need to send 4 witnesses:

![](docs/proof5.svg)

By the way, consider the degenerate case of creating a proof for all of the leaves in a tree. In this case, no witnesses need to be sent at all, since the verifier will be constructing the entire tree anyways. Also, nothing additional needs to be sent to prove that a record does not exist, since the verifier has the entire set.




### Non-inclusion proofs

So far we have discussed proving that a queried value *exists* in the database. For many applications it is also necessary to prove that a value does *not* exist. These are called "non-inclusion proofs".

In a pure sparse merkle tree, every leaf is conceptually present in the tree, even if it is empty. In such systems it would be sufficient to provide an existence proof for the corresponding empty leaf. However, Quadrable uses the collapsed leaf optimisation which means that this will not work since the paths to the empty leaves might be blocked by collapsed leaves. Because of this, non-inclusion proofs are slightly more complicated, however this complexity is more than made up for by the reduction in proof sizes.

To provide a non-inclusion proof, Quadrable uses one of two methods, depending on the structure of the tree and the key hash of the queried record.

The first method is to present a branch where the corresponding child node is occupied by an empty sub-tree value (all zeros):

![](docs/non-inclusion-empty.svg)

* Note: As an optimisation, when providing the keyHash for a non-existing leaf in an empty non-inclusion proof the keyHash has its trailing bits zeroed out. For instance, in the diagram above, the record to be proven to not exist could have the key hash `101100101111001...` but the proof would instead prove that the record with key hash `101000000000000...` does not exist. This results in the same partial-tree, however the encoded proof contains more zero bytes, which are cheaper when included in calldata for an Ethereum smart contract.

The second method is to present a leaf node that is on the corresponding path, but has a different key hash. This conflicting leaf is called a "witness leaf":

![](docs/non-inclusion-witnessleaf.svg)

Both of these methods are proved in the same way as inclusion proofs: There is an untrusted value that will be hashed and then combined with witnesses up the tree until a candidate root node is reached. If this candidate root matches the trusted root then the non-inclusion proof is satisifed.

In fact, at the proof level there is no such thing as a non-inclusion proof. The proofs provide just enough information for the verifier to construct a tree that they can use to determine the key they are interested in does not exist.

Witness leaves are like regular proof-of-inclusion leaves except that a hash of the leaf's value is provided, not the leaf's value itself. This is because the verifier is not interested in this leaf's value (which could be large). Instead, they merely wish to prove that it is blocking the path to where their queried leaf would have lived in the tree. Note that it is possible for a leaf to be used for a non-inclusion proof instead of a witness leaf. This can happen if a query requests the value for this leaf *and* for a non-inclusion proof that can be satisifed by this leaf. In this case there is no need to send a witness leaf since the leaf can be used for both.




### Strands

![](docs/strands1.svg)

Quadrable's proof structure uses a concept of "strands". I'm not sure if this is the best way to formulate it, but it seems to result in fairly compact proofs. Furthermore, these proofs can be processed with a single pass over the proof data in resource-constrained environments such as smart contracts. After processing a proof, you end up with a ready-to-use partial-tree.

Each strand is related to a record whose value (or non-inclusion) is to be proven. Note that in some cases there will be fewer strands than records requested to be proven. This can happen when a witness reveals an empty sub-tree that is sufficient for satisfying a requested non-inclusion proof.

A Quadrable proof includes a list of strands, *sorted by the hashes of their keys*. Each strand contains the following:

* Hash of the key, or (optionally) the key itself
* Depth
* Record type
  * Leaf: A regular leaf value, suitable for satisfying a get or update request
  * WitnessLeaf: A leaf value, suitable for proving non-inclusion
  * Witness: An unspecified node, suitable for proving non-inclusion
* A value, the meaning of which depends on the record type:
  * Leaf: The leaf value (ie the result of a get query)
  * WitnessLeaf: The hash of the leaf value (allows to prover to create the nodeHash)
  * Witness: Unused

The first thing the verifier should do is run some initial setup on each strand (although this can be done lazily on first access instead, if desired):

* Hash the key (if key was included)
* Compute the strand's nodeHash: If the record type is Leaf or WitnessLeaf, compute the leaf nodeHash. If Witness, then use the key hash directly for this
* Set a `merged` boolean value to `false`
* Set a `next` index value to `i+1` where `i` is the node's index in the list, or an empty sentinel for the last strand (for example, `-1`). This functions as a singly-linked list of unmerged strands

### Commands

In addition to the list of strands, a Quadrable proof includes a list of commands. These are instructions on how to process the strands. After running all the commands, all the strands will have been merged into one strand and the root will be reconstructed. Assuming this candidate root matches the trusted root, the proof is considered verified.

Every command specifies a strand by its index in the strand list. After running a command this strand's depth will decrease by 1.

There are 3 types of commands ("ops"):

* `HashProvided`: Take the provided witness, concatenate it with the specified strand's nodeHash, and hash, storing the result into this strand's nodeHash. The order of concatenation depends on the bit of the strand's keyHash at position `depth`.
* `HashEmpty`: The same as the previous, but there is no provided witness. Instead, the empty sub-tree nodeHash (32 zero bytes) is used.
* `Merge`: Take this strand's nodeHash and concatenate and hash it with the nodeHash of the next un-merged strand in the strand list, which can be found by looking at the `next` linked list.
  * Check that the `merged` flag is false in the `next` strand, and that both strands are at the same depth.
  * After the merge, set `merged` to true in the `next` strand, and unlink it from the linked list.

After processing all commands, implementations should check the following:

* The first strand has an empty `next` linked list, meaning all strands have merged into the left-most strand
* The first strand has a depth of 0, meaning it has reached the root
* The first strand's nodeHash is equal to the trusted root

While processing commands, implementations should be creating nodes along the way for later querying. Technically this is optional and an implementation could just verify the root hash and then rely on the values included in the initial strand data. However, it is easier to make security-related mistakes with this approach. For example, suppose an implementation forgets to check that the first strand has an empty `next` linked list after processing the proof. In this case, an unauthenticated value could be in the initial strands that was never merged into the root. The tree-construction method "fails safe" in the presence of this mistake (among others) since this unauthenticated value will never get added to the created tree used for querying.

Furthermore, a tree structure will be required in order to compute a new root after making modifications to these values, so in this case you may as well create a partial-tree while processing the proof. And if you do support building a partial-tree like this, any other handling of the strand values is duplicated code, which should be avoided.



### Proof encodings

There are a variety of ways that proofs could be encoded (whether using the strands model or otherwise). The Quadrable C++ library has a conceptual separation between a proof and its encoding. There is a `quadrable::Proof` class, and it contains an abstract description of the proof. In order to serialize this to something that can be transmitted, there is a separate `encodeProof()` function. This function takes two arguments: The proof to encode and the encoding type. So far we have the following encoding types:

* `CompactNoKeys` (0): An encoding with strands and commands that will be described in the following sections. It tries to make the smallest proofs possible, but the optimizer still has room for improvement.
* `CompactWithKeys` (1): The same as the previous, but the keys (instead of the key hashes) are included in the proof. These proofs may be larger (or not) depending on the sizes of your keys. They will take slightly more CPU to verify than the no keys version, but at the end you will have a partial-tree that supports [key enumeration](#key-tracking).

Although new Quadrable proof encodings may be implemented in the future, the first byte will always indicate what encoding type is in use, and will correspond to the numbers in parentheses above. Since the two encoding types implemented so far are similar, we will describe them concurrently and point out the minor differences as they arise.

Although the C++ implementation has the `quadrable::Proof` intermediate representation of proofs which it converts encoded proofs into prior to processing, other implementations may choose to directly process the encoded form.




### Compact encoding

The first byte is the version byte described above, and then strands and commands are serialized:

    [1 byte proof type]
    [ProofStrand]+
    [ProofCmd]*

* The sort order is significant for both. For strands it must be sorted by key hash (even if keys are included, and not the keyHashes). For commands the order is the sequence that the commands will be processed.
* At the end of the lists of strands, a special "Invalid" strand signifies the end of the strand list, and that the commands follow.
* The commands are just processed until the end of the encoded string.

Here is how each strand is encoded:

    [1 byte strand type, Invalid means end of strands]
    [1 byte depth]
    if Leaf
      if CompactNoKeys:
        [32 byte keyHash]
      else if CompactWithKeys:
        [varint size of key]
        [N-byte key]
      [varint size of val]
      [N-byte val]
    else if WitnessLeaf
      [32 byte keyHash]
      [32 byte valHash]
    else if WitnessEmpty
      [32 byte keyHash]

* A varint is a BER (Binary Encoded Representation) "variable length integer". Specifically, it is in base 128 with the most significant digit first using the fewest possible digits, and with the most significant bit set on all but the last digit.
* The strand types are as follows:
  * `0`: Leaf (chosen to be 0 since this is probably the most common, and 0 bytes are cheaper in Ethereum calldata)
  * `1`: Invalid
  * `2`: WitnessLeaf
  * `3`: WitnessEmpty

The encoded commands are 1 byte each, and they do not correspond exactly with the commands described previously, although there is a straightforward conversion between the two. Here are the commands:

           hashing: 0[7 bits hashing details, all 0 == merge]
    short jump fwd: 100[5 bits distance]   jumps d+1, range: 1 to 32
    short jump rev: 101[5 bits distance]   jumps -(d+1) range: -1 to -32
     long jump fwd: 110[5 bits distance]   jumps 2^(d+6) range: 64, 128, 256, 512, ..., 2^37
     long jump rev: 111[5 bits distance]   jumps -2^(d+6) range: -64, -128, -256, -512, ..., -2^37

The hashing details are 7 bits that indicate a sequence of either hashing with a provided witness value or an empty sub-tree (32 zero bytes).

* Only 6 or fewer of the bits can actually be used for hashing directives. These bytes are padded with `0` bits, starting from the *least* significant bit, until a marker `1` bit is seen. The remaining bits are used (`0` means empty and `1` means provided witness). This way between 1 and 6 hashes can be applied per hashing byte.
* The witnesses are provided inline, that is they are the 32 bytes directly after the command byte. These 32 bytes are then skipped over and the next command is the following byte (unless more witnesses follow).
* If all 7 bits in the hashing details are `0` (there is no marker bit) then the command says to merge this strand with the next unmerged strand (which can be found via the `next` linked list). In the `next` strand, `merged` is set to true and this strand is unlinked.

The decoding algorithm keeps a variable that stores index into the strands. This is the current "working strand". The jump commands alter this index, so that subsequent hashing commands can work on other strands.

* The initial value of the working strand is the *last* (right-most) strand. This was an arbitrary choice, but usually the strands are worked on starting at the right, because left strands survive longer (the left-most one always becomes the final strand).
* The short jumps simply add one to the 5 bit distance and add or subtract this from the working strand
* The long jumps add `6` to the distance, and adds or subtracts that power of two from the working strand. This allows an implementation to rapidly jump nearby to the next desired strand, even if there are huge numbers of strands. It can then narrow in with subsequent long jumps until it gets within 32, and then use a short jump to go to the exact strand. This is sort of like a varint implementation, but fits into the simple "1 byte per command" model, and doesn't permit representation of zero-length jumps (which would be pointless to support)
* Implementations must check to make sure that a jump command does not jump outside of the list of strands. I though about making them "wrap" around, which could allow some clever encoding-time optimizations, especially if the number of strands is relatively prime with 2, but the added complexity didn't seem worth it.



### Proof bloating

Since the number of witnesses is proportional to the depth of the tree, given a nicely balanced tree the number of witnesses needed for a proof of one value is roughly the base-2 logarithm of the total number of nodes in the tree. Thanks to the beauty of logarithmic growth, this is usually quite manageable. A tree of a million records needs about 20 witnesses. A billion, 30 witnesses.

However, since our hashes are 256 bits long, in the worst case scenario we would need 255 witnesses. This happens when all the bits in two keyHashes are the same except for the last one (if *all* the bits were the same with two distinct keys, then records might get overwritten and the data structure would simply be broken). Fortunately, it is computationally expensive to find long shared hash prefixes.

In fact, this is one of the reasons we make sure to hash keys before using them as paths in our tree. If users were able to present hashes of keys to be inserted into the DB without having to provide the corresponding preimages (unhashed versions), then they could deliberately put values into the DB that cause very long proofs to be generated (compare also to [DoS attacks based on hash collisions](https://lwn.net/Articles/474912/)).

If keys can only be selected by trusted parties, then it may make sense to use special non-hash values for keys, which could considerably reduce proof sizes if values commonly requested together are given adjacent keys. Quadrable does not yet support this, but it would be a trivial modification.

So why are we worried about deep trees and their correspondingly large proof sizes? The worst case overhead for proving a single value is around 8,000 bytes and a couple hundred calls to the hash function, which is of course trivial for modern networks and CPUs. The issue is that the proof size and computational overhead of verification is part of the security attack surface for some protocols. For instance, if in an [optimistic rollup](https://docs.ethhub.io/ethereum-roadmap/layer-2-scaling/optimistic_rollups/) system verifying a fraud proof requires more gas than is allowed in a single block, then an attacker can get away with fraudulent transactions. The best solution to this is to keep the fraud-proof units granular enough that even heavily bloated proofs can be verified with a [reasonable amount of gas](#gas-usage). Counter-intuitively, this also means it is important for verification code to be gas-efficient, even if it is expected to never be called during normal operation of the protocol.

For a point of reference regarding how expensive it is to bloat proof sizes, at the time of writing Bitcoin block hashes start with about 75 leading 0 bits. Generating one of these earns about US $70,000. Unfortunately, for our situation we need to make a distinction. Bitcoin miners are specifically trying to generate block hashes with leading 0 bits (well, technically below a particular target value, but close enough). If somebody is trying to created bloated proofs, it may be sufficient to find *any* two keys with long hash prefixes. These attacks are easier because of the birthday effect, which is the observation that it's easier to find any two people with the same birthday than it is to find somebody with a specific birthday.

Depending on the protocol though, there may be more value in generating partial collisions for particular keys. You can imagine an attacker specifically trying to bloat a victim key so as to cause annoyance or expense to people who verify this key frequently.

There is a `quadb mineHash` command to help brute force search for a key who has a specific hash prefix. This is useful when writing tests for Quadrable, so that you can build up the exact tree you need.

One interesting consequence of the [caching optimization](#sparseness) that makes sparse merkle trees possible is that it is unnecessary to send the empty sub-tree witnesses along with a proof. One bit suffices to indicate whether a provided witness should be used, or a default hash is sufficient. The nice thing about this is that finding a long shared keyHash prefix for two keys doesn't really bloat the proof that much (but it does still increase the hashing overhead). To fully saturate the path, you need partial collisions at every depth. Unfortunately, exponential growth works *against* us here. To find an N-1 bit prefix collision is only half the work of finding an N bit one (in the specific victim key case). So summing the repeated fraction indicates that that saturating the witnesses only approximately doubles an attacker's work (in the birthday attack case they might get this as a free side effect).





## Storage

### Copy-On-Write

In the diagrams in above it shows nodes in the tree being modified during an update. This makes it easier to explain what is happening, but is not actually how the data structure is stored. To support multiple-versions of the tree, nodes are never modified. Instead, new nodes are added as needed, and they point to the old nodes in the places where the trees are identical.

In particular, when a leaf is added/modified, all of the branches on the way back up to the root need to be recreated. To illustrate this, here is the example from the [splitting leaves section](#splitting-leaves), but showing all the nodes that needed to be created (green), and how these nodes point back into the original tree (dotted lines):

![](docs/cow.svg)

Notice how references to the original tree remain valid after the update.

This copy-on-write behaviour is why our diagrams have the arrows pointing from parent to child. Most descriptions of merkle trees have the arrows pointing the other direction, because that is the direction the hashing is performed (you must hash the children before the parents). While this is still of course true in Quadrable, in our case we decided to draw the arrows indicating how the nodes reference each-other. This is also the order of traversal when looking up a record.

Since leaves are never deleted during an update, they can continue to exist in the database even when they are not reachable from any head (version of the database). These nodes can be cleaned up with a run of the [garbage collector](#garbage-collection). This scans all the trees to find unreachable nodes, and then deletes them.


### LMDB

Because traversing Quadrable's tree data-structure requires reading many small records, and these reads cannot be parallelised or pipelined, it is very important to be able to read records quickly and efficiently.

Quadrable uses the [Lightning Memory-mapped Database](https://symas.com/lmdb/). LMDB works by memory mapping a file and using the page cache as shared memory between all the processes/threads accessing the database. When a node is accessed in the database, no copying or decoding of data needs to happen. The node is already "in memory" and in the format needed for the traversal.

LMDB is a B-tree database, unlike Log-Structured-Merge (LSM) databases such as LevelDB that are commonly used for merkle tree storage. Compared to LevelDB, LMDB has radically better read performance and uses the CPU more efficiently. It has instant recovery after a crash, suffers from less write amplification, offers ACID transactions and multi-process concurrency (in addition to multi-thread), and is less likely to suffer data corruption.

LMDB supports multi-version concurrency control (MVCC). This is great for concurrency, because writers don't block readers, and readers don't block anybody (in fact there are no locks or system calls at all in the read path). But yes, this does mean that Quadrable has built a copy-on-write layer on top of a copy-on-write database. This is necessary because LMDB's MVCC snapshots are not persistent (they cannot outlive a single transaction), and because our nodes are more granular than LMDB's B-tree pages.

### nodeId

Some implementations of hash trees store leaves and nodes in a database, keyed by the node's hash. This has the nice property that records are automatically de-duplicated. Since a collision-resistant hash function is used, if two values have the same hash they can be assumed to be identical. This is called [content-addressable storage](https://git-scm.com/book/en/v2/Git-Internals-Git-Objects).

Quadrable does not do this. Instead, every time a node is added, a numeric incrementing `nodeId` is allocated and the node is stored with this key. Although records are not de-duplicated, there are several advantages to this scheme:

* Nodes are clustered together in the database based on when they were created. This takes advantage of the phenomenon known as [locality of reference](https://medium.com/@adamzerner/spatial-and-temporal-locality-for-dummies-b080f2799dd). In particular, the top few levels of nodes in a tree are likely to reside in the same B-tree page.
* When garbage collecting unneeded nodes, no locking or reference counting is required. A list of collectable nodeIds can be assembled using an LMDB read-only transaction, which does not interfere with any other transactions. The nodeIds it finds can simply be deleted from the tree, since nodeIds are never reused.
* Intermediate nodes don't store the hashes of their two children nodes, but instead just the nodeIds. This means they only occupy 8+8 bytes, rather than 32+32.

### nodeType

Internally there are several different types of nodes stored.

* **Branches**: These are interior nodes that reference one or two children nodes. In the case where one of the nodes is an empty node, a branch left/right node is used. If the right child is empty, a branch left node is used, and vice versa. If neither are empty then a branch both node is used. Note that a branch implies that there are 2 or more leaves somewhere underneath this node, otherwise the branch node would be collapsed to a leaf or would be empty.
* **Empty**: Empty nodes are not stored in the database, but instead are implicit children of branch left/right nodes.
* **Leaf**: These are collapsed leaves, and contain enough information to satisfy get/put/del operations. A hash of the key is stored, but not the key itself. This is (optionally) stored in a [separate table](#key-tracking).
* **Witness** and **WitnessLeaf**: These are nodes that exist in partial-trees. A Witness node could be standing in for either a branch or a leaf, but a WitnessLeaf could only be a Leaf. The only difference between a WitnessLeaf and a Leaf is that the WitnessLeaf only stores a hash of the value, not the value itself. This means that it cannot be used to satisfy a get request. However, it still could be used for non-inclusion purposes, or for updating/deletion.

### Node layout in storage

Nodes are stored as byte strings in the `quadrable_node` table in LMDB.

The first 8 bytes are a combination of a tag value which indicates the nodeType, and an optional nodeId. These 8 bytes are a uint64_t stored in native byte order (which, yes, means that a Quadrable database file is *not* portable between machines of different endianness). The nodeId is shifted left 8 bits and a single byte tag is bitwise or'ed in.

The following 32 bytes are a cache of the nodeHash of this node. In principle, this value could be reconstructed by recrawling the sub-tree beneath this node and rehashing the leaves and combining upwards, however this would ruin performance since the nodeHash is frequently needed, for example during tree updates and proof generation.

The meaning of the remaining bytes depend on the nodeType:

    branch left:  [8 bytes: \x01 | leftNodeId << 8]  [32 bytes: nodeHash]
    branch right: [8 bytes: \x02 | rightNodeId << 8] [32 bytes: nodeHash]
    branch both:  [8 bytes: \x03 | leftNodeId << 8]  [32 bytes: nodeHash] [8 bytes: right nodeId]
    leaf:         [8 bytes: \x04 | 0]                [32 bytes: nodeHash] [32 bytes: keyHash] [N bytes: val]
 
    witness:      [8 bytes: \x05 | 0]                [32 bytes: nodeHash]
    witnessLeaf:  [8 bytes: \x06 | 0]                [32 bytes: nodeHash] [32 bytes: keyHash] [32 bytes: valHash]


### Key tracking

Quadrable can run in two modes: with key-tracking enabled or disabled. By default, the C++ library has it disabled, but the `quadb` tool enables it (unless provided the `--noTrackKeys` option or the `QUADB_NOTRACKKEYS` environment variable).

Without key tracking, you cannot enumerate keys, because the database doesn't store them. It only stores the hashes. This has an effect on commands like `quadb export`:

    $ quadb checkout
    $ quadb --noTrackKeys put hello world
    $ quadb export
    H(?)=0x1c8aff950685...,world

It is slightly more efficient to not store keys in the database, so for large databases where enumeration is not needed, it might be advisable.




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

*Root* is the hash of the root node in your database. Provided the hash function is cryptographically secure, this is a globally unique identifier for the current state of the tree pointed to by your head. For an empty tree, [a special all-zero value](#sparseness) is used.

The number in parentheses after the root hash is the [nodeId](#nodeid). This is an internal value used by Quadrable and is shown here for informational purposes only.

### quadb put

This adds a new record to the database, or updates an existing one. On success there is no output:

    $ quadb put key val

Unless the value was the same as a previously existing one, the current head will be updated to have a new root, and a new nodeId will be allocated for it:

    $ quadb status
    Head: master
    Root: 0x0b84df4f4677733fe0956d3e4853868f54a64d0f86ecfcb3712c18e29bd8249c (1)

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

Note that the output is *not* sorted by the key. It is sorted by the hash of the key, because that is the way records are stored in the tree (see the section on [proof bloating](#proof-bloating)). You can pipe this output to the `sort` command if you would like it sorted by key.

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
    => temp : 0xf4f60482d2e639d24d6dfae605337968a86c404f5c41286987a916e40af21261 (2427)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

The `tempKey` record that we just inserted only exists in the `temp` head, and if we checkout back to master it would not be visible there.

Running `quadb checkout` with no head name will result in a [detached head](#heads) pointing to an empty tree.

### quadb fork

When we created a new head with checkout, it was initialised to an empty tree. Instead, we may choose to use `quadb fork` to copy the current head to the new head:

    $ quadb fork temp2
    $ ./quadb head
       temp : 0xf4f60482d2e639d24d6dfae605337968a86c404f5c41286987a916e40af21261 (2427)
    => temp2 : 0xf4f60482d2e639d24d6dfae605337968a86c404f5c41286987a916e40af21261 (2427)
       master : 0x0000000000000000000000000000000000000000000000000000000000000000 (0)

Our new `temp2` head starts off with the same root as `temp`. We can now modify `temp2` and it will not affect the `temp` tree.

Although semantically `quadb fork` acts like it copies the tree pointed to by the current head, no copying actually occurs. In fact, the two trees share the same structure so forking is a very inexpensive operation. Cheap database snapshots is an important feature of Quadrable, and is useful for a variety of tasks.

If no head name is passed in to `quadb fork`, it will fork to a [detached head](#heads).

`quadb fork` can optionally take `--from` flag which represents the head to be forked from, instead of using the current head.

### quadb diff

You can view the differences between the head you have checked out and another branch with `quadb diff`. If the heads are equivalent, there will be no output:

    $ quadb diff temp
    $

Let's add a new key, delete an existing one from our current branch, and run `diff` again:

    $ quadb put new test
    $ quadb del tempKey
    $ quadb diff temp
    -tempKey,tempVal
    +new,test

Now the diff shows one line per modification. If the modification was an insertion or update, the the first character will be `+` and the key/value will be the new record to be set. If it was a deletion, the first character will be `-` and the key/value will be the old record that has been removed.

* You can change the separator using the `--sep` option, just as with `import`/`export`.
* If two trees have been forked from one another recently, then diffs will be very fast. This is because the algorithm will detect shared portions of the tree and not bother diffing them. Diffing two trees that don't share structure (for example, if they were separately `import`ed from the same data) will still work, but will run slower. In the future we may implement a `dedup` command that uses `diff` to detect equal but unshared structure and make them shared.

### quadb patch

This command accepts a diff on standard input, and applies it to the current head.

* The format is the same as printed by `diff`
* Lines that start with `#` are treated as comments and are ignored
* The separator argument `--sep` must match what was used in the `diff` invocation

### quadb exportProof

This command constructs an encoded proof for the supplied keys against the current head, and then prints it to standard output:

    $ quadb exportProof --hex -- key1 "no such key"
    0x0000030e42f327ee3cfa7ccfc084a0bb68d05eb627610303012a67afbf1ecd9b0d32fa0568656c6c6f0201b5553de315e0edf504d9150af82dafa5c4667fa618ed0a6f19c69b41166c55100b42b6393c1f53060fe3ddbfcd7aadcca894465a5a438f69c87d790b2299b9b201a030ffe62a3cecb0c0557a8f4c2d648c7407bb5e90e2bd490e97e3447a0d4c081b7400

* The list of keys to prove should be provided as arguments. They can be keys that exist in the database (in which case their values are embedded into the proof), or keys that don't (in which case a non-inclusion proof is sent).
* The default [proof encoding](#proof-encodings) is `CompactNoKeys`, but this can be changed with `--format`.
* `--hex` causes the output to be in hexadecimal (with a `0x` prefix). By default raw binary data will be printed.
* The example above puts the keys after `--`. This is in case a key begins with `-` it won't be interpreted as a switch.
* `--dump` prints a human-readable version of the proof (pre-encoding). This can be helpful for debugging.

### quadb importProof

This is the opposite of `exportProof`. It takes the encoded proof and uses it to create a new partial tree.

The current head must be the empty tree (all 0 bytes):

    $ quadb checkout new-partial-tree

If the proof is stored in a file `my-proof`, import it like this:

    $ quadb importProof --hex < my-proof

The tree can now be read from and updated as usual, as long as no records that weren't part of the proof are accessed. Proofs can also be exported from this tree.

* `--hex` must be used if the proof is in hexadecimal.
* [Enumeration by key](#key-tracking) is only possible if the `CompactWithKeys` proof encoding was specified.

### quadb mergeProof

After importing a proof, if you receive additional proofs against the same database (meaning it has the same root), you can merge these proofs in too:

    $ quadb mergeProof --hex < my-proof2

* If the roots of the new proof differs from the root of your current head, and error will be thrown and the head will not be modified.
* On success, queries/updates/proofs can access any of the records in either proof.







## C++ Library

Quadrable's main functionality is implemented as a C++ header-only library that is intended to be used by applications such as the `quadb` command-line tool.

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

Although in the `quadb` application some values are presented in hexadecimal encoding, the Quadrable library itself does not use hexadecimal at all. You may find it convenient to use the `to_hex` and `from_hex` utilities used by `quadb` and the test-suite:

    #include "hoytech/hex.h"
    using hoytech::to_hex;
    using hoytech::from_hex;



### Heads

Most of the operations described in the `quadb` command-line application have counterparts in the C++ library.

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


## Solidity

In addition to the C++ library, there is also a [Solidity](https://solidity.readthedocs.io/en/v0.6.11/) implementation. Solidity is a programming language for implementing smart contracts on the Ethereum blockchain.

See the `README.md` file in [the solidity/ directory](https://github.com/hoytech/quadrable/tree/master/solidity) for details on how to compile and test the library.

Since using blockchain storage from a smart contract is very expensive, Quadrable does not require it. In fact, avoiding storage is one of the primary reasons you might use Quadrable: An authenticated data-structure allows a smart contract to perform read and write operations on a large data-set, even if that data-set does not exist in the blockchain state at all.


### Smart Contract Usage

First, copy the `Quadrable.sol` file into your project's `contracts` directory, and `import` it:

    import "./Quadrable.sol";

To validate a Quadrable proof in a smart contract, you need two items:

* `bytes encodedProof` - This is a variable-length byte-array, as output by [quadb exportProof](#quadb-exportproof) (must be `CompactNoKeys` encoding).
* `bytes32 trustedRoot` - This is a hash of a root node from a trusted source (perhaps from storage, or provided by a trusted user).

Once you have these, load the proof into memory with `Quadrable.importProof`. This creates a new tree and returns a memory address pointing to the root node:

    uint256 rootNodeAddr = Quadrable.importProof(encodedProof);

Use `Quadrable.getNodeHash` to retrieve the hash of this node and ensure that it is the same as the `trustedRoot`:

    require(Quadrable.getNodeHash(rootNodeAddr) == trustedRoot, "proof invalid");

Now that you have authenticated the partial tree created from the proof, you can begin to use it. First the key you are interested in must be hashed:

    bytes32 keyHash = keccak256(abi.encodePacked("my key"));

Now `Quadrable.get` can be called. It returns two items: `found` is a boolean indicating whether the key is present in the tree. If it is true, then `val` will contain the corresponding value for the provided key. If `found` is false, then `val` will be empty (0 length):

    (bool found, bytes memory val) = Quadrable.get(rootNodeAddr, keyHash);

Finally, you can modify the tree with `Quadrable.put`. It may return a new `rootNodeAddr`, which you should save:

    rootNodeAddr = Quadrable.put(rootNodeAddr, keyHash, "new val");

`Quadrable.getNodeHash` can be used to retrieve the updated root incorporating your modifications:

    bytes32 newTrustedRoot = Quadrable.getNodeHash(rootNodeAddr);



### Memory Layout

See the [Strands](#strands) section for details on how the proof decoding algorithm works. The solidity implementation is similar to the C++ implementation, except that it does not decode the proof to an intermediate format prior to processing. Instead, it directly processes the encoded proof for efficiency reasons.

Because the number of strands is not known in advance and Solidity does not support resizing dynamic memory arrays, the function that parses the strands is careful to not allocate any memory in order to support prcessing the proof in a single pass. Instead, as it executes it builds up a contiguous array of strand elements. Each strand element contains a 32-byte strandState, a keyHash, and a node that will store the leaf for this strand (if any):

    Strand element (128 bytes):
        uint256 strandState: [0 padding...] [1 byte: depth] [1 byte: merged] [4 bytes: next] [4 bytes: nodeAddr]
        [32 bytes: keyHash]
        [64 bytes: possibly containing leaf node for this strand]

The strandState contains the working information needed while processing the proof, and the keyHash for this strand.

Each node is 64 bytes and consists of a 32-byte nodeContents followed by a 32-byte nodeHash. The nodeContents uses the least significant byte to indicate the type of the node, and the rest is specific to the nodeType as follows:

    Node (64 bytes):
        uint256 nodeContents: [0 padding...] [nodeType specific (see below)] [1 byte: nodeType]
                   Leaf: [4 bytes: valAddr] [4 bytes: valLen] [4 bytes: keyHashAddr]
            WitnessLeaf: [4 bytes: keyHashAddr]
                Witness: unused
                 Branch: [4 bytes: parentNodeAddr] [4 bytes: leftNodeAddr] [4 bytes: rightNodeAddr]
        bytes32 nodeHash

* `parentNodeAddr` is only used as a temporary scratch area of memory during tree updates, to avoid recursion.



### Limitations

* Only the `CompactNoKeys` proof encoding is supported. This means that enumeration by key is not possible.
* Unlike the C++ library, the Solidity implementation does not support deletion. This may be implemented in the future, but for now protocols should use some sensible empty-like value if they wish to support removals (such as all 0 bytes, or the empty string).
* The Solidity implementation does *not* use [copy-on-write](#copy-on-write), so multiple versions of the tree can not exist simultaneously. Instead, the tree is updated in-place during modifications (nodes are reused when possible). This is done to limit the amount of memory consumed.
* Unlike the C++ implementation operations are not [batched](#operation-batching). This is complicated to do in Solidity because dynamic memory management is difficult. Nevertheless, this may be an area for future optimisation.
* Proofs cannot be created by the Solidity implementation. This should not be necessary for most use-cases.
* Large proofs can have [excessive gas costs](#gas-usage).


### Gas Usage

There are several variables than impact the gas usage of the library:

* Size of the database and distribution of its keys
* Number of elements to be proven
* Distribution of *their* keys
* Proportion of inclusion versus non-inclusion proofs
* Length of the values

Following is a generated table of gas costs for a simple scenario. For each row, a DB of size N is created with effectively random keys. One element is selected to be proven (an inclusion proof). The proof size is recorded and this is used to estimate calldata costs. Then the gas costs are measured by the test harness for 3 operations: Importing the proof, looking up the value in the partial tree, and updating the value and computing a new root.

| DB Size (N) | Average Depth | Calldata (gas) | Import (gas) | Query (gas) | Update (gas) | Total (gas) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 | 1216 | 2722 | 1622 | 1693 | 7253 |
| 10 | 3.3 | 6368 | 8582 | 3651 | 8376 | 26977 |
| 100 | 6.6 | 7392 | 8848 | 3651 | 8377 | 28268 |
| 1000 | 10 | 11520 | 14014 | 5356 | 13977 | 44867 |
| 10000 | 13.3 | 14624 | 18887 | 7031 | 19518 | 60060 |
| 100000 | 16.6 | 19776 | 23401 | 8031 | 22830 | 74038 |
| 1000000 | 19.9 | 22848 | 25083 | 8697 | 25038 | 81666 |

* The gas usage is roughly proportional to the number of witnesses provided with the proof. Because of logarithmic growth, the DB size can grow quite large without raising the gas cost considerably.
* The calldata estimate is slightly high since it doesn't account for zero bytes.

Now consider the following test. Here we have setup a DB with 1 million records (the same configuration as the last row in the previous test). Each row creates a proof proving the inclusion of `N` different records. This results in a proof with `N` [strands](#strands). Each of the `N` records is queried and then updated (with no batching).

| Num Strands (N) | Approx Witnesses | Calldata (gas) | Import (gas) | Query (gas) | Update (gas) | Total (gas) |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 19.9 | 22848 | 25083 | 8697 | 25038 | 81666 |
| 2 | 39.9 | 41632 | 50833 | 17683 | 51156 | 161304 |
| 4 | 79.7 | 79136 | 97768 | 36112 | 104747 | 317763 |
| 8 | 159.5 | 144736 | 174654 | 70738 | 204839 | 594967 |
| 16 | 318.9 | 260000 | 328373 | 138122 | 398654 | 1125149 |
| 32 | 637.8 | 508768 | 643279 | 278367 | 803794 | 2234208 |
| 64 | 1275.6 | 971712 | 1265301 | 564306 | 1630652 | 4431971 |
| 128 | 2551.2 | 1789600 | 2485351 | 1138851 | 3288189 | 8701991 |

* The number of witnesses is roughly the number of strands times the average depth of the tree (fixed at 19.9). This estimate is slightly high because it doesn't account for the witnesses omited due to [combined proofs](#combined-proofs). Better proxies for this estimate are proof size or (equivalently) calldata gas: Observe that when the number of strands doubles, the calldata gas less than doubles. This effect will become more pronounced with smaller DB sizes or larger number of strands.

In general, the gas cost is proportional to the number of witnesses in the proof, which is roughly the average depth of the tree times the number of values to be proven. To determine the gas cost for calldata, importing the proof, querying, and updating, take this number and multiple it by 5000 (very rough estimate). The gas cost technically isn't linear, since (among other things) the cost of memory increases quadratically, but this estimate seems to hold for reasonable parameter sizes.

For optimistic roll-up applications, proofs only need to be supplied in the case a fraudulent action is detected. If the system is well designed, then game-theoretically the frequency of this should be "never". Because of this, typical gas costs aren't the primary concern. The bigger issue is the worst-case gas usage in an [adversarial environment](#proof-bloating). If an attacker manages to make it so costly for the system to verify a fraud proof that it cannot be done within the block-gas limit (the maximum gas that a transaction can consume, at any cost), then there is an opportunity for fraud to be committed.

Let's assume that an attacker can create colliding keyHashes up to a depth of 160 for every element to be proven. This would be extremely computationally expensive -- on the same order as finding distinct private keys with colliding bitcoin/ethereum addresses. In this case, calldata+import+query+update would take around 800k gas for each value. At the time of this writing, the gas block limit is 12.5m, which suggests that around 15 of these worst-case scenario values could be verified. In order to leave a very wide security margin, this suggests that fraud-proof systems should try to use 15 or fewer values for each unit of verification (assuming other gas costs are negligible).


## Author and Copyright

Quadrable © 2020 Doug Hoyte.

2-clause BSD license. See the LICENSE file.

Does this stuff interest you? Subscribe for news on my upcoming book: [Zero Copy](https://leanpub.com/zerocopy)!
