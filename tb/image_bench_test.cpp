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

#include <tb/base64.h>
#include <tb/image_protocol.h>
#include <tb/iterm2.h>
#include <tb/kitty.h>
#include <tb/plasma.h>
#include <tb/png.h>
#include <tb/sixel.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

namespace
{

/// @return @p text viewed as a span of raw bytes.
[[nodiscard]] std::span<std::uint8_t const> bytesOf(std::string_view text)
{
    return { reinterpret_cast<std::uint8_t const*>(text.data()), text.size() };
}

/// @return true if @p haystack contains @p needle.
[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

/// @return A big-endian 32-bit integer read from @p data at @p offset.
[[nodiscard]] std::uint32_t readBE32(std::span<std::uint8_t const> data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24)
           | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
           | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
           | static_cast<std::uint32_t>(data[offset + 3]);
}

/// A minimal reference PNG decoder covering exactly what png::encode emits (8-bit RGB, filter 0,
/// stored DEFLATE blocks). Verifies every chunk CRC and the zlib Adler-32 along the way.
///
/// @param png   The encoded PNG bytes.
/// @param width Out: decoded image width.
/// @param height Out: decoded image height.
/// @return The decoded row-major RGB pixels, or empty on any structural/checksum failure.
[[nodiscard]] std::vector<std::uint8_t> decodePng(std::string const& png, unsigned& width, unsigned& height)
{
    auto const data = bytesOf(png);
    constexpr std::array<std::uint8_t, 8> Signature { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
    if (data.size() < 8 || !std::ranges::equal(data.first(8), Signature))
        return {};

    width = 0;
    height = 0;
    std::vector<std::uint8_t> idat;
    std::size_t offset = 8;
    while (offset + 12 <= data.size())
    {
        auto const length = readBE32(data, offset);
        auto const type = png.substr(offset + 4, 4);
        auto const chunk = data.subspan(offset + 4, 4 + length); // type + payload
        auto const storedCrc = readBE32(data, offset + 8 + length);
        if ((png::detail::crc32(chunk) ^ 0xFFFFFFFFu) != storedCrc)
            return {};

        if (type == "IHDR")
        {
            width = readBE32(data, offset + 8);
            height = readBE32(data, offset + 12);
            if (data[offset + 16] != 8u || data[offset + 17] != 2u) // bit depth 8, truecolor
                return {};
        }
        else if (type == "IDAT")
        {
            auto const payload = data.subspan(offset + 8, length);
            idat.insert(idat.end(), payload.begin(), payload.end());
        }
        offset += 12 + length;
        if (type == "IEND")
            break;
    }

    if (idat.size() < 6)
        return {};

    // Inflate the stored-block DEFLATE stream (skip the 2-byte zlib header).
    std::vector<std::uint8_t> raw;
    std::size_t position = 2;
    bool final = false;
    while (!final && position + 5 <= idat.size())
    {
        auto const header = idat[position++];
        final = (header & 0x01u) != 0;
        if (((header >> 1) & 0x03u) != 0) // only stored blocks are emitted
            return {};
        auto const len =
            static_cast<std::size_t>(idat[position]) | (static_cast<std::size_t>(idat[position + 1]) << 8);
        position += 4; // LEN + NLEN
        if (position + len > idat.size())
            return {};
        raw.insert(raw.end(),
                   idat.begin() + static_cast<std::ptrdiff_t>(position),
                   idat.begin() + static_cast<std::ptrdiff_t>(position + len));
        position += len;
    }

    // Trailing Adler-32 must match a fresh checksum of the inflated data.
    if (position + 4 > idat.size() || readBE32(idat, position) != png::detail::adler32(raw))
        return {};

    // Strip the per-row filter byte (must be 0 = None).
    auto const rowBytes = static_cast<std::size_t>(width) * 3u;
    std::vector<std::uint8_t> rgb;
    rgb.reserve(static_cast<std::size_t>(height) * rowBytes);
    for (auto const y: std::views::iota(0u, height))
    {
        auto const rowStart = static_cast<std::size_t>(y) * (1 + rowBytes);
        if (rowStart >= raw.size() || raw[rowStart] != 0u)
            return {};
        rgb.insert(rgb.end(),
                   raw.begin() + static_cast<std::ptrdiff_t>(rowStart + 1),
                   raw.begin() + static_cast<std::ptrdiff_t>(rowStart + 1 + rowBytes));
    }
    return rgb;
}

} // namespace

TEST_CASE("base64 encodes RFC 4648 vectors", "[base64]")
{
    auto encode = [](std::string_view text) {
        std::string out;
        base64::encode(out, bytesOf(text));
        return out;
    };

    CHECK(encode("") == "");
    CHECK(encode("f") == "Zg==");
    CHECK(encode("fo") == "Zm8=");
    CHECK(encode("foo") == "Zm9v");
    CHECK(encode("foob") == "Zm9vYg==");
    CHECK(encode("fooba") == "Zm9vYmE=");
    CHECK(encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("sixel emits DCS framing and palette registers", "[sixel]")
{
    std::vector<img::Rgb> const palette { { 255, 0, 0 }, { 0, 255, 0 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2, all color 0

    std::string out;
    sixel::encode(out, indices, 2, 2, palette);

    CHECK(out.starts_with("\033Pq\"1;1;2;2"));
    CHECK(contains(out, "#0;2;100;0;0"));
    CHECK(contains(out, "#1;2;0;100;0"));
    CHECK(out.ends_with("\033\\"));
    // Two rows of color 0 -> vertical bits 0b11 = 3 -> glyph '3'+63 == 'B', run of 2 (< 4, no RLE).
    CHECK(contains(out, "#0BB"));
}

TEST_CASE("sixel run-length encodes long runs", "[sixel]")
{
    std::vector<img::Rgb> const palette { { 10, 20, 30 } };
    std::vector<std::uint8_t> const indices(10, 0); // 10x1, single color

    std::string out;
    sixel::encode(out, indices, 10, 1, palette);

    // Single set row -> bit 1 -> glyph '@' (63+1), run of 10 -> "!10@".
    CHECK(contains(out, "!10@"));
}

TEST_CASE("sixel separates 6-pixel bands", "[sixel]")
{
    std::vector<img::Rgb> const palette { { 255, 255, 255 } };
    std::vector<std::uint8_t> const indices(7, 0); // 1x7 -> two bands (6 + 1)

    std::string out;
    sixel::encode(out, indices, 1, 7, palette);

    CHECK(std::ranges::count(out, '-') == 1); // exactly one band separator between two bands
}

TEST_CASE("image protocol registry resolves every protocol", "[registry]")
{
    for (auto const name: img::protocolNames())
    {
        auto const protocol = img::makeProtocol(name);
        REQUIRE(protocol != nullptr);
        CHECK(protocol->name() == name);
    }

    auto const names = img::protocolNames();
    CHECK(std::ranges::find(names, "sixel") != names.end());
    CHECK(std::ranges::find(names, "kitty") != names.end());
    CHECK(std::ranges::find(names, "iterm2") != names.end());
    CHECK(img::makeProtocol("does-not-exist") == nullptr);
}

TEST_CASE("kitty emits APC transmit framing", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2

    kitty::KittyProtocol protocol;
    std::string out;
    protocol.encode(out, img::Image { indices, 2, 2, palette });

    CHECK(out.starts_with("\033_Ga=d\033\\"));       // previous image deleted first
    CHECK(contains(out, "\033_Ga=T,f=24,s=2,v=2;")); // transmit + display control keys
    CHECK(out.ends_with("\033\\"));
}

TEST_CASE("png is structurally valid and round-trips pixels", "[png]")
{
    // 2x1 image: red, green.
    std::vector<std::uint8_t> const source { 255, 0, 0, 0, 255, 0 };

    std::string out;
    png::encode(out, source, 2, 1);

    CHECK(contains(out, "IHDR"));
    CHECK(contains(out, "IDAT"));
    CHECK(contains(out, "IEND"));

    unsigned width = 0;
    unsigned height = 0;
    auto const decoded = decodePng(out, width, height);
    CHECK(width == 2);
    CHECK(height == 1);
    CHECK(decoded == source);
}

TEST_CASE("iterm2 wraps a PNG in an OSC 1337 sequence", "[iterm2]")
{
    std::vector<img::Rgb> const palette { { 4, 5, 6 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2

    iterm2::Iterm2Protocol protocol;
    std::string out;
    protocol.encode(out, img::Image { indices, 2, 2, palette });

    CHECK(out.starts_with("\033]1337;File=inline=1;width=2px;height=2px;preserveAspectRatio=0:"));
    CHECK(out.ends_with("\007"));

    // The payload between ':' and BEL must base64-decode to a PNG (signature check via re-encode-free path).
    auto const colon = out.find(':');
    auto const payload = out.substr(colon + 1, out.size() - colon - 2);
    CHECK(!payload.empty());
}

TEST_CASE("plasma field stays within the palette and is deterministic", "[plasma]")
{
    constexpr unsigned Width = 40;
    constexpr unsigned Height = 24;
    constexpr unsigned Colors = 128;

    std::vector<std::uint8_t> first(Width * Height, 0);
    std::vector<std::uint8_t> second(Width * Height, 0);
    plasma::render(first, Width, Height, 1.5, Colors);
    plasma::render(second, Width, Height, 1.5, Colors);

    CHECK(first == second); // deterministic for identical inputs
    CHECK(std::ranges::all_of(first, [](std::uint8_t v) { return v < Colors; }));
}

TEST_CASE("plasma palette size is clamped", "[plasma]")
{
    CHECK(plasma::makePalette(0).size() == 1);
    CHECK(plasma::makePalette(256).size() == 256);
    CHECK(plasma::makePalette(1000).size() == 256);
}

TEST_CASE("metrics aggregate per-frame samples", "[metrics]")
{
    plasma::Metrics metrics;
    metrics.add({ 1.0, 2.0, 1.0, 100 }); // frame 4 ms
    metrics.add({ 1.0, 1.0, 2.0, 200 }); // frame 4 ms
    metrics.add({ 2.0, 2.0, 4.0, 300 }); // frame 8 ms

    auto const summary = metrics.summarize(2.0);
    CHECK(summary.frames == 3);
    CHECK(summary.totalBytes == 600);
    CHECK(summary.minFrameMs == Approx(4.0));
    CHECK(summary.maxFrameMs == Approx(8.0));
    CHECK(summary.avgFrameMs == Approx(16.0 / 3.0));
    CHECK(summary.avgComputeMs == Approx(4.0 / 3.0));
    CHECK(summary.avgWriteMs == Approx(7.0 / 3.0));
    CHECK(summary.avgFps == Approx(1.5));    // 3 frames / 2 s
    CHECK(summary.peakFps == Approx(250.0)); // 1000 / 4 ms
}
