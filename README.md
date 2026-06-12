# gawk-cjp2p

A GAWK extension that adds unconnected UDP socket primitives, plus a
`presence.awk` demo that joins the [cjp2p](https://cjp2p.org) peer
network and implements the same presence protocol as `presence.py` and
`presence.hs`.

## Files

| File | Description |
|------|-------------|
| `udp.c` | GAWK extension -- four UDP functions |
| `Makefile` | Builds `udp.so` |
| `presence.awk` | Presence demo (parallel to `presence.py` / `presence.hs`) |

## Building

You need:
- `gawk >= 5.0`
- `gawkapi.h` (ships with the gawk source, or in a `gawk-dev` / `gawk-devel` package)

- `gawk-json` from [gawkextlib](https://gawkextlib.sourceforge.net/) (for `presence.awk`)
  - On this system: `/usr/lib/x86_64-linux-gnu/gawk/json.so` (already installed)
  - Functions are namespaced: `json::from_json` / `json::to_json`

```sh
# Debian/Ubuntu -- get headers
apt install gawk

# If gawkapi.h is not in /usr/include/gawk/, copy it from the gawk source
# tree or set GAWK_INCLUDE:
#   git clone https://git.savannah.gnu.org/git/gawk.git
#   cp gawk/gawkapi.h .

make
make check        # quick smoke-test (open+close only, no json needed)
```

### Locating gawkapi.h

The Makefile tries these in order:
1. `$GAWK_INCLUDE` environment variable
2. `pkg-config --cflags gawk`
3. The current directory (copy `gawkapi.h` here as a fallback)

## UDP extension API

Load in a GAWK script with:
```awk
@load "udp"
```

No other libraries are needed — `presence.awk` includes a self-contained JSON parser.

Then set `AWKLIBPATH` to the directory containing `udp.so` when running,
or install `udp.so` to `/usr/local/lib/gawk/` with `make install`.

### Functions

#### `udp_open(port)` -> fd

Creates a `SOCK_DGRAM` socket and binds it to `0.0.0.0:port`.
Use `port=0` for an ephemeral (send-only) socket.
Returns a file-descriptor number, or -1 on error.

#### `udp_sendto(fd, data, host, port)` -> bytes_sent

Sends `data` (a string) as a single UDP datagram to `host:port`.
`host` may be a dotted-decimal address or a hostname.
Returns bytes sent, or -1 on error.

#### `udp_recvfrom(fd, result [, timeout_ms])` -> bytes_received

Receives one datagram.  If `timeout_ms` is given, waits at most that
many milliseconds; returns 0 on timeout.  Returns -1 on error.

On success populates the GAWK array `result`:
- `result["data"]` -- payload string
- `result["host"]` -- sender IPv4 address (dotted-decimal)
- `result["port"]` -- sender port (as a string)

#### `udp_close(fd)`

Closes the socket.

### Minimal example

```awk
@load "udp"
BEGIN {
    fd = udp_open(0)                          # ephemeral port
    udp_sendto(fd, "hello", "127.0.0.1", 9999)
    n = udp_recvfrom(fd, pkt, 2000)           # 2 s timeout
    if (n > 0)
        print "got:", pkt["data"], "from", pkt["host"] ":" pkt["port"]
    udp_close(fd)
}
```

## Running presence.awk

```sh
# Build the extension first.
make

# Run (NAME defaults to "anon", or pass as first argument or env var).
AWKLIBPATH=. gawk -f presence.awk alice

# Or, if gawk-json is installed system-wide but udp.so is local:
AWKLIBPATH=.:/usr/lib/gawk gawk -f presence.awk alice
```

Every 5 seconds it sends `IAmHere` to all known peers.
Every 10 seconds it prints who it has seen recently:

```
Listening on UDP 24254 as alice

--- Who is here (alice) ---
  alice                148.71.89.128:24254    3s ago
  bob                  203.0.113.42:24254     7s ago
```

## Protocol

The demo uses the same wire protocol as `presence.py` and `presence.hs`.
All messages are JSON arrays sent as single UDP datagrams on port 24254.

Custom message types (ignored by the cjp2p Rust node):

```
IAmHere        {"name": "alice", "t": 1718000000}
WhoIsHere      {}
HereIsWho      {"nodes": [{"name":"...", "t":..., "addr":"..."}]}
```

Standard cjp2p message types also used:

```
PleaseSendPeers              {}
Peers                        {"peers": ["ip:port", ...]}
PleaseAlwaysReturnThisMessage  "cookie"
AlwaysReturned               "cookie"
```

Anti-amplification: if a peer has not yet echoed our cookie, and the
response would exceed 2.5x the request size, the response is trimmed to
just our cookie request plus at most one peer address.

## Differences from presence.py / presence.hs

- **Cookies**: per-peer random hex strings stored in an awk array,
  rather than HMAC-SHA256 derived from a session secret.  Functionally
  equivalent; the HMAC approach trades per-peer state for statelessness
  at large scale.
- **No threads**: a single event loop with a 1-second `recvfrom` timeout
  drives both I/O and the ping / print timers.
- **JSON construction**: outgoing messages are assembled as literal
  strings; only incoming messages use the json library for parsing.

## License

MIT
