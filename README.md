# gawk-cjp2p

A GAWK extension that adds unconnected UDP socket primitives. 

## Files

| File | Description |
|------|-------------|
| `udp.c` | GAWK extension -- four UDP functions |
| `Makefile` | Builds `udp.so` |

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

