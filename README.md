# Custom Hash Map — C++ Implementation

Two from-scratch implementations of a hash map in C++, built without using `std::unordered_map`. The goal was to understand what actually happens under the hood — from hash functions and collision resolution to tombstone deletion and rehashing.

---

## What is a hash map?

A hash map lets you store and retrieve key-value pairs in **O(1) average time**. The idea is simple: instead of searching through a list, you mathematically convert the key into an array index and jump straight there.

The pipeline every lookup goes through:

```
key  →  hash function  →  huge integer  →  % N  →  array index  →  value
```

The `% N` (modulo) squeezes the integer into a valid index for an array of size N. If nothing else occupies that slot — you're done. If something does, that's a **collision**, and how you resolve it defines the two approaches below.

---

## Approach 1 — Separate Chaining

### How it works

The internal array doesn't store values directly. Each slot holds the **head of a linked list** (called a chain or bucket). When two keys hash to the same index, they both live in the same list at that index — one after the other.

```
index 0  →  NULL
index 1  →  ["alice", 25]  →  ["carol", 31]  →  NULL   ← collision handled here
index 2  →  NULL
index 3  →  ["bob", 40]  →  NULL
```

### Key terms

**Bucket** — a single slot in the array, holding a linked list of entries that share the same hash index.

**Chain** — the linked list inside a bucket. If no collisions occur, each chain has exactly one node. Under heavy load, chains grow longer.

**Collision** — two different keys producing the same array index after hashing. Separate chaining handles this gracefully by appending to the list.

**Load factor (λ)** — `count / number_of_buckets`. At λ = 1.0, there is on average one element per bucket. Separate chaining tolerates λ up to ~1.0 before performance noticeably degrades, because long chains are the only cost.

**Rehashing** — when λ exceeds the threshold, the internal array is doubled in size and all existing keys are re-inserted. Every key gets a new home because `hash % (2N)` produces different results than `hash % N`.

### Operations

| Operation | Average case | Worst case |
|-----------|-------------|------------|
| Insert    | O(1)        | O(n) — all keys in one chain |
| Search    | O(1)        | O(n) |
| Delete    | O(1)        | O(n) |

Delete is clean — you just unlink the node from its list. No special markers needed.

### C++ headers used

**`<vector>`** — the outer array of buckets is a `std::vector<Bucket>`. Gives contiguous storage for the bucket array itself and handles resizing on rehash cleanly.

**`<list>`** — each bucket is a `std::list<pair<K,V>>`. A doubly-linked list is used because deletion from the middle of a chain is O(1) given an iterator — you just relink the neighbours.

**`<functional>`** — provides `std::hash<K>{}(key)`, the standard library's hash functor for common types like `int`, `string`, and pointers. Used to turn any key into a `size_t` integer.

**`<stdexcept>`** — used to throw `std::out_of_range` in `get()` when a key is not found, matching the contract of standard containers.

---

## Approach 2 — Open Addressing with Linear Probing

### How it works

No linked lists. No heap allocations beyond a single flat array. Every key lives **directly in the array**, and every slot is one of three states:

- **Empty** — nothing has ever touched this slot
- **Occupied** — a live key-value pair lives here
- **Tombstone** — a key was here, got deleted, left a marker

When two keys hash to the same index, the second one **walks forward** through the array until it finds an empty slot. This walking is called **probing**.

```
Insert "alice"  →  home = 3, slot 3 empty  →  place at 3
Insert "carol"  →  home = 3, slot 3 occupied  →  probe slot 4, empty  →  place at 4
```

### Key terms

**Home slot** — the index produced by `hash(key) % N`. Where the key *wants* to live. If it's free, the key goes there directly.

**Linear probing** — on collision, check slot+1, slot+2, slot+3... in order until an empty slot is found. The array is treated as circular — after the last slot, wrap back to index 0.

**Primary clustering** — the main weakness of linear probing. Occupied slots form long consecutive runs. Any new key hashing *anywhere* into that run extends it further, making future insertions and searches slower. This is why the load factor threshold is kept lower than with chaining.

