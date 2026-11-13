# mortar

**An in-memory key-value store, written from scratch in C11.**

Mortar is what holds bricks together. This holds bytes together: a small,
dependency-free key-value store with TTL expiry, built the way I like
systems code — explicit memory ownership, allocator-aware data structures,
and diagnostics you can actually inspect at runtime.

```
> SET beam "reinforced concrete"
OK
> SET load 42.5 EX 1
OK
> TTL load
(integer) 1000
> GET beam
reinforced concrete
```

## Features

- **Hash table from first principles** — open addressing, linear probing,
  FNV-1a, tombstone deletes, and a dual-threshold policy that decides
  between *growing* and *sweeping* tombstones on pressure.
- **Two custom allocators, working together** — a block-chained bump arena
  interns keys (reclaimed generationally on rehash), and fixed-size slab
  pools absorb value churn, cascading to `malloc(3)` with a live fallback
  count instead of pretending pools never run out.
- **Per-key TTL with two-layer eviction** — a 256-bucket timer wheel
  reclaims expired keys aggressively on `store_tick` (the caller drives
  the clock; no background thread), and exact lazy eviction on read
  stays as the correctness backstop.
- **A REPL client** (Redis-flavored: `SET key value [EX seconds]`, `GET`,
  `DEL`, `TTL`, `STATS`) that also runs script files, comments included.
- **Honest observability** — `STATS` reports slab high-water marks, heap
  fallbacks, tombstone counts, rehashes, pending expiry timers, and the
  key arena's footprint.
- **Test suite that runs under ASan + UBSan**, plus a micro-benchmark
  and a GitHub Actions matrix (gcc/clang × test/asan).

## Build

C11, no dependencies beyond libc. Clang and GCC both work.

```console
$ make            # bin/mortar (client) + bin/bench
$ make test       # build and run the unit tests
$ make asan       # rebuild everything under ASan + UBSan and run tests
$ make debug      # -O0 -g3 build meant for gdb sessions
$ make bench      # run the micro-benchmark
```

CMake works too, for editors that speak it:

```console
$ cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
$ cmake --build build-cmake
```

## Testing

26 cases across five binaries (`arena`, `mem`, `htab`, `store`,
`timers`), each asserting on behavior that matters: alignment
guarantees, pool recycling, tombstone bookkeeping, rehash survival of
50k entries, TTL semantics (`-1` no expiry, `-2` missing), wheel firing
order and stale-fire suppression, and counter accuracy.

```console
$ make test
== test_arena ==
  5 case(s), 0 failure(s)
== test_mem ==
  3 case(s), 0 failure(s)
== test_htab ==
  4 case(s), 0 failure(s)
== test_store ==
  9 case(s), 0 failure(s)
== test_timers ==
  5 case(s), 0 failure(s)
```

`make asan` runs the same suite with AddressSanitizer and UBSan enabled —
zero leaks, zero warnings is the bar, on your machine and in CI alike.

## Benchmark

`./bin/bench` — 262,144 SETs and GETs over a 65,536-key space, values
spread across the three slab size classes (Intel i7-9750H, clang -O2):

```
SET   1442230 ops/s
GET   3291819 ops/s  (84.2% hit)
```

The bench also prints allocator diagnostics after the run — slab
high-water marks, heap fallbacks, tombstone pressure, rehash count — so
allocator behavior can be compared across machines and parameter tweaks,
not just raw throughput.

## Project layout

```
src/        arena.c  block-chained bump allocator (key interning)
            mem.c    slab pools + heap fallback for values
            htab.c   open-addressing table with tombstones
            store.c  TTL, counters, ownership glue — the public API
            timers.c expiry wheel, driven by the caller's clock
            main.c   REPL client and script runner
tests/      one binary per module, harness in test.h
bench/      throughput + allocator utilization harness
docs/       DESIGN.md — architecture and the reasoning behind tradeoffs
            DEBUGGING.md — gdb, sanitizers and profiler workflows
```

Design notes and debugging workflows live in
[docs/DESIGN.md](docs/DESIGN.md) and
[docs/DEBUGGING.md](docs/DEBUGGING.md).

## License

MIT — see [LICENSE](LICENSE).
