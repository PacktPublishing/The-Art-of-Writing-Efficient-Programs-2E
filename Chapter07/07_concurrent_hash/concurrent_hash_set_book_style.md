# The Concurrent Hash Set

## What is a hash table?

Before we deal with concurrency, let us recall what a hash table is and why it delivers its
famous O(1) lookup. The key idea is to convert the search key — which can be arbitrary data of
any size and type — into an integer, using a hash function. This conversion is necessarily
lossy: we are mapping an unbounded domain of possible keys onto a finite set of integer values,
so two different keys can produce the same hash. At its core, the hash table is simply an
array, and the hash value, folded into the valid range of array indices (in our case, by
masking against a power-of-two size), selects the element. Collisions at an element are
therefore unavoidable, and for two independent reasons: different keys may hash to the same
64-bit value, and even different hash values are folded onto the same index, since the array is
vastly smaller than 2^64. Every hash table must provide some way to attach multiple values to
the same array element — open addressing, chaining, cuckoo displacement — and the data
structure we are about to study chains them into a linked list per array element (per
*bucket*). For the same reason that collisions exist, finding a candidate element in a bucket
is not the same as finding the key: each candidate must be compared to the search key for full
equality, because matching the hash value, let alone the folded index, proves nothing.

## Overview

The `ConcurrentResizableHashSet<T>` is a chained concurrent hash set. If you have ever tried to
design a concurrent hash table, you know that two problems dominate the effort: safe memory
reclamation (when is it safe to delete a node that another thread may still be reading?) and
resizing (how do we rehash the table while readers are traversing it?). Textbook solutions
exist for both — hazard pointers, epochs, and reference counting for the first;
reader-writer locks and incremental rehashing for the second — and all of them share an
unpleasant property: they tax the common path to pay for the rare event.

This data structure takes a different route. It does not solve either problem; it arranges for
neither problem to exist. The entire design rests on three decisions, each of which will get
its own section, and each of which can be held in one line until then: nodes, once allocated,
are never freed, moved, or reused before the set itself is destroyed; a growing table never
relinks its chains — new buckets are populated lazily, by copying; and deletion sets a mark on
a node rather than unlinking it. Everything that follows is the working-out of these three
refusals — what they make trivially safe, what they cost in memory, and where those costs
surface.

The underlying `ConcurrentAppendDeque` was studied earlier, and we take it as given. The hash
set relies on only three of its properties: element access through `operator[]` is wait-free
and element addresses are stable forever (block storage, retired directories); `emplace_back`
and `resize` are serialized internally and publish new elements with a release store of the
size; and `size()` is an acquire load of that same value.

One more thing is worth understanding before we look at the class, because it is a genuine
design decision and not a mere implementation detail: where the data lives. The set is built
from two of these deques, and they play very different roles. The array that the hash function
actually indexes — the bucket array — never stores an element; it stores *indices*. The
elements themselves live in a second, grow-only array of nodes, and they are appended to it in
the order in which they arrive. An element's position in that array carries no information
whatsoever: it is whichever slot the allocator handed out next, and the element's hash value
has no influence on it.

The consequence of this indirection is a complete decoupling of the hash geometry from the
data placement. In a conventional hash table, the hash determines where an element physically
resides, and therefore growing the table means physically relocating elements — which is
precisely what makes resizing so hostile to concurrent readers. Here, the hash determines only
an entry in an array of small integers. When the table grows, it is the index array that
changes; the data never moves — it only accumulates. Every hard problem that resizing poses
will be attacked at the level of those integers, while the elements themselves sit still.

## The interface

Before we look inside, let us lay out the class and the contract it presents to its callers;
it is much easier to see where each implementation piece fits when the whole is visible first.
The public face of the hash set is deliberately small:

```cpp
template <typename T, typename... Args>
using DefaultConcurrentDeque = ConcurrentAppendDeque<T, 1024>;

template <
    typename T,
    bool AllowDelete = false,
    typename Hash = std::hash<T>,
    template <typename, typename...> class Container = DefaultConcurrentDeque
>
class ConcurrentResizableHashSet {
public:
    struct Node {
        T value;
        std::atomic<size_t> next_bucket_node_idx;
    };

    ConcurrentResizableHashSet(size_t initial_capacity = 4);

    bool contains(const T& key);
    bool insert(const T& key);
    bool erase(const T& key) requires AllowDelete;

    size_t get_internal_node_count() const;   // exposed for testing
};
```

The template parameters first. `T` is the element type; it must be copy-constructible and
equality-comparable, and — since the container copies elements internally during splits —
copies must be equivalent to their originals. `Hash` is invoked as `Hash{}(key)` at every use,
so it must be default-constructible and stateless: two threads constructing two `Hash` objects
must get the same function. `Container` is the pluggable backing store; anything that provides
the three guarantees we required of the `ConcurrentAppendDeque` — wait-free element access at
stable addresses, internally serialized growth, and a release-published size — can be
substituted. `AllowDelete` is a compile-time policy switch: when it is `false` (the default),
the `erase` method does not merely fail — it does not exist, and the type system enforces a
delete-free usage pattern at no runtime cost. This is our chapter's recommendation to exploit
application-specific restrictions, promoted into the type signature.

