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
#include <cstdint>
#include <ranges>
#include <span>
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

    /// @return The Adler-32 checksum (RFC 1950) of @p data.
    /// @param data Bytes to checksum.
    [[nodiscard]] inline std::uint32_t adler32(std::span<std::uint8_t const> data) noexcept
    {
        constexpr std::uint32_t Mod = 65521u;
        constexpr std::size_t NMax = 5552; // largest run before a modulo is required
        std::uint32_t a = 1;
        std::uint32_t b = 0;

        auto const n = data.size();
        auto const blocks = (n + NMax - 1) / NMax;
        for (auto const blk: std::views::iota(std::size_t { 0 }, blocks))
        {
            auto const start = blk * NMax;
            auto const len = std::min(NMax, n - start);
            for (auto const byte: data.subspan(start, len))
            {
                a += byte;
                b += a;
            }
            a %= Mod;
            b %= Mod;
        }
        return (b << 16) | a;
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
/// The DEFLATE payload uses only uncompressed ("stored") blocks, so the encoder is fully
/// self-contained — no zlib, no third-party dependency. That trades payload size for zero
/// dependencies, which is acceptable (and a legitimate stress input) for a throughput benchmark.
///
/// @param out    Sink the PNG bytes are appended to.
/// @param rgb    Row-major RGB pixel data, size == width*height*3.
/// @param width  Image width in pixels (> 0).
/// @param height Image height in pixels (> 0).
inline void encode(std::string& out, std::span<std::uint8_t const> rgb, unsigned width, unsigned height)
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

    // zlib stream: 2-byte header, DEFLATE stored blocks, trailing Adler-32.
    std::vector<std::uint8_t> zlib;
    zlib.push_back(0x78u); // CMF: deflate, 32K window
    zlib.push_back(0x01u); // FLG: no preset dict, valid check bits for 0x7801

    constexpr std::size_t MaxBlock = 65535;
    auto const total = raw.size();
    auto const blockCount = total == 0 ? std::size_t { 1 } : (total + MaxBlock - 1) / MaxBlock;
    for (auto const block: std::views::iota(std::size_t { 0 }, blockCount))
    {
        auto const offset = block * MaxBlock;
        auto const blockLen = std::min(MaxBlock, total - offset);
        auto const isLast = (block + 1 == blockCount);

        zlib.push_back(isLast ? std::uint8_t { 1 } : std::uint8_t { 0 }); // BFINAL, BTYPE=stored
        auto const len = static_cast<std::uint16_t>(blockLen);
        auto const nlen = static_cast<std::uint16_t>(~len);
        zlib.push_back(static_cast<std::uint8_t>(len & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>(nlen & 0xFFu));
        zlib.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFFu));
        zlib.insert(zlib.end(),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + blockLen));
    }

    for (auto const byte: be32(adler32(raw)))
        zlib.push_back(byte);

    appendChunk(out, "IDAT", zlib);
    appendChunk(out, "IEND", {});
}

} // namespace png
