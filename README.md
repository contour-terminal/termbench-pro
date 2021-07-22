# termbench-pro

Termbench Pro is the interim name for a project to benchmark
the terminal emulators backend bandwidth throughput.

Therefore not just a CLI executable will be provided but also a library
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
- [ ] sixel image