**Load factor (λ)** — `count / capacity`. With open addressing, λ must be kept below ~0.6–0.7. Beyond that, clusters grow large enough that probe chains become seriously long.

**Probe count** — the number of slots examined during a single operation. At λ = 0.3, the average probe count is ~1.2. At λ = 0.9, it climbs to ~5–6.

**Tombstone** — a deletion marker. When a key is deleted, you cannot blank the slot out completely. If you did, a future search for a key that was *pushed past this slot* by linear probing would stop here and incorrectly conclude that key doesn't exist.

```
Before delete:  [ ... | "alice" | "carol" | ... ]   ← indices 3, 4
Naive delete:   [ ... |  EMPTY  | "carol" | ... ]   ← search for "carol" stops at 3 ✗
Tombstone:      [ ... |  TOMB   | "carol" | ... ]   ← search skips past 3, finds 4 ✓
```

Tombstones also participate in insertion — the first tombstone found during a probe sequence is recorded and reused as the insertion site, placing the key as early in the chain as possible.

**Rehashing** — same trigger as chaining (λ > threshold), but tombstones are *not* copied. Rehash is a free tombstone flush — the rebuilt table only contains live keys.

### Operations

| Operation | Average case | Worst case |
|-----------|-------------|------------|
| Insert    | O(1)        | O(n) — full table scan |
| Search    | O(1)        | O(n) |
| Delete    | O(1) — marks tombstone | O(n) |

### C++ headers used

**`<vector>`** — the entire hash map is a single `std::vector<Slot>`, where `Slot` is a struct holding the key, value, and two booleans (`occupied`, `tombstone`). One contiguous allocation — this is the source of the cache advantage.

**`<functional>`** — same as above, `std::hash<K>{}(key)` to convert the key to an integer before taking modulo.

**`<stdexcept>`** — used in `find()` and `erase()` for error signalling on key-not-found cases.

**`<optional>`** — useful when you want `find()` to return an `std::optional<V>` instead of a raw pointer, giving callers a cleaner null-safety story without raw pointer arithmetic.

---

## Why open addressing is faster in practice

With separate chaining, finding a key in a non-empty bucket involves a **pointer chase** — you dereference the list node pointer, which points somewhere on the heap. Each dereference is a potential cache miss: the CPU has to pause and fetch from main memory.

With a flat array, probing slot+1 after slot 0 reads an adjacent memory address. Because CPUs load memory in **cache lines** (typically 64 bytes), slot+1 is almost certainly already loaded from the previous access. No round-trip to RAM.

```
Chaining lookup:   hash → array[i] → pointer → heap node → compare → maybe another pointer
Open addressing:   hash → array[i] → compare → array[i+1] (already in cache) → compare
```

This is why production flat hash maps like `absl::flat_hash_map` and `robin_hood::unordered_map` outperform `std::unordered_map` on benchmarks — they trade pointer chasing for sequential memory access.

---

## Improvements over a naive implementation

| Feature | Naive | This implementation |
|---------|-------|---------------------|
| Tombstone reuse on insert | No | Yes — first tombstone in probe sequence is recorded and reused |
| Rehash trigger | Manual | Automatic at λ > 0.6 |
| Tombstone flush | Never | Free on every rehash |
| Generic key/value types | Fixed types | Full `template <typename K, typename V>` |
| Update on duplicate insert | Undefined | In-place value update |
| Safe deletion | Blank-out (breaks probing) | Tombstone marker |

---

## Files

```
.
├── Chaining_HashMap.cpp       # separate chaining implementation
├── Optimized_HashMap.cpp      # open addressing with linear probing
└── README.md
```

---

## Building

```bash
g++ -std=c++17 Chaining_HashMap.cpp  -o Chaining_HashMap  && ./Chaining_HashMap
g++ -std=c++17 Optimized_HashMap.cpp -o Optimized_HashMap && ./Optimized_HashMap
```

Requires GCC 7+ or any compiler with C++17 support. No external dependencies.
