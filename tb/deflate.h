/**
 * This file is part of the "termbench" project
 *   Copyright (c) 2021 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <vector>

#include <zlib.h>

namespace zlib
{

/// Deflate level meaning "do not compress".
///
/// zlib still emits a valid RFC 1950 stream at this level, using only stored blocks. Protocols
/// treat it as the signal to stay on their uncompressed path.
inline constexpr int NoCompression = Z_NO_COMPRESSION;

/// Fastest deflate level.
inline constexpr int FastestLevel = Z_BEST_SPEED;

/// Smallest-output deflate level.
inline constexpr int MaxLevel = Z_BEST_COMPRESSION;

/// @return Whether @p level is a deflate level this wrapper accepts.
[[nodiscard]] constexpr bool isValidLevel(int level) noexcept
{
    return level >= NoCompression && level <= MaxLevel;
}

/// Why a deflate operation failed.
enum class Error
{
    InvalidLevel,  ///< The level was outside NoCompression..MaxLevel.
    InitFailed,    ///< deflateInit() could not allocate, or the stream was reset in a bad state.
    InputTooLarge, ///< The input exceeds what a single zlib pass can address.
    DeflateFailed, ///< deflate() reported an error mid-stream.
};

/// A reusable RFC 1950 (zlib) deflate stream.
///
/// The z_stream is created once and only reset per call, so a benchmark loop pays zlib's stream
/// setup and its internal window/hash allocations once rather than once per frame.
///
/// Construction cannot fail loudly (it is used from encoder constructors); an unusable stream is
/// reported from compress() instead, so the whole error surface is one std::expected.
class Deflator
{
  public:
    /// @param level Deflate level in NoCompression..MaxLevel. An invalid level leaves the deflator
    ///              unusable, and every compress() call then reports Error::InvalidLevel.
    explicit Deflator(int level): _level { level }
    {
        if (isValidLevel(level))
            _open = deflateInit(&_stream, level) == Z_OK;
    }

    ~Deflator()
    {
        if (_open)
            deflateEnd(&_stream);
    }

    // A z_stream records its own address internally, so neither copying nor moving it is safe.
    Deflator(Deflator const&) = delete;
    Deflator& operator=(Deflator const&) = delete;
    Deflator(Deflator&&) = delete;
    Deflator& operator=(Deflator&&) = delete;

    /// @return The deflate level this stream was constructed with.
    [[nodiscard]] int level() const noexcept { return _level; }

    /// Compresses @p data into @p out as one complete zlib stream.
    ///
    /// @param out  Destination; cleared, then filled with the compressed stream.
    /// @param data Raw bytes to compress.
    /// @return Nothing on success, otherwise why the compression failed.
    [[nodiscard]] std::expected<void, Error> compress(std::vector<std::uint8_t>& out,
                                                      std::span<std::uint8_t const> data)
    {
        if (!isValidLevel(_level))
            return std::unexpected(Error::InvalidLevel);
        if (!_open)
            return std::unexpected(Error::InitFailed);

        // zlib addresses its buffers with uInt, so an input beyond that would silently truncate.
        if (data.size() > std::numeric_limits<uInt>::max())
            return std::unexpected(Error::InputTooLarge);

        if (deflateReset(&_stream) != Z_OK)
            return std::unexpected(Error::InitFailed);

        auto const inputSize = static_cast<uInt>(data.size());
        out.resize(deflateBound(&_stream, inputSize));

        // zlib's next_in is a non-const Bytef*, though deflate() only reads through it; the cast
        // is what that C API requires of a const input.
        _stream.next_in = const_cast<Bytef*>(data.data());
        _stream.avail_in = inputSize;
        _stream.next_out = out.data();
        _stream.avail_out = static_cast<uInt>(out.size());

        // deflateBound sized `out` to hold the whole stream, so one Z_FINISH pass must complete it.
        if (deflate(&_stream, Z_FINISH) != Z_STREAM_END)
            return std::unexpected(Error::DeflateFailed);

        out.resize(out.size() - _stream.avail_out);
        return {};
    }

  private:
    int _level;           ///< The level this stream deflates at.
    z_stream _stream {};  ///< The zlib stream, live for this object's lifetime when _open.
    bool _open { false }; ///< Whether deflateInit() succeeded and deflateEnd() is owed.
};

} // namespace zlib
