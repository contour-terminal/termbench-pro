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

#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
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
    out.clear();
    out.reserve(static_cast<std::size_t>(frame.width) * frame.height * 3u);
    for (auto const index: frame.indices)
    {
        auto const color = frame.palette[index];
        out.push_back(color.r);
        out.push_back(color.g);
        out.push_back(color.b);
    }
}

/// Constructs the image protocol registered under @p name.
///
/// This is the data-driven seam: adding a protocol is one new row in the registry table plus
/// its encoder — no call site changes.
///
/// @param name One of protocolNames().
/// @return The protocol instance, or nullptr if @p name is unknown.
[[nodiscard]] std::unique_ptr<ImageProtocol> makeProtocol(std::string_view name);

/// @return The names of all registered image protocols, in registration order.
[[nodiscard]] std::vector<std::string_view> protocolNames();

} // namespace img
