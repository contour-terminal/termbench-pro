# termbench

Termbench is a project to benchmark the terminal emulators backend bandwidth throughput.

Therefore not just a CLI executable (`tb`) will be provided but also a library
that one can link against to test against your own terminal emulator backend
without being affected by your rendering pipeline.

The library is written in modern C++17 with a strong focus on clean, readable,
and maintainable code so that other project might possibly also be able to
benefit from this library, if not even integrating it.

Adding a C-API binding is considered as soon there is also interest
from other projects that cannot use the C++ API.

## Test coverage

- [x] long text lines
- [x] short text lines
- [x] text lines with foreground color
- [x] text lines with foreground & background color
- [ ] cursor movement (`CUB`, `CUD`, `CUF`, `CUP`, `CUU`)
- [ ] rectangular operations (`DECCRA`, `DECFRA`, `DECERA`)
- [ ] insert lines/columns (`IL`, `DECIC`)
- [ ] delete lines (`DL`)
- [ ] erase lines/columns (`EL`, `ED`)
- [ ] complex unicode LTR
- [ ] complex unicode RTL
- [x] sixel image

## Image protocol benchmark (`image-bench`)

`image-bench` renders a full-screen animated plasma using a terminal image protocol and reports
realtime throughput metrics (a HUD at the bottom, plus a summary at exit). It benchmarks multiple
protocols behind a pluggable registry:

- `sixel` — DCS, palette-indexed
- `kitty` — Kitty graphics protocol (RGB, base64)
- `iterm2` — iTerm2 inline images (OSC 1337, PNG + base64)

Run it in an image-capable terminal and press `q` or `ESC` to quit:

```sh
image-bench --protocol sixel        # or: kitty, iterm2
image-bench --help                  # all options (colors, size, fps cap, duration, …)
```

For scripted/non-interactive benchmarking, `--duration S` auto-exits after `S` seconds.

### Compression

`--compression-level N` (0..9) sets the zlib deflate level for the protocols that can compress:
`kitty` (via the graphics protocol's `o=z`) and the PNG-based ones (`iterm2`, `gip-png`). The
others ignore it.

The default is `0`, meaning **no compression** — kitty transmits raw RGB and the PNG payload is
merely stored, which is what these protocols have always sent.

A compressed run measures a *different thing* and its numbers are **not comparable** with an
uncompressed one: the bytes counted are compressed bytes, it exercises the terminal's inflate path
rather than its raw parse path, and the deflating is charged to `image-bench` rather than to the
terminal. Measured on a 1000x1000 plasma frame, `kitty` gives up roughly half its frame rate at
level 1 to send ~5.8x fewer bytes, and about two thirds of it at level 6 to send ~11x fewer.
