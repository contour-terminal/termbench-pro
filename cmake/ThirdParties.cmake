include(CPM)

set(3rdparty_fmt_version "8465869d7b04ae00f657332b3374ecf95b5e49c0" CACHE STRING "fmt: commit hash")
set(3rdparty_range_v3_version "0487cca29e352e8f16bbd91fda38e76e39a0ed28" CACHE STRING "range_v3: commit hash")

CPMAddPackage(
  NAME fmt
  VERSION ${3rdparty_fmt_version}
  URL https://github.com/fmtlib/fmt/archive/${3rdparty_fmt_version}.zip
  URL_HASH SHA256=3c0a45ee3e3688b407b4243e38768f346e75ec4a9b16cefbebf17252048395da
  EXCLUDE_FROM_ALL YES
)

CPMAddPackage(
  NAME range_v3
  VERSION ${3rdparty_range_v3_version}
  URL https://github.com/ericniebler/range-v3/archive/${3rdparty_range_v3_version}.zip
  URL_HASH SHA256=e3992d30629d058e5918b9721d6fbdbc20f72b298cdf5cfb96e798fc4b5b54fe
  EXCLUDE_FROM_ALL YES
)

