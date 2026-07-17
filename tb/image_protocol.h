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

#include <tb/base64.h>
#include <tb/deflate.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace img
{

/// A 24-bit RGB color, one byte per channel (0..255).
struct Rgb
{
    std::uint8_t r {}; ///< Red channel.
    std::uint8_t g {}; ///< Green channel.
    std::uint8_t b {}; ///< Blue channel.

    constexpr auto operator<=>(Rgb const&) const noexcept = default;
};

/// A single, protocol-agnostic image frame: palette-indexed pixels plus their palette.
///
/// The plasma renderer produces exactly this; each protocol adapts it to its wire format
/// (Sixel consumes the indices+palette directly, RGB protocols expand `palette[index]`).
struct Image
{
    std::span<std::uint8_t const> indices; ///< Row-major palette indices, size == width*height.
    unsigned width {};                     ///< Image width in pixels.
    unsigned height {};                    ///< Image height in pixels.
    std::span<Rgb const> palette;          ///< Palette; every index must be < palette.size() (<= 256).

    /// The cell area the frame should occupy. Protocols able to state this explicitly should: it
    /// makes the benchmark independent of how a terminal derives its cell size from the window, so
    /// the number measures the protocol rather than the terminal's arithmetic.
    unsigned columns {}; ///< Target width in cells (0 = let the terminal decide).
    unsigned rows {};    ///< Target height in cells (0 = let the terminal decide).

    /// Which of a fixed pool of pre-rendered frames this is, for protocols whose FrameModel is
    /// Cyclic. Nullopt means a freshly computed, unnamed frame.
    std::optional<unsigned> poolId {};
};

/// How a protocol consumes the animation.
enum class FrameModel
{
    /// Every frame is freshly computed and transmitted whole. The default, and the only basis on
    /// which protocols compare fairly against each other.
    Streaming,

    /// A fixed pool of frames is uploaded once, then cycled by reference. This is what an image
    /// pool is *for*, but it measures a different thing: steady-state compute goes to zero and
    /// bytes/frame collapse, so the numbers are not comparable with Streaming ones.
    Cyclic,
};

/// Encodes image frames into one terminal image protocol's wire format.
///
/// Implementations are pure: they append bytes to a caller-owned sink and perform no I/O,
/// which keeps them unit-testable without a real terminal.
class ImageProtocol
{
  public:
    virtual ~ImageProtocol() = default;

    /// @return The protocol's registry name (e.g. "sixel").
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// @return How this protocol consumes the animation.
    ///
    /// Declared by the encoder rather than carried as a registry column, so that adding a protocol
    /// stays one row plus one header, and so the frame source is chosen from data rather than from
    /// a name comparison in the loop.
    [[nodiscard]] virtual FrameModel frameModel() const noexcept { return FrameModel::Streaming; }

    /// Encodes @p frame, appending the protocol's wire bytes to @p out.
    /// @param out   Sink the encoded bytes are appended to.
    /// @param frame The image frame to encode.
    virtual void encode(std::string& out, Image const& frame) = 0;
};

/// Appends the decimal representation of @p value to @p out (fast, allocation-free).
/// @param out   Sink the digits are appended to.
/// @param value Value to format.
inline void appendDecimal(std::string& out, unsigned long long value)
{
    std::array<char, 20> buffer {};
    auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

/// Expands a palette-indexed frame to packed 8-bit RGB (three bytes per pixel).
///
/// Shared by the RGB-oriented protocols (Kitty, iTerm2). Replaces the contents of @p out.
///
/// @param out   Destination buffer; cleared, then filled with width*height*3 bytes.
/// @param frame The frame whose indices are expanded through its palette.
inline void expandToRgb(std::vector<std::uint8_t>& out, Image const& frame)
{
    // Sized up front and written through an iterator: push_back would re-check the capacity for
    // every one of the three channels of every pixel. Callers reuse their buffer, so the resize
    // only ever allocates on the first frame.
    out.resize(frame.indices.size() * 3u);
    auto it = out.begin();
    for (auto const index: frame.indices)
    {
        auto const color = frame.palette[index];
        *it++ = color.r;
        *it++ = color.g;
        *it++ = color.b;
    }
}

/// The number of base64 characters one pixel of 24-bit RGB occupies. Three bytes in, four out.
inline constexpr std::size_t Base64CharsPerPixel = 4;

/// Encodes a palette-indexed frame directly as base64-encoded 24-bit RGB, without ever
/// materialising the RGB bytes.
///
/// Every pixel expands to exactly three bytes, and base64 encodes exactly three bytes per
/// four-character group, so pixel N is always base64 group N: no group straddles two pixels, and
/// width*height*3 is always divisible by 3 so there is no padding tail either. A palette holds at
/// most 256 entries, so every pixel's four output characters can be precomputed once per frame and
/// the image then encoded with one table lookup per pixel — no intermediate RGB buffer, and none
/// of its memory traffic.
///
/// The output is byte-identical to expandToRgb() followed by base64::encode(); this is the same
/// wire format produced faster, so that the terminal rather than the encoder stays the benchmark's
/// bottleneck.
///
/// @param out   Destination; cleared, then filled with indices.size()*4 characters. (Note this
///              replaces @p out, whereas base64::encode() appends to it.)
/// @param frame The frame whose indices are expanded through its palette.
inline void expandToBase64(std::string& out, Image const& frame)
{
    // One pre-encoded base64 group per palette entry.
    auto table = std::array<std::array<char, Base64CharsPerPixel>, 256> {};
    auto const paletteSize = std::min(frame.palette.size(), table.size());
    for (auto const i: std::views::iota(std::size_t { 0 }, paletteSize))
    {
        auto const color = frame.palette[i];
        auto const b0 = static_cast<unsigned>(color.r);
        auto const b1 = static_cast<unsigned>(color.g);
        auto const b2 = static_cast<unsigned>(color.b);
        table[i] = { base64::Alphabet[b0 >> 2],
                     base64::Alphabet[((b0 & 0x03u) << 4) | (b1 >> 4)],
                     base64::Alphabet[((b1 & 0x0Fu) << 2) | (b2 >> 6)],
                     base64::Alphabet[b2 & 0x3Fu] };
    }

    out.resize(frame.indices.size() * Base64CharsPerPixel);
    auto it = out.begin();
    for (auto const index: frame.indices)
        it = std::ranges::copy(table[index], it).out;
}

/// Knobs the driver supplies to protocol encoders at construction.
///
/// A struct rather than extra registry columns, so that adding a knob does not reshape the registry
/// table, and a protocol that does not care simply ignores it.
struct ProtocolOptions
{
    /// The zlib deflate level protocols that compress should use.
    ///
    /// zlib::NoCompression means: do not compress at all. Kitty then transmits raw RGB and omits
    /// its o=z key, and the PNG encoder emits stored blocks — i.e. each protocol's uncompressed
    /// wire format. Higher levels trade client CPU for fewer bytes on the wire.
    int compressionLevel { zlib::NoCompression };
};

/// Constructs the image protocol registered under @p name.
///
/// This is the data-driven seam: adding a protocol is one new row in the registry table plus
/// its encoder — no call site changes.
///
/// @param name    One of protocolNames().
/// @param options Knobs the encoder may honour; protocols that do not care ignore them.
/// @return The protocol instance, or nullptr if @p name is unknown.
[[nodiscard]] std::unique_ptr<ImageProtocol> makeProtocol(std::string_view name,
                                                          ProtocolOptions const& options = {});

/// @return The names of all registered image protocols, in registration order.
[[nodiscard]] std::vector<std::string_view> protocolNames();

} // namespace img
