# Design notes

Why mortar looks the way it does: the constraints, the decisions, and the
tradeoffs I consciously accepted.

## Goals and non-goals

Goals: single-threaded correctness, explicit memory ownership, bounded
allocation churn on the hot path, and runtime diagnostics good enough to
debug allocator behavior from the outside.

Non-goals: networking, persistence, concurrency. mortar is a storage
*engine*, not a server. Threading in particular would force a lock
discipline on every structure here; it is out of scope by design.

## Architecture

```
        main.c (REPL / scripts)
              │
          store.c  ← public API: SET/GET/DEL/TTL, counters
              │
          htab.c   ← open addressing, tombstones, rehash policy
          │      │
     arena.c    mem.c
   (keys)     (values)
```

Keys and values follow different allocation strategies because they have
different lifecycles:

- **Keys are written once and read forever.** They are interned into a
  bump arena (`arena.c`). Individual keys are never freed — the arena is
  reclaimed *generationally*: every rehash rebuilds the live key set into
  a fresh arena and releases the old one wholesale.
- **Values churn constantly** (overwrites, deletes, expirations). They go
  through fixed-size slab pools (`mem.c`) sized 64 / 256 / 4096 bytes.
  Each payload carries a 16-byte header naming its pool, so free is O(1)
  with no allocator-side lookup. Exhausted pools and oversize requests
  cascade to `malloc(3)`, and the fallback is *counted*, not silent.

This split is the core idea: neither allocator is asked to do the other's
job. The arena never frees individuals (it can't, honestly); the slabs
never grow unboundedly (a fallback is cheaper than a lie).

## Hash table

- **FNV-1a 64-bit.** Chosen for speed and trivial implementation; the
  table caches each key's hash at insert time, so rehashes and probe
  chains re-compare hashes before touching `strcmp`.
- **Linear probing.** Contiguous memory, cache-friendly, and the load
  factor ceiling (70%) keeps average probe chains short.
- **Tombstones.** Deletion can't leave a hole in a probe chain, so
  deleted slots become tombstones. They are recycled opportunistically:
  a new key reuses the first tombstone its probe crosses.
- **Dual-threshold pressure policy.** When `(live + tombstones)` crosses
  70% of capacity, the next put() triggers a rehash — but *growth* only
  happens if live entries alone cross the same 70%. If the pressure is
  mostly tombstones, a same-capacity rehash sweeps them without doubling
  memory. This one decision is why delete-heavy workloads don't balloon
  the table.

## TTL: lazy eviction

Expiry is a timestamp compare on the read path. There is no timer wheel
and no background thread: an expired entry is reclaimed the moment
something asks for it. `TTL` for a logically-expired-but-unevicted key
reports `-2` (missing), which keeps the client-visible semantics coherent
even before the memory is reclaimed.

The tradeoff: dead entries hold memory until they are read again or a
rehash sweeps them. For mortar's use case (interactive sessions,
workloads that re-read keys) that is the right side of the tradeoff. A
timer wheel is the honest next step if write-heavy TTL churn matters.

## Ownership contract

The subtle part of any C data structure is who frees what. mortar's
rules:

1. The table interns keys — callers may reuse or free their key buffer
   immediately after `htab_put`.
2. Values are owned by the caller-supplied destructor
   (`htab_free_fn`). Overwrite, delete, TTL eviction, and shutdown all
   route through it — the tests assert the destructor fires exactly once
   per dropped value.
3. `store_set` copies both key and value; caller buffers are never
   retained.

## Failure model

- Allocation failure is fatal (`util.c:xmalloc` aborts loudly). mortar is
  a library of data structures that are hard to unwind mid-operation;
  threading NULL through every path would double the code and half its
  clarity, for a failure that in practice ends the process anyway.
- `mem_free` validates a magic word in the block header and aborts on a
  foreign or double free — a deliberate tripwire instead of silent pool
  corruption.

## Known tradeoffs

- Tombstoned keys keep their arena bytes until the next rehash.
- Single-threaded; see non-goals.
- Value payloads round up to 16 bytes; tiny values cost 16 extra header
  bytes. At mortar's scale that is cheaper than a side index.
- The slab layer is configured at `mem_init` (sizes, blocks per class);
  it does not grow. Watch `fallbacks` in `STATS` — that counter is the
  signal that pools need retuning.
