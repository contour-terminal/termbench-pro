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


## Run tests
To run tests you need to spawn `tb` executable from terminal, for example:
` contour $(pwd)/build/tb/tb --fixed-size --column-by-column --size 2 --output contour_results`
for more info run `tb --hlep`
In folder scripts you can find some convinent scripts for testing different terminals and way to create results afterwards,
you can run

``` sh
./scripts/Xvfb-bench-run.sh $(pwd)/build/tb/tb
julia  scripts/plot_results.jl
```

This will run benchmark in background and plot results with julia afterwards

## Test coverage

- [x] long text lines
- [x] short text lines
- [x] text lines with foreground color
- [x] text lines with foreground & background color
- [x] cursor movement (`CUB`, `CUD`, `CUF`, `CUP`, `CUU`)
- [ ] rectangular operations (`DECCRA`, `DECFRA`, `DECERA`)
- [x] insert lines/columns (`IL`, `DECIC`)
- [ ] delete lines (`DL`)
- [ ] erase lines/columns (`EL`, `ED`)
- [ ] complex unicode LTR
- [ ] complex unicode RTL
- [ ] sixel image
