# The Lock-Free List

## Overview

The previous section ended with a promise. The hash set achieved its performance by three
refusals, the first and greatest of which was the refusal to reclaim memory: nodes were
immortal, and every hard question about concurrent deletion simply never came up. That
strategy has a domain of applicability — build-heavy workloads with bounded lifetimes — and
outside that domain, the memory bill comes due. This section pays it. We are going to build a
data structure that deletes for real: nodes are physically unlinked while other threads
traverse them, and their memory is returned to the allocator at the earliest safe moment. The
entire difficulty of the section is in the last four words — determining the earliest safe
moment is the memory reclamation problem, and we are about to see, in code and in
benchmarks, exactly what full reclamation looks like and what it costs.

The vehicle is a singly-linked list, and the choice is not arbitrary. A list — or any nodal
structure: a tree, a graph, a skip list — supports insertion and removal at arbitrary
positions, and traversals that visit many nodes. Locks serve such structures poorly. One lock
over the whole list serializes everything, including a long search against an unrelated
insertion at the other end. One lock per node invites the bane of all fine-grained locking:
a thread holding node 1 and wanting node 2 meets a thread holding node 2 and wanting node 1,
and both wait forever; restrict every thread to one lock at a time and the deadlocks become
livelocks. If the program truly spends its time inside a shared nodal structure — and only
then; as always, first ask whether partitioning or per-thread copies can make the problem
disappear — the implementation has to be lock-free.

The basic idea is the one we have used throughout this chapter: manipulate the links with
compare-and-swap. Insertion is our publishing protocol yet again. To insert at the head
(insertion after any node works the same way), read the current head pointer, build the new
node privately with its next pointer aimed at the current first node, and publish it with a
single CAS on the head; if the CAS fails, some other thread published first — re-aim and
retry. Removal at the head is the mirror image: read the head, read the first node's next
pointer, and CAS the head from the former to the latter; the node is now unreachable, and the
thread that removed it still holds the original pointer and can delete it.

Each operation is simple and reliable on its own. The trouble arises when we combine them.

## The A-B-A problem

Let two threads operate on the list at once. Thread A is removing the first node: it has read
the head pointer (call the node it points to T1) and T1's next pointer (T2), and it is about
to execute the CAS that swings the head from T1 to T2 — but it has not executed it yet.
Just at this moment, thread B gets busy:

```
  Initial:        head -> T1 -> T2 -> T3
                          ^     ^
        thread A holds: head=T1, head'=T2   (CAS pending)

  B removes T1:   head -> T2 -> T3          T1 freed
  B removes T2:   head -> T3                T2 freed
  B inserts T4:   head -> T4 -> T3          new T4 allocated AT T1's OLD ADDRESS

  A resumes, executes CAS(head: T1 -> T2):
      head still holds the address of T1  (it is T4 now, but who can tell?)
      the CAS SUCCEEDS

  Result:         head -> T2(freed!) ...    T4 unreachable; the list is corrupt
```

The allocator is not merely permitted to reuse T1's address for T4 — it prefers to: most
allocators return the most recently freed memory first, on the sound theory that it is still
hot in the cache. Thread A's compare-and-swap compares an address, the address matches, and
the CAS succeeds — installing a pointer to freed memory and losing T4 forever.

This failure mechanism is so common in lock-free programming that it has a name: the
**A-B-A problem**. A and B are values of a memory location: some pointer changed from A to B
and then back to A, and a thread that observed only the endpoints saw no change at all. The
compare in compare-and-swap is an equality test on a word; it cannot see history. The
programmer wrote the CAS assuming "if the value is unchanged, the structure is unchanged" —
and that assumption is simply false: the structure may have changed almost arbitrarily, so
long as the observed word was restored.

The root of the problem is that once memory is deallocated and reallocated, an address no
longer uniquely identifies data. Every solution therefore accomplishes the same thing by
different means: *from the moment a pointer is read until the CAS that uses it completes, the
memory it addresses must not be returned to the allocator*. If the memory is not freed, no
new allocation can land at that address, and the A-B-A problem cannot occur. Note carefully
that *not deallocating memory* is not the same as *not deleting nodes*: a node can be made
unreachable, its value can even be destroyed — but its memory must survive while anyone may
still be holding its address.

