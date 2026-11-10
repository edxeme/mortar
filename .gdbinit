# gdb init for mortar — start sessions with:
#   gdb -x .gdbinit ./bin/mortar-dbg
# then:  (gdb) mortar-trace
#        (gdb) run

set pagination off
set confirm off
set print pretty on
set history save on

# trace every store-level operation without stopping the program
define mortar-trace
  break store_set
  commands
    silent
    printf "[set] key=%s ttl_ms=%llu\n", key, (unsigned long long)ttl_ms
    continue
  end
  break store_get
  commands
    silent
    printf "[get] key=%s\n", key
    continue
  end
  break store_del
  commands
    silent
    printf "[del] key=%s\n", key
    continue
  end
  printf "tracing enabled: store_set / store_get / store_del\n"
end

# dump the entry slot a probe landed on
define entry
  printf "state=%u hash=%llu expires_at=%llu key=%s\n", $arg0->state, \
      (unsigned long long)$arg0->hash, (unsigned long long)$arg0->expires_at, \
      $arg0->key
end
