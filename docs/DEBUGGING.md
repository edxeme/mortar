# Debugging mortar

The workflows I actually use on this codebase: gdb sessions against the
debug build, sanitizer runs, and profiler-guided reading of the bench.

## Build variants

```console
$ make debug     # -O0 -g3 — full debug info, no inlining to fight
$ make asan      # tests + client under ASan + UBSan
```

## A gdb session

The repo ships a [.gdbinit](../.gdbinit) with two helpers. Start with:

```console
$ gdb -x .gdbinit ./bin/mortar-dbg
(gdb) mortar-trace        # log store_set/store_get/store_del, no stopping
(gdb) run
```

`mortar-trace` installs silent breakpoints that print and continue, so a
script replay (`mortar -f script.txt`) becomes a command trace you can
correlate with `STATS` output.

**Watching the rehash policy.** The interesting variable is the pressure
counter. Break after inserts and inspect, or watch it:

```console
(gdb) break htab_put
(gdb) run
(gdb) watch -l t->tombstones
```

Every delete-plus-insert cycle prints the tombstone delta — the fastest
way I found to convince myself the recycle path actually recycles.

**Inspecting a slot.** The `entry` helper dumps one entry's state, cached
hash, expiry, and key:

```console
(gdb) entry &t->entries[42]
state=1 hash=... expires_at=0 key=beam
```

**Catching a lazy eviction.** Set a key with a 1-second TTL, then break
inside `htab_get`'s expiry branch. Watching `free_value` fire from there
is the clearest demonstration that eviction happens on the read path.

## The double-free tripwire

`mem_free` clears a magic word in the block header; a second free aborts
instead of corrupting the pool:

```console
$ ./bin/mortar-dbg
fatal: mem_free() on a foreign or corrupted block
Abort trap: 6
```

Under gdb this lands exactly at the offender — `backtrace` names the
call site that freed twice.

## Sanitizers + gdb

The `make asan` suite is the every-commit smoke test. For interactive
work, make ASan crash *into* gdb instead of printing and exiting:

```console
$ ASAN_OPTIONS='abort_on_error=1:detect_leaks=1' gdb ./bin/mortar-asan
(gdb) run
```

When ASan fires, the report plus `bt` distinguishes the three failure
classes worth distinguishing: heap-use-after-free (a value freed by the
destructor and still read), double-free (a destructor wired twice), and
leak (a destructor never wired). All three have bitten me in this exact
codebase during development, which is why the ownership contract in
DESIGN.md is written down at all.

valgrind works too (`valgrind --leak-check=full ./bin/test_store`) —
useful as a second opinion on macOS where ASan and the system allocator
occasionally disagree.

## Profiling

Throughput numbers mean nothing without knowing where time goes.

Linux, with `perf`:

```console
$ perf record -g ./bin/bench
$ perf report --no-children
```

The hot path should be `fnv1a64`, the probe loop, and `memcpy` — in that
order. When I see `strcmp` above the hash compare, the cached-hash
optimization has regressed.

macOS, no extra tools needed:

```console
$ sample ./bin/bench 5 -file /tmp/mortar.sample
$ heap ./bin/bench   # while it runs, to watch pool residency
```

One consistent finding from profiling this code: the slab high-water
marks in `STATS` predicted the perf profile. Pools absorbing 100% of
value churn showed flat allocator time; the moment `fallbacks` climbed,
`malloc` showed up in the profile. The counters are the cheap profiler.

## A worked example: proving tombstone accounting

I don't trust delete/reinsert logic I haven't watched. The sequence:

1. `make debug`, then `gdb -x .gdbinit ./bin/mortar-dbg`.
2. `break htab_del`, run a script that deletes five keys.
3. After each delete: `p t->tombstones` — must step 1, 2, 3, 4, 5.
4. `break htab_put`, insert a new key whose probe crosses a tombstone:
   `p recycle` must be non-NULL and the counter must step back down.
5. The same session proves the dual-threshold policy: force the table
   to 70% live and watch `t->capacity` double, then force it to 70%
   occupied-but-mostly-dead and watch the rehash keep capacity flat.

Ten minutes of this beats an afternoon of reading code and hoping.