The previous chapter and section gave us a taxonomy of ways to defer deallocation:
keep everything forever (the hash set's answer); collect garbage at quiescent points, with
RCU as the industrial-strength variant; publish hazard pointers so reclaimers know what to
avoid; or count references, so that memory frees itself at the precise moment the last
reference drops. This section takes the last road, in its most demanding form: **atomic
shared pointers**. Every link in the list — the head, every node's next pointer, and the
pointer inside every iterator — is a reference-counted smart pointer, and the ones that live
in shared locations are *atomic* reference-counted smart pointers. The consequences are
immediate and pleasant:

- A node is destroyed at exactly the right moment: when the last link or iterator releases
  it. Not at the end of the program, not at the next quiescent period — immediately, and
  never early.
- The A-B-A problem dies structurally. In the interleaving above, thread A *holds a strong
  reference* to T1 — its copy of the head pointer is a reference. T1 therefore cannot be
  freed while A's CAS is pending, T4 cannot be allocated at T1's address, the CAS honestly
  fails, and A retries against reality. The witness protects the evidence. No version
  counters, no tagged pointers.
- Iterators never dangle. An iterator parked on a node keeps that node alive after it is
  deleted from the list, and — because deletion never clears the deleted node's next pointer
  — the iterator can keep walking, through the "graveyard" of removed nodes, back into the
  live part of the list. Traversal proceeds concurrently with any number of insertions and
  deletions, with no locking whatsoever.

One habit of mind must be surrendered at this door: there is no such thing as *the current
contents* of this list. The only way to learn the contents is to traverse, and by the time
the iterator reaches the end, the beginning has changed. A traversal observes some
interleaving-dependent mixture of past and present — each node it visits was in the list at
the moment it was visited, and no stronger statement can be made. This takes getting used
to, and it is not a defect of the implementation; it is what "concurrent" means.

All of this rests on one load-bearing component: the atomic shared pointer. The standard
gives us `std::atomic<std::shared_ptr<T>>`; we will also build our own intrusive one; and we
will borrow a genuinely lock-free one from Daniel Anderson. The list itself, as you are
about to see, is short — some three hundred lines with the comments, half of which is the
deletion algorithm — and the honest summary of this section is that we are not really
writing a lock-free list at all: we are writing an atomic pointer three times, and one list
on top of them all.

## The interface

The public face of the list, with the implementations elided:

```cpp
template <typename T, template <typename> class AtomicPtr>
class LockFreeList {
public:
    struct Node {
        T value;
        AtomicPtr<Node> next;
        /* intrusive refcount hooks: AddRef(), DelRef(), use_count() */
        Node(T val);
        ~Node();          // iterative, not recursive -- see Destruction
    };

    using SharedNodePtr = typename AtomicPtr<Node>::shared_ptr_type;

    class iterator {
    public:
        SharedNodePtr curr_;               // a STRONG reference to the node
        T& operator*() const;
        T* operator->() const;
        iterator& operator++();
        bool operator==(const iterator&) const;
        bool operator!=(const iterator&) const;
    };

    LockFreeList(SharedNodePtr dummy_head);

    iterator before_begin() const;
    iterator begin() const;
    iterator end() const;

    bool insert_after(iterator anchor, SharedNodePtr new_node);
    bool erase_after(iterator anchor);
};
```

The second template parameter is the whole story: `AtomicPtr` is the atomic shared pointer
policy, and the list demands of it a small concept — a non-atomic value type
(`shared_ptr_type`) that owns a strong reference; atomic `load`, `store`, and
`compare_exchange_strong` over that value type; and, optionally, the Harris marking API
(`is_marked`, `set_mark`, `get_unmarked`) advertised by a `supports_marking` constant. The
list branches on that constant with `if constexpr` and compiles a marking implementation or
a simpler (and weaker — we will see exactly how) one. Of our three policies, the two custom
pointers support marking; the standard pointer does not, for reasons we will see when we
meet it, and the weaker mode exists for its sake. Everything this section measures at
the end is a consequence of which policy fills this slot.

Notice that the list never allocates a node — not even its own head. The constructor takes a
caller-supplied dummy head node, and `insert_after` takes a ready-made `SharedNodePtr`. This
is forced by the policy design: each pointer type creates its pointees differently
(`std::make_shared`, plain `new` onto an intrusive count, `parlay::make_shared`), and the
list has no business knowing which. The dummy head, by contract, is never erased — which is
what makes `before_begin()` a permanently valid anchor and spares every operation a special
case for the empty list.

Now the contract. `insert_after(anchor, node)` links the node immediately after the anchor
and returns `true`; it returns `false` only if the anchor has been logically deleted —
linking onto a corpse would strand the new node in a detached chain that is about to be
unlinked, so the operation refuses, and the caller decides where else to put it.
`erase_after(anchor)` removes the live node immediately following the anchor; it returns
`true` if and only if *this call* deleted a node, with the deletion decided by a single CAS
that exactly one competitor can win — the exactly-one-winner discipline we have maintained
throughout this chapter. It returns `false` when there is nothing to erase: the anchor is
null or itself deleted, or has no live successor. Under contention it does not give up
early: a call that loses the deletion race *helps* finish the loser's cleanup and retries on
the next node, so the idiom `while (erase_after(head)) {}` drains the list no matter how
many threads fight over it.

The progress guarantee is lock-free, not wait-free: an individual operation may retry
indefinitely, but only because some other operation's CAS succeeded, so the system as a
whole always advances. Whether that guarantee survives the pointer policy underneath is a
question we will take up when we meet the policies — it is less clear-cut than it sounds.

Finally, the iterator contract, which is where this list is unlike anything in the standard
library: *iterators are never invalidated*. Not by insertion, not by erasure, not by erasure
of the very node the iterator points to. The iterator holds a strong reference; its node
stays alive for as long as the iterator does, and advancing from a deleted node walks
through the graveyard back into the live list. The price appears in the type: `operator++`
performs an atomic load and reference-count traffic, so even read-only traversal is not the
few-cycle pointer chase you may be accustomed to. How large that price is, is precisely what
the benchmarks at the end will show.

## The structure

The private part of the list is one member:

```cpp
private:
    AtomicPtr<Node> head_;   // the dummy head: set once, never marked, never erased
```

Everything else lives in the nodes, and the node deserves a close look:

```cpp
struct Node {
    T value;
    AtomicPtr<Node> next;

    std::atomic<long> ref_count{0};
    void AddRef() { ref_count.fetch_add(1, std::memory_order_relaxed); }
    bool DelRef() { return ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1; }
    long use_count() const { return ref_count.load(std::memory_order_relaxed); }
    ...
};
```

The value and the atomic next pointer you expected. The reference-counting hooks are the
intrusive-pointer contract: when the policy is our intrusive pointer, the count lives here,
inside the node, and `AddRef`/`DelRef` are how the pointer manipulates it — `DelRef` returns
`true` on the transition to zero, and its acquire-release order is what makes every prior
write to the node visible to whichever thread ends up destroying it. When the policy is one
of the other two pointers, these hooks are eight bytes of dead weight per node, accepted
with open eyes so that `Node` does not have to know which pointer it is instantiated with.
(Yes, this contradicts "unnecessary generality is your enemy" — by exactly eight bytes. The
alternative, a node type parameterized on the policy's needs, buys those bytes back at a
real cost in complexity; we measured the trade and took it.)

The memory-ordering pattern in the hooks is worth committing to memory, because it is *the*
canonical reference-counting discipline: increments are relaxed — taking an extra reference
to an object someone already keeps alive synchronizes nothing — while the decrement is
acquire-release, because the thread that drops the count to zero is about to run the
destructor and must observe every write that every other releasing thread made to the
object. Relax the decrement and you have a use-after-free that strikes once a month.

One convention completes the structure, and it should look familiar. A node's logical
deletion is recorded by setting a mark bit *on that node's own next pointer* — the mark
travels with the victim, exactly as the hash set's tombstone did. The consequence bears
repeating in this new context, because every loop in the implementation leans on it: a
marked value loaded *from* `X->next` means *X itself* is deleted; whether X's successor is
deleted can only be discovered by loading the successor's own next pointer. Where the hash
set stored the mark in bit 63 of an index, the pointers here steal bit 0 of an aligned
address — different bit, same idea:

```
  A node's next pointer (one 64-bit word):

  +----------------------------------------------------------+---+
  |              address of the successor node               | M |
  +----------------------------------------------------------+---+
    bits 63..1  (objects are >= 4-byte aligned)               bit 0
                                                    M = this node is
                                                        logically deleted
```

The mark is part of a pointer's *identity*: a marked and an unmarked pointer to the same
node compare unequal. This is not pedantry — the compare-and-swap loops depend on it (a CAS
whose expected value is unmarked fails the instant someone marks the word, which is how
insertion detects a dying anchor), and the iterator depends on its inverse, as we will see.

## The atomic shared pointer

Everything now reduces to one question: what, exactly, is an *atomic* shared pointer? A
plain `shared_ptr` copy is two operations — read the pointer, increment the count it leads
to — and therein lies one of the nastiest chicken-and-egg problems in concurrent
programming. To take a reference, you must increment a counter that you can only reach
through a pointer you have not yet secured. In the gap between your load and your increment,
a concurrent `store` may drop the last reference and destroy the object — and your increment
lands in freed memory. The entire design space of atomic shared pointers is the design space
of ways to close this gap. We will use three, and they close it three different ways: with
the standard's blessing and a hidden lock; with our own explicit one-bit lock; and with
hazard pointers, locklessly.

### The standard pointer: std::atomic<std::shared_ptr>

The standard, since C++20, lets you write `std::atomic<std::shared_ptr<T>>` and gives it the
usual atomic interface. What the standard does not give it is lock-freedom:
`is_lock_free()` is permitted to return `false`, and in the major implementations it does.
The library we benchmark (libstdc++) closes the load-increment gap the honest way — a
one-bit spinlock embedded in the atomic word, held across the pointer read and the count
adjustment. Keep that fact in your pocket for the benchmark section: the contest between the
standard pointer and our intrusive one is not "locked versus lock-free." It is two embedded
spinlocks, distinguished by the size of their critical sections, the number of cache lines
they touch, and — decisively, as it turns out — their manners under contention.

The standard pointer's structural cost is inherited from `shared_ptr` itself: the count
lives in a separately-allocated control block, so every node is two allocations (unless
`make_shared` fuses them) and every reference operation is traffic on a cache line that is
not the node's own.

Where does the library keep its spinlock? In the low bit of the atomic word — the very
trick, the very bit, that our pointers are about to spend on the Harris mark. This is worth
pausing on for two reasons. The first: since both the standard pointer and our intrusive
one own an embedded one-bit lock, the trick itself is evidently not the advantage — as the
benchmarks will show, stealing the bit did not buy the library what you would expect, and
whatever separates the two contestants' numbers must lie in what happens *while* the bit is
held and in how the losers wait. The second consequence is subtractive: the word's low bit
is spoken for, and the word itself belongs to the library, not to us — the standard pointer
offers our adapter no honest place to put a mark. (A tag could be smuggled through the
`shared_ptr`'s stored pointer via the aliasing constructor, at the price of stripping it in
every accessor of a type never designed to carry one; we declined.) So `supports_marking`
is `false` for the standard pointer alone, the list compiles the fallback insertion and
deletion for it, and the correctness limitation we will meet in the deletion section is,
concretely, *this* pointer's limitation.

### The intrusive pointer

Our own entry attacks the structural cost first: the count lives *in the object* — those are
the `AddRef`/`DelRef` hooks in `Node` — so there is no control block, no second allocation,
and the count shares a cache line with the data it protects. The atomic word packs three
things:

```
  intr_shared_ptr's atomic word:

  +------------------------------------------------------+---+---+
  |                  the U* pointer                      | L | M |
  +------------------------------------------------------+---+---+
    bits 63..2                                            bit1 bit0
                              L = one-bit spinlock  (implementation detail)
                              M = Harris mark       (part of the value)
```

Bit 0 is the mark — part of the pointer's identity, compared by CAS, visible to the list.
Bit 1 is the lock — pure implementation, invisible in every value the pointer hands out. The
gap is closed by brute honesty:

```cpp
shared_ptr_type load(...) const {
    uintptr_t val = lock<Intent::Read>();   // spin on bit 1
    shared_ptr_type res;
    res.p_ = reinterpret_cast<U*>(val & ~2ULL);
    if (res.get_unmarked_ptr()) res.get_unmarked_ptr()->AddRef();
    unlock(val);                            // republish the pre-lock value
    return res;
}
```

Hold the lock across load-plus-`AddRef`, and no concurrent `store` or CAS can drive the
count to zero in between; `store` and `compare_exchange_strong` take the same lock for their
own read-modify-release sequences, and the release of the lock *is* the publishing store,
which is why the implementation quietly refuses to honor a relaxed memory order if a caller
asks for one — a relaxed unlock would publish the pointer without publishing the pointee.

So we have built a spinlock into a pointer, in a section about lock-free lists, and the
reader is entitled to raise an eyebrow. Two things justify it. The first is that the
*algorithm* above the pointer remains lock-free: the list's CAS loops, its helping, its
linearization points are untouched; what the embedded lock forfeits is the formal
progress guarantee at the very bottom of the stack — a thread descheduled while holding some
pointer's lock bit stalls that pointer's other users. Formally, then, the composed structure
is not lock-free, and we will not pretend otherwise; whether the formality matters is a
question the benchmarks answer better than the definitions do. The second justification is
where this chapter's spinlock lessons come home to roost — the lock is engineered for its
one-microsecond job, and its backoff policy is asymmetric with intent:

```cpp
if constexpr (intent == Intent::Read) {
    // readers: pause-loop, then yield, then a 1 us nanosleep
} else {
    // writers: sleep IMMEDIATELY -- a spinning writer steals the very
    // cache line the lock holder needs in order to finish
}
```

Readers stay polite and stay out of the kernel as long as they can. Writers go to sleep at
the first sign of contention, because we know — we measured it, several sections ago — that
a hot CAS loop hammering a contended line does not merely wait for the holder, it *slows the
holder down*, thrashing the one cache line whose release everyone is waiting for. Hold that
thought until the benchmarks: the difference between a spinlock with manners and a spinlock
without turns out to be two orders of magnitude.

### The lock-free pointer: hazard pointers under the hood

The third contestant is Daniel Anderson's `parlay::atomic_shared_ptr`, and it is the real
thing: a genuinely lock-free load. It closes the gap with hazard pointers — but notice the
role reversal, because it is the most instructive thing about this design. Earlier we
described hazard pointers as a memory-reclamation scheme for data structures: publish what
you are reading so reclaimers steer around it. Here the entire scheme has been demoted to an
implementation detail *inside the pointer*: the hazard pointer protects not the list node
but the `shared_ptr` *control block*, for just long enough to increment its count safely:

```cpp
shared_ptr_type load(...) const {
    control_block_type* cb = nullptr;
    auto& hazptr = get_hazard_list<control_block_type>();
    while (true) {
        cb = hazptr.protect(control_block, ...);     // publish, re-validate
        if (cb == nullptr) break;
        if (cb->increment_strong_count_if_nonzero()) break;  // resurrect-if-alive
    }
    return shared_ptr_type(make_shared_from_ctrl_block(cb));
}
```

`protect` is the hazard-pointer handshake: store the candidate into your hazard slot, then
re-read the source and confirm it has not changed — a store followed by a load of a
different location, which is to say a StoreLoad ordering requirement, which is to say a full
barrier on every architecture that matters, *once per pointer load*. Then
`increment_strong_count_if_nonzero` handles the last subtlety: the control block, protected
from deallocation, may still describe an object whose count already hit zero, and a plain
increment would resurrect the dead; the conditional increment retries the loop instead.

Two details reward close reading. The mark bit is stolen from the low bit of the *control
block* pointer — the only word this class owns — and scrubbed inside every accessor so that
the reference-counting machinery never sees a misaligned address. And the weak/strong CAS
pair carries a confession in a comment: on CAS failure, `expected` is refreshed with a fresh
`load()`, and *it is possible that expected ABAs and stays the same on failure* — so
`compare_exchange_strong` cannot simply forward to one `weak` attempt, and is instead built
as a loop that retries until the observed word genuinely differs. Savor that: an A-B-A
footnote inside the very tool we adopted to abolish the A-B-A problem. The pointer kills
ABA for its *users* — a held reference pins the address, which is all the list needs — while
its own innards, operating below the reference count, must still defend against it. The
problem is never solved; it is only ever pushed down a level, and the bottom level always
pays.

## Insertion

With the pointer policy doing the heavy lifting, the list operations are short. Insertion,
in the marking mode:

```cpp
bool insert_after(iterator anchor, SharedNodePtr new_node) {
    if (!anchor.curr_) return false;
    while (true) {
        auto expected_next = anchor.curr_->next.load(std::memory_order_acquire);
        if (expected_next.is_marked()) {
            return false;                    // the anchor itself is deleted
        }
        new_node->next.store(expected_next, std::memory_order_relaxed);
        if (anchor.curr_->next.compare_exchange_strong(expected_next, new_node,
                std::memory_order_release, std::memory_order_relaxed)) {
            return true;
        }
        // CAS failed: successor changed, or the anchor was marked
        // (the mark is part of the compared value). Reload; the reload
        // re-runs the mark check.
    }
}
```

This is the publishing protocol with two refinements. The mark check is the black-hole
guard: per the marking convention, a marked value read out of `anchor->next` means the
*anchor* is deleted, and linking a new node onto a corpse would publish it into a detached
chain that the next unlink throws away — so the insertion refuses. The second refinement is
free of charge: because the mark participates in the compared value, an anchor marked
*after* our check but *before* our CAS makes the CAS fail all by itself; the retry reloads
and the reload re-runs the check. There is no window. The relaxed store into the private
node needs no more than that — the node is unpublished until the release CAS, which
publishes node and store together, as always.

## Deletion

Deletion is where the list earns the adjective *Harris-style*, after Tim Harris's classic
algorithm: erasing a node is two separable steps, a *logical* deletion that decides the
race, and a *physical* unlink that merely tidies up.

```
  Step 1 -- mark (the linearization point; exactly one eraser wins):

     anchor ------> target --*--> successor        * = mark on TARGET'S OWN
                                                       next pointer

  Step 2 -- unlink (best-effort; anyone may do it):

     anchor ---------------------> successor
                    target --*-->  (unreachable from the list;
                                    alive while referenced)
```

```cpp
bool erase_after(iterator anchor) {
    if (!anchor.curr_) return false;
    while (true) {
        auto target = anchor.curr_->next.load(std::memory_order_acquire);
        if (!target || target.is_marked()) return false;  // nothing live to erase

        auto target_next = target->next.load(std::memory_order_acquire);
        bool marked_by_us = false;
        while (!target_next.is_marked()) {                // the marking race
            auto marked_next = target_next.set_mark();
            if (target->next.compare_exchange_strong(target_next, marked_next,
                    std::memory_order_release, std::memory_order_acquire)) {
                marked_by_us = true;
                target_next = marked_next;
                break;
            }
        }
        // target is now logically deleted -- by us or by somebody.
        // Physically unlink it (helping, when the mark is not ours):
        anchor.curr_->next.compare_exchange_strong(target,
                target_next.get_unmarked(),
                std::memory_order_release, std::memory_order_relaxed);

        if (marked_by_us) return true;   // the mark decided it; the unlink need not succeed
        // We lost the race and helped clean up; retry on the new successor.
    }
}
```

Read it with the two-step structure in mind, because every subtlety is a consequence of it.

*The mark is the linearization point.* The CAS that sets the mark on the target's own next
pointer is the moment the node leaves the list, logically; exactly one thread's CAS
transitions the word from unmarked to marked, so exactly one `erase_after` returns `true`
for this node. Everything after that CAS is housekeeping.

*The mark freezes the word forever.* Once the target is marked, `insert_after` refuses it as
an anchor and no second eraser can win the race — nothing will ever write that word again.
This is what licenses the audacity of the unlink: `target_next.get_unmarked()` is the
target's *final* successor, and it remains the correct value to install no matter how long
this thread stalls between the mark and the unlink. A lesser design, where the deleted
node's successor could still change, would make the delayed unlink a corruption; the frozen
mark makes it merely late.

*The unlink is best-effort, and failure is somebody else's success.* The unlink CAS on
`anchor->next` can lose — an insertion landed after the anchor, or the anchor itself got
erased. The dead node then simply stays linked, marked, until the next `erase_after` over
the same edge finds it: the marking race discovers an already-marked successor, falls
through to the unlink (this is the *helping*), and retries on whatever comes next. A retry
happens only after some CAS on the contested word succeeded — ours or a competitor's — which
is exactly the shape of a lock-free (not wait-free) guarantee: any individual thread can be
starved, the list cannot.

*And the CAS loops need no version counters.* Here is the A-B-A immunity, made concrete:
`target` in the code above is a strong reference. While this thread holds it, the target's
memory cannot be freed, so its address cannot be recycled, so the unlink CAS can never be
fooled by a same-address impostor. The witness reference we promised in the overview is not
a metaphor; it is a local variable.

It is worth knowing what deletion looks like *without* the mark, because for one of our
three policies this is not a hypothetical: the standard pointer cannot mark, and the list
it parameterizes runs the fallback — a direct unlink: load the target,
load its successor, CAS the anchor past it. It almost works, and "almost" here is
instructive. With the chain H → X → Y → Z, let one thread erase after H and another erase
after X. The first thread reads X's successor, Y. The second thread erases Y — its CAS of
X's next pointer from Y to Z succeeds, and it truthfully reports Y deleted. Now the first
thread's CAS swings H's next from X to... Y. The node Y, whose erasure was confirmed, is
back in the list. No memory was corrupted — every pointer is a valid strong reference, the
reference counts are immaculate — and the *semantics* are still wrong: a confirmed deletion
was undone. That is why the mark exists: logical deletion must be recorded *on the node*,
where every party can see it, not inferred from reachability, which no party can observe
atomically. (The head-anchored benchmarks that follow never erase at adjacent positions, so
the standard pointer competes there on equal semantic footing; but a standard-pointer-backed
list is confined to such disciplined access patterns, while the marking pointers permit
erasure anywhere.)

## Traversal, the graveyard, and destruction

The iterator is four lines and one decision:

```cpp
iterator& operator++() {
    if (curr_) {
        curr_ = curr_->next.load(std::memory_order_acquire).get_unmarked();
    }
    return *this;
}
```

The decision is the `get_unmarked()`. The value loaded from a deleted node's next pointer
carries the deleted node's mark, and the mark is part of a pointer's identity — so an
iterator that kept the mark would hold a pointer that compares *unequal* to an iterator that
reached the same node through the live chain, and unequal to `end()` if the last node was
erased. Iterators therefore canonicalize: they always hold unmarked values, and two
iterators at the same node are equal regardless of the roads they traveled. Note what the
increment deliberately does *not* do: it does not check whether the node it is stepping
*onto* is deleted — that fact lives in the next node's own next pointer, one load further
than we have gone. Traversal walks into deleted nodes by design:

```
   live list:    H ------------------------------> T4 -> T5
                        (unlinked, alive)
   graveyard:         T1 --*--> T2 --*--> T3 --*----^
                       ^
                  your iterator, parked on T1, serenely continues:
                  T1, T2, T3, T4, T5 -- through the graveyard
                  and back into the world of the living
```

Erasure never clears the victim's next pointer, so the graveyard chains lead back into the
live suffix, and a traversal that raced a bulk deletion completes unharmed, visiting some
nodes that were alive when it started. By the contract from the overview, no stronger
promise was ever on offer.

The last piece is the one nobody thinks about until it fires at 3 a.m.: the destructor.
Nodes hold smart pointers to nodes, so destroying a node destroys its successor, which
destroys *its* successor — the default destructor of a linked list is recursion in disguise,
and a million-node list dies of stack overflow on its way out. The cure is an iterative
walk, and its guard condition is a small proof:

```cpp
~Node() {
    auto curr = next.load(std::memory_order_relaxed);
    next.store(nullptr, std::memory_order_relaxed);
    while (curr && curr.use_count() == 1) {
        auto next_curr = curr->next.load(std::memory_order_relaxed);
        curr->next.store(nullptr, std::memory_order_relaxed);
        curr = std::move(next_curr);
    }
}
```

Why is `use_count() == 1` race-free, in a chapter that has spent pages sneering at
check-then-act on shared counters? Because this destructor runs only when its node is
already unreachable, and `curr` itself *is* one reference to the node it holds. A count of
exactly one therefore means: no link and no iterator anywhere refers to this node except
the local variable in our hand — no other thread can ever acquire it again, and we may
dismantle it. If the count is higher, we stop and walk away; whichever holder releases last
will re-enter this same destructor and resume the walk from there. Nulling each node's next
pointer *before* advancing is what keeps the whole thing iterative: the assignment to `curr`
destroys the previous node, whose own `~Node()` finds `next == nullptr` and returns without
recursing. And the relaxed ordering is honest, not lazy: exclusivity was already established
by the acquire-release decrement that brought each count to its final value — the
`DelRef` discipline from the structure section, paying its dividend.

## The results

The benchmarks run the same list with each of the three pointer policies filling the slot,
so every difference in the numbers is the pointer, and nothing but the pointer. Four
workloads, on the same 16-core, 32-thread desktop as before, list operations anchored at
the head, the list pre-populated with a thousand nodes. *Read-heavy* is 90% traversals of
up to fifty nodes, with a trickle of insertions and erasures to keep the front of the list
churning. *Write-heavy* inverts it: 90% insertions and erasures, with short traversals
mixed in. The *graveyard* workload is 70% erasure against 30% insertion — a list being
eaten faster than it is fed, nodes dying constantly, reclamation running at full boil; it
exists to measure exactly the machinery this section added. And a pure insertion workload
hammers the head CAS and the allocator with no relief.

The first result is the one this whole section has been building toward, and it is a
negative: **reads do not scale — for anyone.** The read-heavy workload delivers roughly two
million operations per second on one thread and roughly two million on thirty-two,
whichever pointer you pick; thirty-two times the silicon buys approximately nothing. There
is no mystery once you say it out loud: under reference counting, *readers are writers*.
Every hop of every traversal increments and decrements a counter, and every traversal
starts at the head, so the counters of the first few nodes are hammered by every reading
thread in the process — the cache line ping-pong we have met so many times, now generated
by operations that modify nothing the user can see. Set this against the previous section:
the hash set's readers, who registered nowhere and wrote nothing, scaled to 487 million
lookups per second on this machine. That three-orders-of-magnitude gulf *is* the price of
per-node reclamation, measured. Nothing in this section's engineering — not the intrusive
count, not the hazard pointers — escapes it, because it is not an implementation flaw; it
is the arithmetic of the approach.

Where the pointers do differ — spectacularly — is under write pressure, and the ranking
holds across every write-flavored workload. At thirty-two threads on the write-heavy mix,
the intrusive pointer sustains 6.3 million operations per second; Anderson's lock-free
pointer, 0.7 million; the standard pointer, 0.18 million — a factor of 34 between first and
last. The graveyard workload stretches it to 47× (13.1 million against 0.28 million), and
pure insertion to 68×. Three observations unpack these numbers.

*The standard pointer's collapse is our old spinlock lesson, relearned at a cost.* On one
thread the standard pointer is perfectly respectable — 10.2 million write-heavy operations
per second to the intrusive pointer's 11.8; the library is well made and nothing is wrong
with it in the absence of contention. It is even running the cheaper algorithm — no marking
support means no marking CAS, one step where the Harris pointers take two — and loses
anyway. But recall what both of these pointers are: one-bit spinlocks, the library's bit
stolen from the same word ours is. Under thirty-two threads, the standard pointer's threads
burn every nanosecond
of wall time as CPU time — the signature, by now familiar, of naive spinning — while the
intrusive pointer's threads show wall time five times their CPU time: they are *asleep*,
by design, its writers napping at the first sign of contention precisely so the lock
holder can keep its cache line and finish. Same lock, different manners, and the manners
are worth a factor of thirty. (And note how this inverts the previous section's acoustic
observation. There, the quiet machine was the slow one, readers convoyed in futex_wait
under a reader-writer lock. Here the loud machine is the slow one, spinning heat into a
contended line. The fans, it turns out, are an honest profiler but a poor oracle: they
tell you where the power goes, and it remains your job to know whether the power is doing
anything.)

*Lock-freedom buys graceful degradation, not speed.* Anderson's pointer is the only truly
lock-free contestant, and its curve shows exactly what that property is worth: no collapse,
no convoy, a steady controlled decline — at thirty-two threads it beats the standard
pointer by 4× on writes. What lock-freedom does not buy back is its constant factor: the
hazard-pointer handshake is a full StoreLoad barrier per pointer load, plus a detour
through the control block's cache line, and the single-threaded read-heavy numbers show
the bill — 0.64 million operations per second, against 2.8 for the standard pointer and
2.0 for the intrusive one. A fence per hop is a hard tax to outrun. The formal guarantee
and the fast path pull in opposite directions, and this data set is unusually clean
evidence that you generally choose one.

*The intrusive count wins on structure, then on manners.* One allocation per node instead
of two; the count on the node's own cache line instead of a control block's; a critical
section a handful of instructions long; and a backoff policy tuned by the measurements of
this chapter's spinlock sections. Nothing in the list is exotic — its advantage is the
accumulation of every boring decision made correctly.

Step back from the contest, and the section's real ledger is the comparison with its
predecessor. The hash set refused to reclaim and served half a billion wait-free reads per
second; the list reclaims perfectly — every node freed at the earliest safe moment, every
iterator forever valid — and moves single-digit millions. Between them lie the intermediate
schemes of our taxonomy, buying back throughput by relaxing immediacy. Neither endpoint is
"right": the hash set is unusable where memory must be returned, and the list is a
thirty-fold overpayment where it need not be. What this section adds is that the axis now
has numbers on it, and the numbers are large enough that the choice of reclamation
strategy is not a detail of a concurrent design — on read-heavy workloads it *is* the
design.

## Summary

We built a linked list in which every operation is a compare-and-swap and every pointer is
a reference: insertion is the publishing protocol behind a mark-guard, deletion is Harris's
two-step — a logical mark that decides the race and a physical unlink that anyone may
finish for anyone — and traversal walks fearlessly through the graveyard of deleted nodes
because deletion can make a node unreachable but never dead while someone still looks at
it. The A-B-A problem, introduced here in its classic form, was dispatched not by tagging
or versioning but structurally: a compare-and-swap whose expected value is a strong
reference cannot be deceived, because the reference is a witness that keeps the evidence
alive.

But the honest summary is the one from the overview: we did not really write a lock-free
list. We wrote an atomic shared pointer — three times — and one list on top,
and the benchmarks confirm the allocation of effort: every performance difference that
matters lives in the pointer. The load-increment gap at the heart of the atomic pointer
admits a lock (the standard's answer, and ours — ours merely with better manners under
contention, worth a factor of thirty), or hazard pointers (Anderson's answer — genuinely
lock-free, degrading gracefully, and paying a fence per load for the privilege). And
beneath all three, the arithmetic nobody escapes: reference-counted readers are writers,
so reads that scaled without limit in the previous section scale not at all in this one.
Full memory reclamation is not a feature you add to a concurrent data structure. It is a
different data structure, with a different bill — and now you have seen both bills, itemized.
