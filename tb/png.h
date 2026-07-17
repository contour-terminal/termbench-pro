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

#include <tb/deflate.h>

#include <array>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace png
{

namespace detail
{

    /// @return The CRC-32 (ISO 3309 / PNG) of @p data, continued from @p crc.
    /// @param data Bytes to fold into the running CRC.
    /// @param crc  Running CRC state (start at 0xFFFFFFFF; caller XORs 0xFFFFFFFF at the end).
    [[nodiscard]] inline std::uint32_t crc32(std::span<std::uint8_t const> data,
                                             std::uint32_t crc = 0xFFFFFFFFu) noexcept
    {
        static auto const table = [] {
            std::array<std::uint32_t, 256> t {};
            for (auto n: std::views::iota(std::uint32_t { 0 }, std::uint32_t { 256 }))
            {
                auto c = n;
                for ([[maybe_unused]] auto const k: std::views::iota(0, 8))
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[n] = c;
            }
            return t;
        }();

        for (auto const byte: data)
            crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
        return crc;
    }

    /// @return @p value as 4 big-endian bytes.
    [[nodiscard]] constexpr std::array<std::uint8_t, 4> be32(std::uint32_t value) noexcept
    {
        return { static_cast<std::uint8_t>((value >> 24) & 0xFFu),
                 static_cast<std::uint8_t>((value >> 16) & 0xFFu),
                 static_cast<std::uint8_t>((value >> 8) & 0xFFu),
                 static_cast<std::uint8_t>(value & 0xFFu) };
    }

    /// Appends @p value to @p out as 4 big-endian bytes.
    inline void appendBE32(std::string& out, std::uint32_t value)
    {
        for (auto const byte: be32(value))
            out.push_back(static_cast<char>(byte));
    }

    /// Appends one PNG chunk (length, type, data, CRC-32 over type+data) to @p out.
    /// @param out  Sink the chunk is appended to.
    /// @param type The 4-character chunk type (e.g. "IHDR"); the trailing NUL is ignored.
    /// @param data The chunk payload.
    inline void appendChunk(std::string& out, char const (&type)[5], std::span<std::uint8_t const> data)
    {
        appendBE32(out, static_cast<std::uint32_t>(data.size()));

        auto const crcStart = out.size();
        out.append(type, 4);
        out.append(reinterpret_cast<char const*>(data.data()), data.size());

        auto const* crcData = reinterpret_cast<std::uint8_t const*>(out.data()) + crcStart;
        auto const crc = crc32({ crcData, 4 + data.size() }) ^ 0xFFFFFFFFu;
        appendBE32(out, crc);
    }

} // namespace detail

/// Encodes an 8-bit truecolor (RGB) image as a PNG byte stream appended to @p out.
///
/// The PNG framing (signature, chunks, CRCs) is written here; the IDAT payload is deflated by the
/// caller-supplied @p deflator, which carries the level. At zlib::NoCompression that payload is
/// stored rather than compressed, which keeps the encoder's output a deliberately large throughput
/// stress input — the default — while a higher level trades client CPU for wire bytes.
///
/// The deflator is injected rather than created here so that a benchmark loop can reuse one stream
/// across frames instead of paying zlib's setup per frame.
///
/// @param out      Sink the PNG bytes are appended to.
/// @param rgb      Row-major RGB pixel data, size == width*height*3.
/// @param width    Image width in pixels (> 0).
/// @param height   Image height in pixels (> 0).
/// @param deflator The deflate stream compressing the IDAT payload.
inline void encode(std::string& out,
                   std::span<std::uint8_t const> rgb,
                   unsigned width,
                   unsigned height,
                   zlib::Deflator& deflator)
{
    using namespace detail;

    // 8-byte PNG signature.
    constexpr std::array<std::uint8_t, 8> Signature { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    out.append(reinterpret_cast<char const*>(Signature.data()), Signature.size());

    // IHDR: width, height, bit depth 8, color type 2 (truecolor), no compression/filter/interlace flags.
    std::array<std::uint8_t, 13> ihdr {};
    std::ranges::copy(be32(width), ihdr.begin());
    std::ranges::copy(be32(height), ihdr.begin() + 4);
    ihdr[8] = 8u;  // bit depth
    ihdr[9] = 2u;  // color type: truecolor
    ihdr[10] = 0u; // compression method
    ihdr[11] = 0u; // filter method
    ihdr[12] = 0u; // interlace method
    appendChunk(out, "IHDR", ihdr);

    // Raw scanlines: each row is prefixed with filter-type byte 0 (None).
    auto const rowBytes = static_cast<std::size_t>(width) * 3u;
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1u + rowBytes));
    for (auto const y: std::views::iota(0u, height))
    {
        raw.push_back(std::uint8_t { 0 });
        auto const rowStart = static_cast<std::size_t>(y) * rowBytes;
        raw.insert(raw.end(),
                   rgb.begin() + static_cast<std::ptrdiff_t>(rowStart),
                   rgb.begin() + static_cast<std::ptrdiff_t>(rowStart + rowBytes));
    }

    // The zlib stream — header, DEFLATE blocks and the trailing Adler-32 — is what IDAT carries.
    std::vector<std::uint8_t> compressed;
    if (auto const result = deflator.compress(compressed, raw); !result)
        // deflateBound sized the output and the level was validated at the CLI boundary, so a
        // failure here is a broken invariant (or OOM) rather than a recoverable condition. The
        // pure layer reports it as a value; encode() cannot, so it escalates.
        throw std::runtime_error { "png: deflating the image data failed" };

    appendChunk(out, "IDAT", compressed);
    appendChunk(out, "IEND", {});
}

} // namespace png