Now the contract, operation by operation. The container is a set of unique values (a
closed-addressing, or chained, hash set, in the taxonomy of the opening section), safe for
any number of threads to use concurrently, in any mix of operations, with no external
synchronization. `insert(key)` returns `true` if and only if the key was absent and *this
call* inserted it; among any number of threads concurrently inserting the same key, exactly
one receives `true`, which allows a caller to use the return value to elect an owner ("the
thread that inserted the key initializes its payload"). `contains(key)` reports whether the
key was present at some moment during the call. `erase(key)` returns `true` for exactly one of
any set of concurrent erasers of a live key, and `false` if the key was absent or already
deleted. All three return-value guarantees hold across concurrent resizes; we will see what
that costs to preserve.

The progress guarantees are part of the contract, just as they were for the deque, and they
are the reason this data structure exists. Readers are wait-free: `contains` performs no
atomic read-modify-write operations and acquires no locks, with one exception we will meet
shortly — the first operation to touch a freshly created bucket performs its split, which
allocates under a lock, on behalf of everyone who comes after. This, incidentally, is why
`contains` is not declared `const`, and the signature is telling you the truth: a search is
logically a query but physically a potential writer — it may populate a bucket, allocating
nodes into shared state as it does so. There is a convention at work here that deserves a
moment of attention. For externally synchronized types — which is to say, for nearly the
entire standard library — `const` carries a thread-safety promise: a `const` member function
is data-race-free against other `const` calls precisely because it writes nothing, and the
signature tells the caller when locking is their problem. Internally synchronized data
structures step outside this convention as a matter of course — our deque lets you call
anything at any time, `const` or not — because the thread-safety promise has migrated from
the individual signature up to the class contract, where it is stated once: every operation,
any mix, any number of threads. That migration leaves `const` free to revert to its literal
meaning — *does this function modify the object* — and we use it honestly in that role:
`get_internal_node_count` is `const` because it writes nothing; `contains` is not, because it
may. Writers are lock-free at the point of publication — the insertion and the
deletion each commit through a single
compare-and-swap that some thread always wins — with one short locked section for node
allocation. The resize is serialized, but it is brief and it blocks no one but a competing
resizer; every other thread, reader or writer, proceeds through it.

Like the deque beneath it, the set is neither copyable nor movable: its address is, in
effect, part of its identity, since every thread using it holds a pointer to the one shared
instance. This does not prevent returning one from a factory function — hold it by
`std::unique_ptr`, the same way one handles any immovable type — it only prevents the object
itself from traveling. As for exceptions, we continue the policy we have held throughout this
chapter: thread safety first, exception safety set aside. There is a genuine tension between
the two at the interface level (the thread-safe queue was the prime example: it must move
elements *out* through its interface, and what if that move throws, mid-transaction?), but
high-throughput concurrent data structures in practice hold simple types — integers, indices,
pointers — whose copies do not throw, and the set's internal machinery is arranged so that
the one plausible thrower, allocation, cannot corrupt the structure. We will note that
arrangement where it appears and otherwise leave the subject alone.

Note what is deliberately absent: there is no `size()`, no iteration, no `clear()`, no
rehash-control knobs. This is the minimal-interface principle from this chapter's
recommendations applied without sentiment. A `size()` on a concurrent set is a value that is
stale before it returns; iteration over a structure that other threads are mutating is not a
well-defined transaction at all. Every operation the class does offer is transactional — it
has a well-defined result for any state of the set and any concurrent activity. The one extra
method, `get_internal_node_count()`, is a testing window, and it reports *allocations*, not
live keys; the distinction will matter repeatedly in what follows.

Two more clauses of the contract concern lifetime. The constructor takes an initial capacity,
which is rounded up to a power of two (and to at least 4); as we will see, choosing it
generously is not merely a performance nicety. And the destructor, as with every data
structure in this chapter, is not a concurrent operation: all threads must be done with the
set before one thread destroys it. Nothing else is ever freed — memory is reclaimed exactly
once, at destruction — which is not a footnote but a defining architectural property, and the
next sections show what it buys.

## The structure

Where the public interface is the contract, the private part of the class is the map of the
whole design; every implementation section that follows explains one or two of these members,
so it is worth seeing them all in one place first:

```cpp
private:
    // Bucket heads: each holds the index of the first node of its chain.
    Container<std::atomic<size_t>> buckets_;

    // The node arena: append-only; nodes are never moved, reused, or freed
    // before the set is destroyed.
    Container<Node> data_;

    // The logical bucket count. Always a power of 2; grows by doubling.
    std::atomic<size_t> table_size_;

    // Serializes table doubling (double-checked locking).
    SpinLock resize_lock_;

    // Serializes node allocation into data_.
    SpinLock data_alloc_lock_;

    static constexpr size_t MARK_BIT      = 1ULL << 63;
    static constexpr size_t EMPTY         = ~MARK_BIT;
    static constexpr size_t UNINITIALIZED = EMPTY - 1;
    static constexpr size_t PTR_MASK      = ~MARK_BIT;
```

Two containers, one atomic scalar, two spinlocks, four constants — that is the entire state.
The first of our three one-line decisions is already visible: `data_` is a node *arena* — an
append-only store in which a node, once created, lives at a permanent index until the set is
destroyed — and the chains are linked by those indices, not by pointers. Notice immediately
what is *not* here: no per-node reference counts, no hazard-pointer slots,
no epoch counters, no free lists, no reader registration of any kind. Notice also that
`buckets_` may be physically longer than `table_size_`: the deque only grows, while
`table_size_` defines how much of it is currently in play. In memory, the pieces fit together
like this:

```
                buckets_ (logical size = table_size_)
                +-----+-----+-----+-----+-----+-----+-----+-----+
     index:     |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
     head:      |  5  |EMPTY|  2  |  0  |UNIN.|  7  |UNIN.|UNIN.|
                +--+--+-----+--+--+--+--+-----+--+--+-----+-----+
                   |           |     |           |
                   v           v     v           v
     data_ (append-only arena, indices are permanent):
     +-------+-------+-------+-------+-------+-------+-------+-------+
 idx:|   0   |   1   |   2   |   3   |   4   |   5   |   6   |   7   |
 val:|  19   |  42   |  10   |   6   |  99   |  35   |  11   |  13   |
 nxt:| EMPTY | M|EMPT|   6   | EMPTY |   1   |   4   | EMPTY | EMPTY |
     +-------+-------+-------+-------+-------+-------+-------+-------+
                ^
                tombstoned (MARK_BIT set on its next word)

     bucket 0 chain: 5 -> 4 -> 1(dead) -> EMPTY
     bucket 2 chain: 2 -> 6 -> EMPTY
```

We have already seen the node in the class declaration — a value plus an atomic next-index —
and now is the time to look at it closely:

```cpp
struct Node {
    T value;
    std::atomic<size_t> next_bucket_node_idx;
};
```

Notice what is atomic here and what is not. The value is written once, before the node becomes
visible to other threads, and is never modified afterward; it needs no synchronization of its
own. All mutation after publication happens on the single 64-bit `next_bucket_node_idx` word.
This one word plays three roles at once — the link to the next node, the tombstone flag, and
the end-of-chain sentinel:

```
  bit 63                                bits 62..0
 +--------+------------------------------------------------------+
 | MARK   |                 index into data_                     |
 +--------+------------------------------------------------------+

 MARK_BIT      = 1ULL << 63     tombstone flag (on the *owning* node's next word)
 PTR_MASK      = ~MARK_BIT      extracts the index
 EMPTY         = ~MARK_BIT      end-of-chain / empty-bucket sentinel (0x7FFF...FFFF)
 UNINITIALIZED = EMPTY - 1      bucket exists physically but has not been split yet
```

There are two subtleties in this encoding, and both are important. First, marking a node as
deleted means setting `MARK_BIT` on *its own* next word, not on the link that points to it. The
mark travels with the node, so a marked node remains fully traversable: we can still read its
next pointer and continue down the chain. Second, because `EMPTY` occupies the
all-ones-below-the-mark pattern, a tombstoned node at the tail of a chain holds
`EMPTY | MARK_BIT`, which is `SIZE_MAX`. Every traversal must therefore test for the end of the
chain on the *masked* value:

```cpp
while ((curr & PTR_MASK) != EMPTY) { ... }
```

A naive `curr != EMPTY` test would sail right past a marked tail node and attempt to index
`data_[SIZE_MAX & PTR_MASK]` — a segmentation fault waiting to happen (and, in an earlier
version of this code, not waiting very long). This is the price we pay for packing the
sentinel into the same value space as the indices: every comparison against `EMPTY`,
everywhere in the code, must be mask-aware, forever. Note also that
`UNINITIALIZED & PTR_MASK` is *not* equal to `EMPTY`; this is safe only because
`UNINITIALIZED` can appear in bucket heads but never in a node's next word.

## Buckets and their parents

Since `table_size_` is always a power of two, the bucket of a key is simply
`Hash{}(key) & (ts - 1)`. Now consider what happens when the table doubles from N to 2N
buckets. One more bit of the hash value participates in the index, so each new bucket `j`
(where N ≤ j < 2N) receives exactly the elements of one old bucket — we will call it the
**parent** — whose new high hash bit is set:

```cpp
size_t parent = j - std::bit_floor(j);   // clear the top set bit of j
```

```
        table_size_ = 4                       table_size_ = 8
      hash & 0b011                          hash & 0b111

      bucket 0  ---------------------->  bucket 0   (keys with hash ≡ 0 mod 8)
                 \___ split ___________  bucket 4   (keys with hash ≡ 4 mod 8)
      bucket 1  ---------------------->  bucket 1
                 \_____________________  bucket 5
      bucket 2  ---------------------->  bucket 2
                 \_____________________  bucket 6
      bucket 3  ---------------------->  bucket 3
                 \_____________________  bucket 7
```

Readers familiar with the literature will recognize the recursive-split addressing of the
Shalev–Shamir split-ordered lists. The resemblance, however, ends at the addressing scheme:
split-ordered lists relink a single shared ordered list, whereas our table *copies* the
elements, as we will see shortly. Because splits are performed lazily, a bucket's parent may
itself still be uninitialized when we come to split the bucket; in that case, the split
recurses up the ancestor chain — `j`, then `parent(j)`, then `parent(parent(j))`, and so on —
until it reaches an initialized ancestor. The depth of this recursion is bounded by the number
of table doublings since that particular lineage of buckets was last touched.

## Searching the hash set

Let us start with the simpler operation: the search. It is wait-free in the sense that a
reader performs no atomic read-modify-write operations and never waits on a lock. There is one
exception to this claim: a reader that stumbles onto an `UNINITIALIZED` bucket performs the
split itself, which allocates nodes under a spinlock. This *cooperative splitting* deliberately
sacrifices strict wait-freedom on the first touch of each new bucket; every subsequent access
to that bucket is wait-free again.

```cpp
bool contains(const T& key) {
    size_t ts = table_size_.load(std::memory_order_acquire);
    while (true) {
        size_t j = Hash{}(key) & (ts - 1);
        size_t head = buckets_[j].load(std::memory_order_acquire);
        if (head == UNINITIALIZED) {
            split_bucket(j);
            head = buckets_[j].load(std::memory_order_acquire);
        }
        bool found = false;
        size_t curr = head;
        while ((curr & PTR_MASK) != EMPTY) {
            size_t actual_curr = curr & PTR_MASK;
            size_t check_curr =
                data_[actual_curr].next_bucket_node_idx.load(std::memory_order_acquire);
            if (data_[actual_curr].value == key && !(check_curr & MARK_BIT)) {
                found = true; break;
            }
            curr = check_curr;
        }
        if (found) return true;

        size_t new_ts = table_size_.load(std::memory_order_acquire);
        if (new_ts == ts) return false;
        ts = new_ts;                       // the table grew under us: retry in the new geometry
    }
}
```

Two details of this loop deserve close attention.

The first is the double duty performed by the load of the next pointer. Each node costs us
exactly one atomic acquire load: the value `check_curr` serves simultaneously as the liveness
test — `check_curr & MARK_BIT` tells us whether *this* node has been tombstoned — and as the
step to the next node. There is no separate "is this node deleted" flag to load. The inner
loop is therefore a pure pointer chase, and its throughput is bounded by cache-miss latency;
this is precisely the property that the read benchmarks, with their hundreds of millions of
lookups per second, exploit.

The second detail is the asymmetry between the positive and the negative result. A positive
result returns immediately: once we have observed a live node with a matching key, the answer
"true" was correct at that instant, whether or not a resize is in progress. A *negative*
result, however, is suspect. Our search may have scanned bucket `j` under an old table size
while a concurrent insertion placed the key into bucket `j + N` of the doubled table — a
bucket we never looked at. This is the purpose of the trailing recheck of `table_size_`: we
return "not found" only if the table geometry was stable across our entire scan; otherwise, we
restart the search under the new size. Remember this validate-only-the-negative pattern; we
will meet it again in `erase`.

## Splitting a bucket

A freshly created bucket holds the value `UNINITIALIZED`. The first operation to touch it —
whether a search, an insertion, or a deletion — performs the split on behalf of everyone:

```cpp
void split_bucket(size_t j) {
    size_t parent = j - std::bit_floor(j);
    size_t parent_head = buckets_[parent].load(std::memory_order_acquire);
    if (parent_head == UNINITIALIZED) {           // the ancestor is not split yet:
        split_bucket(parent);                     // recurse up the lineage
        parent_head = buckets_[parent].load(std::memory_order_acquire);
    }
    size_t N = std::bit_floor(j);
    size_t mask = (N << 1) - 1;                   // the mask of the size at which j was born

    size_t new_subchain_head = EMPTY;
    size_t curr = parent_head;
    while ((curr & PTR_MASK) != EMPTY) {
        size_t actual_curr = curr & PTR_MASK;
        T val = data_[actual_curr].value;
        size_t next_raw =
            data_[actual_curr].next_bucket_node_idx.load(std::memory_order_acquire);
        if (!(next_raw & MARK_BIT)) {             // skip tombstones: this is where GC happens
            if ((Hash{}(val) & mask) == j) {
                new_subchain_head = alloc_node(val, new_subchain_head);  // copy, prepend
            }
        }
        curr = next_raw;
    }
    size_t expected = UNINITIALIZED;
    buckets_[j].compare_exchange_strong(expected, new_subchain_head,
                                        std::memory_order_release,
                                        std::memory_order_relaxed);
    // If the CAS fails, the speculative subchain is silently abandoned in the arena.
}
```

```
  Before the split of bucket 4 (table 4 -> 8, parent = 0):

  buckets_[0] --> [k=16] --> [k=12] --> [k=20] --> [k=8, DEAD] --> EMPTY
  buckets_[4] = UNINITIALIZED

  The splitter scans the parent chain, keeps the live nodes with (hash & 7) == 4,
  COPIES them into new arena nodes, and CASes the copy in:

  buckets_[0] --> [k=16] --> [k=12] --> [k=20] --> [k=8, DEAD] --> EMPTY   (untouched!)
  buckets_[4] --> [k=20'] --> [k=12'] --> EMPTY                            (fresh copies)
                                          (k=8 is dropped: the tombstone is collected)
```

Several important architectural decisions are concentrated in this one function, so let us
take them one at a time.

We copy; we do not relink. The parent chain is never modified by a split. Concurrent readers
of the parent bucket are completely unaffected, and concurrent readers of bucket `j` see
either `UNINITIALIZED` (in which case they perform the split themselves) or a complete chain.
The cost of this simplicity is that the keys that now belong to bucket `j` remain in the
parent's chain as stale duplicates — forever. The parent chain never shrinks; the chain of
bucket 0, in particular, is append-only for the lifetime of the set.

The split is speculative, and publication is a single CAS. Multiple threads may race to split
the same bucket; each builds a private subchain out of fresh arena nodes and attempts one CAS
from `UNINITIALIZED` to the head of its subchain. Exactly one thread wins. The losers abandon
their subchains as unreachable garbage in the arena, and no retry is needed: a lost CAS means
that someone else has already completed the identical job. This is our publishing protocol
again, in its concurrent form — the new data is constructed privately, with no concern for
thread safety, and made visible in one atomic step.

The split is also the garbage collector. Marked nodes fail the `!(next_raw & MARK_BIT)` test
and are simply not copied. A split is thus the only point at which logical deletions become
physical — but note carefully that this happens only in the *child's* view; the dead node
itself remains in the parent chain and in the arena.

Now we come to the central correctness question of the copy-on-split design, and it is a
question you should ask yourself before reading further: if stale copies of moved keys
accumulate in the parent chains, and future splits rescan those chains, what prevents a stale
copy from being resurrected into a live bucket? Nothing in the code prevents it — no flag, no
version counter. What prevents it is arithmetic. A stale entry in the chain of parent `p` is a
key with `hash ≡ p + N (mod 2N)` for some past doubling N; that is what it means for the key
to have moved to child `p + N`. The parent `p` acquires a new child `p + S` at every
subsequent doubling, S = 2N, 4N, and so on, and the split of `p + S` does scan the full parent
chain, stale entries included. But the filter for child `p + S` demands `hash ≡ p (mod S)`,
which implies `hash ≡ p (mod 2N)` — and that contradicts `hash ≡ p + N (mod 2N)`. The stale
entry therefore fails the mask test of every future child of `p`, unconditionally. Stale
copies cost us scan time and memory in the ancestor buckets, but they can never become a
correctness hazard. Keep this argument in mind; it will also cover the leftovers created by
the insertion fallback in the next section.

Finally, a word about allocation. The function `alloc_node` is a bump allocator over the
arena, serialized by a spinlock:

```cpp
size_t alloc_node(const T& val, size_t next) {
    std::lock_guard lock(data_alloc_lock_);
    data_.emplace_back(val, next);
    return data_.size() - 1;
}
```

Notice the deliberate absence of a separate atomic node counter: the arena's own `size()` — an
acquire load of the value the deque publishes with a release store — *is* the counter. Beyond
saving an atomic `fetch_add`, this is an exception-safety decision. If `emplace_back` throws,
no counter has been incremented ahead of time, so nothing can become permanently
desynchronized. The spinlock also means that `alloc_node` is the one place where our
"lock-free insertion" is not literally lock-free; the design accepts a short critical section
for allocation while keeping the *publication* path — the bucket CAS — lock-free.

But before we accept that critical section, it is worth asking what this lock actually
protects. Not the append: the deque serializes its appends internally and needs no help from
us. Look closely at the two lines under the lock — the lock exists solely to fuse the append
with the observation of *where the appended element landed*. The deque knew the index at the
moment of insertion, inside its own critical section, where the answer was free; the classic
container API then throws that knowledge away, returning nothing (or, since C++17, a
reference to the element — a receipt to the object, but not to its position). The caller is
forced to reconstruct the discarded fact with a separate call to `size()`, and between the
two calls the world may move, so a second lock appears whose entire purpose is to compensate
for an interface that lost a value it already had. You have seen this disease before: it is
`front()` followed by `pop()`, the pair of calls that forced a lock around the thread-safe
queue, in a new costume. The cure is the same — make the API transactional. Let
`emplace_back` return the index of the element it just created, and `alloc_node` collapses to
a single expression, the member `data_alloc_lock_` is deleted from the class, and one
serialization (and one contended cache line per insertion) simply ceases to exist. This is
the third time the transactional-interface principle has appeared inside this one data
structure: the queue's `pop` returning the value instead of splitting it from the removal;
our `insert` returning the ownership verdict instead of leaving callers to race a separate
`contains`; and now the allocator's append returning its receipt instead of leaving callers
to race a separate `size()`. Same principle, three proofs.

## Insertion

```cpp
bool insert(const T& key) {
    size_t new_node = EMPTY;                       // lazily allocated, reused across retries
    while (true) {
        size_t ts = table_size_.load(std::memory_order_acquire);
        size_t j = Hash{}(key) & (ts - 1);
        size_t head = buckets_[j].load(std::memory_order_acquire);
        if (head == UNINITIALIZED) { split_bucket(j); continue; }

        /* duplicate scan over the chain (the same loop as in contains) ... */
        if (exists) return false;                  // may orphan new_node — see below

        if (new_node == EMPTY) {
            new_node = alloc_node(key, head);
        } else {                                   // CAS-retry path: re-aim, do not re-allocate
            data_[new_node].next_bucket_node_idx.store(head, std::memory_order_relaxed);
        }

        if (buckets_[j].compare_exchange_strong(head, new_node,
                std::memory_order_release, std::memory_order_relaxed)) {
            /* post-publication resize handling — below */
            return true;
        }
        // The CAS failed: the head changed; loop and rescan with the same node.
    }
}
```

You will recognize the overall structure: it is the textbook lock-free insertion at the head
of a list, built on the CAS loop and the publishing protocol we studied earlier. Three
refinements are layered on top of it.

The first refinement is the reuse of the node across CAS failures. The variable `new_node` is
hoisted out of the retry loop. When the CAS fails, we do not allocate again; we *re-aim* the
node — a relaxed store of the newly observed head into its next word — and retry. The relaxed
order is sufficient because the node is still private to our thread; the eventual successful
release-CAS of the bucket head publishes the node and everything reachable through it, all at
once. Without this reuse, every lost CAS under contention would cost an allocator lock
acquisition and leak a node; with it, contention costs nothing but the retry itself.

The second refinement is that the duplicate check runs before the allocation, so the common
path that rejects a duplicate allocates nothing. The interaction of these two refinements
creates the one leak on this path: we allocate on the first iteration, lose the CAS, and on
the second iteration discover that a competing thread has just inserted the very same key. We
return `false` and permanently orphan our pre-allocated node. This single-node leak on
concurrent duplicate collisions is a documented and accepted trade-off; the alternative — a
free list — would tax every operation to recover a node in a vanishingly rare case.

The third refinement deals with the possibility that our insertion has raced a table
doubling:

```cpp
size_t check_ts = table_size_.load(std::memory_order_acquire);
if (check_ts > ts) {
    size_t correct_j = Hash{}(key) & (check_ts - 1);
    if (correct_j != j) {
        insert(key);        // re-insert under the new geometry; the result is discarded
        return true;
    }
}
```

To see the hazard, consider two threads. Thread A executes the CAS and successfully publishes
its node into bucket `j` under table size N. Just at this moment, thread B doubles the table
to 2N, and some thread splits the new child bucket `j + N` — and the split may have taken its
snapshot of `j`'s chain *before* A's CAS landed. If the key's new home is `j + N`, then A's
node is now invisible to every future lookup, because every future lookup goes to `j + N`.
Thread A detects the danger by re-reading `table_size_` after its CAS; if the table grew and
the key's bucket moved, A simply re-runs the entire insertion under the new size. Both
possible orderings of the race resolve cleanly:

```
  A: CAS node(k) into bucket j     |  B: resize N -> 2N; split of j+N copies chain(j)

  Case 1: the split's snapshot was taken AFTER A's CAS
     Child j+N receives a copy of k. A's re-insert finds it and stops.
     The live copy: the split's copy in j+N.  A's original: stale-in-parent (harmless).

  Case 2: the split's snapshot was taken BEFORE A's CAS
     Child j+N lacks k. A's re-insert scans j+N, finds no duplicate, CASes a fresh node in.
     The live copy: A's second node in j+N.  A's first node: stale-in-parent (harmless).
```

In either case, exactly one live copy of the key ends up in the correct bucket, and the
modular argument of the previous section guarantees that the leftover in bucket `j` can never
be copied forward. Note that the return value of the recursive call is deliberately discarded
and the outer call returns `true`. This is not an oversight. Reaching this path proves that
the key was absent and that our thread published it; our thread is the winner even when the
recursion reports a duplicate — because in Case 1, the duplicate it found is the split's copy
of *our own node*. Discarding the recursive result preserves the exactly-one-winner semantics
that callers rely on when they key initialization work off the return value of `insert`.

The resize trigger rides on the same post-CAS block, and it is our old friend, the
double-checked locking pattern, with `table_size_` itself serving as the flag:

```cpp
if (data_.size() > ts * 2) {
    std::lock_guard lock(resize_lock_);
    size_t current_ts = table_size_.load(std::memory_order_relaxed);
    if (current_ts == ts) {                        // DCLP: someone may have beaten us to it
        size_t new_ts = ts * 2;
        buckets_.resize(new_ts);                   // append new heads; existing ones are stable
        for (size_t i = ts; i < new_ts; ++i)
            buckets_[i].store(UNINITIALIZED, std::memory_order_relaxed);
        table_size_.store(new_ts, std::memory_order_release);
    }
}
```

The order of publication here is the load-bearing part, so let us walk through it. First,
`buckets_.resize` default-constructs the new atomic heads and publishes the deque's new size
internally with a release store. Second, the new heads are stamped `UNINITIALIZED`; a relaxed
store suffices because no reader can compute an index beyond the old table size until the
third step happens. Third, the release store of `table_size_` publishes the new geometry. Any
thread that acquire-loads the new `table_size_` is therefore guaranteed to see fully
constructed heads already stamped `UNINITIALIZED`. Readers that index the old buckets while
the resize is in progress are safe for the reasons we established when we studied the deque:
existing elements never move, and old directories are retired rather than freed. Observe also
how little the resize actually does — no rehashing, no copying, just new empty heads. All of
the real redistribution work is deferred to the lazy per-bucket splits. This is what makes the
resize "non-blocking" from the perspective of every thread except the one that wins the DCLP
race, and even that thread does only O(table size) trivial stores.

One more observation before we move on: look at what the trigger `data_.size() > ts * 2`
actually measures. It counts *allocations*, not live keys. Tombstoned nodes, split copies,
abandoned subchains, and orphaned duplicates all inflate it. We will return to the
consequences of this at the end.

## Deletion

```cpp
bool erase(const T& key) requires AllowDelete {
    /* find bucket j, split if UNINITIALIZED, scan — the same skeleton as contains */
    if (data_[actual_curr].value == key && !(check_curr & MARK_BIT)) {
        if (data_[actual_curr].next_bucket_node_idx.compare_exchange_strong(
                check_curr, check_curr | MARK_BIT,
                std::memory_order_release, std::memory_order_relaxed)) {
            size_t check_ts = table_size_.load(std::memory_order_acquire);
            if (check_ts > ts) {
                size_t correct_j = Hash{}(key) & (check_ts - 1);
                if (correct_j != j) {
                    erase(key);   // re-erase: chase the tombstone into the new bucket
                }
            }
            erased = true;
        }
        break;
    }
    /* on not-found: revalidate table_size_, retry if it changed */
}
```

Logical deletion is a single CAS on the victim's own next word, setting the `MARK_BIT`. It is
worth asking why we use a CAS here rather than the simpler `fetch_or`. The answer is
exclusivity: the expected value `check_curr` was loaded unmarked, so at most one thread can
perform the transition from live to dead and return `true`. Concurrent erasers of the same key
lose the CAS and, through the `break` and the not-found fallthrough, return `false` — exactly
the semantics a set should have. The CAS would also fail if the node's next link changed
between our load and our CAS; but notice that in this design, a published node's next word is
never rewritten by insertions (new nodes are prepended at the head, upstream of every existing
node), so the only competing writer on that word is another eraser.

Deletion has the same resize race as insertion, in mirror image, and it is instructive to
walk through it. Thread A marks the node for key `k` in bucket `j` under table size N. Just at
this moment, thread B doubles the table, and the split of child `j + N` takes its snapshot of
the node's next word *before* A's mark lands. The split sees a live node, copies the key —
unmarked — into `j + N`, and `j + N` is precisely the bucket that every future lookup of `k`
will consult. Thread A has returned `true`, and yet the key is alive: the deletion has been
silently undone. A symmetric problem calls for a symmetric fix, and it is the same fallback we
used in `insert`: after a successful mark, re-read `table_size_`, and if the key's bucket
moved, re-run the erase under the new geometry.

```
  A: mark node(k) in bucket j      |  B: resize N -> 2N; split of j+N copies chain(j)

  Case 1: the split's snapshot was taken BEFORE A's mark
     Child j+N received a live copy of k. A's re-erase finds it and marks it.
  Case 2: the split's snapshot was taken AFTER A's mark
     The tombstone filter dropped k; child j+N has no copy. A's re-erase
     finds nothing and returns false — which the caller correctly ignores.
```

The discarded return value of the recursive call is, once again, not sloppiness but the whole
point. The outer erase has already won the mark race and owns the `true`; the recursion's only
job is the visibility of the tombstone in the new bucket, and "nothing to mark" (Case 2) is a
success for that job. Note also a third possibility: the re-erase may find bucket `j + N`
still `UNINITIALIZED` and perform the split itself — but that split reads the parent chain
*after* A's mark, filters the node out, and collapses into Case 2.

The mark, once set, is honored in exactly three places, and it is worth enumerating them:
the duplicate scans of `contains` and `insert` skip marked nodes (both check the mark and
compare the key using the same loaded word), and `split_bucket` refuses to copy them. Physical
memory is never reclaimed. A deleted key's node persists in the arena and in its chain,
disappearing only from the chains of future *children* at split time. If the same key is
inserted again after an erase, a brand-new node is allocated and prepended; the chain may then
contain both a dead and a live node for the key, with the live one closer to the head. The
correctness does not depend on their relative positions, of course, since the scan tests
liveness on every node individually.

## Memory management, or the lack of it

By now you have noticed that every allocation in this data structure is permanent: "freeing"
memory means "becoming unreachable garbage inside the arena." Let us take a complete
inventory of the dead weight:

| Source | Mechanism | Bound |
|---|---|---|
| Tombstoned nodes | `erase` marks; nothing reclaims | one per successful erase |
| Stale parent copies | the split copies but never trims parent chains | one per key per doubling it survives |
| Abandoned subchains | a lost `split_bucket` CAS discards a whole speculative copy | O(bucket length) per lost split race |
| Orphaned insert nodes | a pre-allocated node meets a duplicate on retry | one node per concurrent-duplicate collision |
| Re-hash leftovers | the insertion fallback strands the first node in the old bucket | one node per insert/resize race |

Now consider what we bought with this memory. No use-after-free is possible under any
interleaving of any operations. No A-B-A problem is possible on the chain links: indices are
never recycled, so a bucket head can never revert to a previously seen value with a different
meaning, and the CAS loop on the head needs no version counter, no tagged pointers, and no
hazard pointers. And the readers touch no shared metadata whatsoever — no epoch counters to
increment, no hazard slots to publish. In the previous section of this book, we listed the
ways to defer deallocation in order to defeat the A-B-A problem; this data structure takes the
simplest of those options — *keep everything until the data structure dies* — to its logical
extreme. Whether this is a good trade depends entirely on the application. For a build-heavy,
delete-light workload with a bounded lifetime — the shape of workload one finds in a compiler
or an EDA tool — arena growth is almost always cheaper than the per-read tax of any
reclamation scheme. For a long-lived table with heavy churn, it is not, and you should reread
the recommendation from earlier in this chapter: a simple structure under a mutex may serve
you better.

## Memory ordering

The synchronization discipline is uniformly acquire/release. Every cross-thread publication —
the bucket-head CAS, the `table_size_` store, the deque's size store, the tombstone CAS — is a
release; every consumption is an acquire. Writes to data that is still private to a thread
(re-aiming a pre-allocated node, stamping `UNINITIALIZED` before the geometry is published)
are relaxed. There is no `seq_cst` anywhere in the code, and its absence is a deliberate
decision with a stated cost, which we must now examine honestly.

Both post-CAS resize detections — the re-insert fallback and the re-erase fallback — rely on
this pattern:

```
  Thread A:  CAS bucket_j (release)  ;  load table_size_ (acquire)
  Thread B:  store table_size_ (release)  ;  split reads bucket_j (acquire)
```

Thread A misses the resize only if its load of `table_size_` sees the old value. But if that
happens, then B's split-read of the bucket and A's CAS have no ordering between them either,
and it is possible that B's snapshot *also* misses A's write. In that case, the update is
dropped — a lost node for `insert`, a resurrected key for `erase` — and neither side detects
anything. This is the classic store-buffer gap: under acquire/release, two independent release
chains have no total order. On x86, with its Total Store Order, the `LOCK CMPXCHG` instruction
is a full barrier; the store buffer drains before A's subsequent load, and the window closes
in hardware, for free. On ARM, closing it would require upgrading the operations to `seq_cst`
and paying for a `DMB ISH` barrier on the hot path of every insertion. The code explicitly
chooses not to pay, which means the window is a real, if narrow, hole in the C++ memory model
on weakly ordered hardware.

Two caveats belong on the record here. First, "it works on current Neoverse silicon" is an
empirical statement about today's processors, not a guarantee; the C++ memory model permits
the reordering, and a future core or a smarter compiler is within its rights to expose it.
Second, a clean ThreadSanitizer run should not be read as validation of the ordering. TSAN
detects races in the interleavings it actually observes, and it models the C++ abstract
machine rather than the reordering envelope of any particular CPU; it is constitutionally
incapable of certifying the *absence* of an ordering bug of this kind. The honest statement —
and the one you should write in your own code reviews — is: the window is accepted, believed
narrow, and has not been observed.

## Measuring the hash set

The hash set is intended for two usage patterns, and each gets its own benchmark. In the
first, insert-dominated pattern, the caller expects most keys to be new but needs to be told
when they are not ("build the set, and tell me about duplicates"). In the second,
lookup-dominated pattern, the table is mostly read, with an occasional insertion trickling in
("mostly queries, occasional growth"). We will describe the workloads in words; the harness
itself is ordinary Google Benchmark machinery.

The insert benchmark gives each thread its own disjoint range of sequential keys, so the
duplicate rate is exactly zero and every operation is a real insertion; the iteration count is
fixed for every thread count, so that every row of the scaling table performs the identical
workload against the identical table trajectory — the table climbs through many doublings live,
in the middle of the measurement. (An earlier version of this benchmark drew keys from a shared
random pool; the duplicate rate then depended on the table fill, which depended on the
iteration budget, which the benchmark library chose differently for every thread count — some
rows were mostly measuring cheap duplicate rejects, which are reads, under an insertion
benchmark's name. Pin the composition, or you do not know what you measured.) The workload also
carries a free correctness check: since every key is globally unique and attempted exactly
once, every `insert` must return `true`, and each thread verifies that it did. This turns the
benchmark into a standing regression test for the insertion return-value semantics under
concurrent resize.

The lookup benchmark pre-populates the table with a million keys, untimed, and then runs 99%
`contains` and 1% insertion on a deterministic schedule. The lookup keys are drawn from a range
exactly twice the pre-populated one, so half of all searches miss — and misses matter, because
a miss is the only operation that exercises the negative-result revalidation path; an all-hits
workload would never touch it. The inserted keys come from thread-disjoint ranges above the
lookup range, so the hit rate stays fixed for the entire run, and the insertion budget is kept
a factor of three below the resize trigger, so the measurement is a true steady state. What a
reader pays while a resize is actually in flight is a tail-latency question, and a tail-latency
question requires a tail-latency benchmark; it has no business hiding inside a mean.

The baseline is the textbook answer to concurrent hashing: `std::unordered_set` behind a
reader-writer lock (`std::shared_mutex`), shared for lookups, exclusive for insertions. It is
worth spelling out why some such lock is *forced* on any growable locked container, because
this is the fundamental design axis the benchmark explores. The rehash must not run while a
reader is in flight — so the reader must somehow be visible to the writer. A plain mutex makes
readers visible by serializing them against each other. A reader-writer lock makes them
visible with a reader count — but now every reader performs an atomic read-modify-write on a
shared cache line, and under concurrency that line ping-pongs between every core that reads.
The industrial remedy is to distribute the count across cores, but a sharded counter is itself
a nontrivial concurrent data structure with its own trade-offs; the plain `shared_mutex` is
the honest pedagogical baseline. Our hash set sits at the other end of the axis: readers
register nowhere and are visible to no one; instead they *validate* — the trailing re-check of
the table size asks "did the geometry change under me?" and retries on the rare yes. Register
versus validate is the entire architectural choice, and the benchmark puts a price on it.

### A cautionary tale: the debt in the setup

The first run of the lookup benchmark produced numbers that were slow, grew worse with thread
count, and — most suspiciously — looked broadly similar for our hash set and for the locked
baseline. The wall-clock time per operation climbed while the CPU time stayed low and flat:
the classic signature of threads waiting rather than working. But readers in this hash set do
not wait on anything. What was there to wait for?

The answer was in the setup, not in the measurement. The table was pre-populated starting from
a small initial capacity, so the prefill drove it through ten doublings — and a doubling, as
we know, does no redistribution work itself; it merely stamps the new buckets `UNINITIALIZED`
and lets the first toucher of each bucket perform the split. The prefill therefore returned
with the container in a semantically complete but physically unfinished state: a million keys
all present and queryable, and half a million splits *pending*. The timed region then began,
and the random lookups touched the unsplit buckets one by one, each performing a split, and
each split allocating nodes through the one global allocator lock. The "wait-free" readers had
inherited a spinlock convoy. Every observable property of the table had looked correct at the
end of the setup — the pending work is encoded only in bucket heads that no public interface
exposes — and the numbers that came out were plausible-looking and wrong.

The lesson generalizes well beyond this table: *a lazily-evaluated data structure makes
"setup" a lie unless the deferred work is forced to quiesce before the timing starts*. Lazy
structures are designed so that deferred work is semantically transparent, which makes it
exactly as invisible to a benchmark harness as to any other caller — and the timer does not
distinguish debt service from new work. The same trap wears many costumes: the file that is
"loaded" but not yet in the page cache; the memory that is reserved but not yet
first-touched, so the timed region measures the kernel's soft page faults; the JIT-compiled
function that is warm in name only; the amortized container left one operation short of its
expensive rebalance. The fix here took one line — construct the table at full capacity, so
that no doubling ever occurs and no split ever exists — and the single-threaded lookup
promptly dropped from 79 ns to 19 ns. The other fix, pre-warming (sweep every bucket once,
untimed, to force the pending splits), is the right choice when you *want* to measure a table
that has lived through its doublings, stale ancestor chains and all; the two setups measure
genuinely different tables, and both are legitimate — as long as you know which one you asked
for.

### Results

The numbers below come from two machines: a 16-core/32-thread AMD Ryzen 9950X desktop, and a
dual-socket Intel Granite Rapids server with 128 physical cores and 256 SMT threads.

For the lookup-dominated workload, the single-threaded numbers already tell half the story. On
the Ryzen, one lookup in the million-key table costs our hash set 19 ns — the hash, the bucket
head, and a short chain walk, dominated by two dependent cache misses. The locked baseline
pays 41 ns for the same search: the uncontended `shared_mutex` round trip roughly doubles the
cost of the work it protects. Under concurrency the gap becomes qualitative. The locked set
collapses to about 6 million operations per second at 2–4 threads — the reader-count cache
line ping-pongs, then the readers start sleeping in the kernel — and although its throughput
recovers roughly linearly at higher thread counts, it does so from a devastated baseline: the
CPU cost per operation sits flat near 750 ns from 4 threads onward, which is to say the lock
now costs forty times the lookup it guards. Our hash set scales to 487 million lookups per
second on 32 Ryzen threads — 11× the locked set — and on Granite Rapids it crosses one billion
operations per second: 1.16 G/s on 128 physical cores, holding 1.08 G/s with all 256 SMT
threads engaged. This is a real table — a million keys, half the searches missing, live
insertions trickling in — not an L1-resident toy, and the per-operation CPU time stays within
a small factor of the single-threaded 19 ns all the way up. Readers that register nowhere have
nothing to contend on.

For the insert-dominated workload, the comparison with the baseline is almost unfair — the
locked set serializes every insertion and decays from 34 M/s at one thread to under 1 M/s at
256 threads, while our hash set peaks at 187 M/s on the Ryzen and 322 M/s on Granite Rapids —
but the more interesting curve is our own. Insertion throughput peaks at 8–16 threads and then
*declines*: to 54 M/s at 32 Ryzen threads, to 32 M/s at 256 Granite Rapids threads. Every
insertion crosses the one global allocator spinlock, and past the peak the benchmark is no
longer measuring the hash table at all — it is measuring the scaling behavior of a contended
spinlock, a subject we treated at length earlier, here wearing a hash table's costume. The
wall-clock versus CPU divergence past the peak (22 µs versus 0.9 µs per operation at 64
threads) shows the threads parked in the lock's backoff rather than computing. This is the
structure's honest bottleneck, and the benchmark hands us the remedy along with the diagnosis:
the lock is crossed once per node, so batching the allocation — reserving a block of slots per
lock acquisition and handing them out thread-locally — divides the crossing rate by the batch
size without changing the architecture.

One final observation costs nothing to collect and is more honest than it has any right to be:
the fans. When the concurrent hash set runs, the cooling fans of the desktop spin up; when the
locked baseline runs — for far longer — the machine stays quiet. The CPU-time column explains
the acoustics: under the reader-writer lock at 32 threads, each operation consumes 0.75 µs of
CPU against 17 µs of wall clock, which means the threads spend more than 95% of their time
asleep in `futex_wait`, drawing no power. The lock converts a 16-core machine into a warm
single-core one, and the run takes longer *because* the silicon is idle. If your concurrency
benchmark is quiet, be suspicious.

## Accepted limitations

Beyond the leak inventory and the ARM ordering window, three properties of the design deserve
to be stated explicitly, because they surface as workload-dependent effects rather than as
bugs.

The first is that the load-factor accounting counts the dead. The resize trigger
`data_.size() > ts * 2` measures total allocations, and under erase-heavy or
contention-heavy workloads, the tombstones, stale copies, orphans, and abandoned subchains all
push the table toward doubling even when the number of live keys is low. Each doubling's
splits then allocate *more* copies, compounding the effect. This is partly a feature —
doubling is the garbage collection mechanism, so garbage pressure buys garbage collection —
but you must understand that "load factor 2" is a statement about arena consumption, not about
chain length. The worst-case chain length is bounded only for insert-dominated workloads.

The second is that the ancestor chains grow monotonically. Because splits copy rather than
trim, the low-numbered buckets accumulate every key — live, moved, or dead — that ever hashed
through them, and every future split of one of their children rescans that entire chain. The
cost of splitting the children of old, well-used buckets therefore grows with the *history* of
the table, not with its live occupancy. Steady-state lookups are unaffected, since they
consult only the current bucket; but the latency of a resize has a tail that lengthens over
the lifetime of the set.

The third we did not deduce; we measured it, by accident, in the cautionary tale above. A
doubling defers all of its redistribution work to the first toucher of each new bucket, and
each of those cooperative splits allocates through the single global allocator lock. A
doubling therefore issues half a table's worth of deferred work, and if the phase that follows
is read-heavy, it is the *readers* who collect on that debt, funneling through the allocator
spinlock in a convoy. The wait-freedom of the read path is a steady-state property; in the
aftermath of a doubling, a reader's worst case is one split plus one contended lock
acquisition, and a burst of doublings followed by a read-only phase temporarily converts
wait-free readers into a queue. The same measurement points at the same remedy as the
insertion ceiling: batch the allocator, so that a split — and an insert — crosses the lock
once per block of nodes rather than once per node. Note how naturally this composes with the
transactional `emplace_back` we proposed when we first met the allocator: an append that
returns its index generalizes to an `emplace_back_n` that returns its *first* index — batch
reservation in a single crossing — so the two improvements are one idea at two granularities.
Alternatively, an application that can
anticipate its growth can pre-warm after a bulk load, sweeping the buckets once to pay the
debt at a time of its choosing.

## Summary

The most important lesson of this data structure is one we have met before in this book, now
in its most radical form: *the fastest way to solve a hard concurrency problem is to arrange
for the problem not to exist*. The hash set makes three refusals. It refuses to reclaim memory
— the append-only arena eliminates hazard pointers, reference counting, and the A-B-A problem
in one stroke. It refuses to relink shared state — copy-on-split means a reader can never
observe a chain in the middle of surgery. And it refuses to move anything — the index-stable
deque means no pointer is ever invalidated. Each refusal converts a hard synchronization
problem into a memory bill, and the design is an explicit bet that, for its intended workload,
the bill is cheaper than the tax.

What remains to be synchronized, after the three refusals, is remarkably small: one CAS per
insertion (the bucket head), one CAS per deletion (the mark bit), one CAS per bucket split
(the installation of the copy), a spinlocked bump allocator, a DCLP-guarded geometry publish,
and a pair of symmetric post-CAS geometry rechecks that chase an update into the new bucket
when a resize races it. And the one invariant that holds the whole scheme together — that the
garbage the design tolerates can never leak back into the live data — is enforced not by any
runtime mechanism but by modular arithmetic. It is worth pausing on that: the cheapest
synchronization primitive available to you is a mathematical proof.

The measurements teach their own lessons, and two are worth carrying forward. On the
benchmarking side: with a lazy data structure, the state your setup leaves behind is part of
your measurement, whether you meant it or not — quiesce the deferred work, or account for it,
but never assume that "the setup is untimed" means the setup is free. And on the design side:
the benchmark located the structure's true bottleneck with complete precision — not the CAS
loops, not the memory ordering, but the one small spinlock everyone was polite enough to cross
one node at a time. When a lock-free data structure stops scaling, look for the lock.
