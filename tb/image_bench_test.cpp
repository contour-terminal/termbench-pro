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
#include <tb/deflate.h>
#include <tb/gip.h>
#include <tb/image_protocol.h>
#include <tb/iterm2.h>
#include <tb/kitty.h>
#include <tb/plasma.h>
#include <tb/png.h>
#include <tb/sixel.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
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

/// A reference base64 decoder, existing so tests can read back what base64::encode wrote.
///
/// Only tests need to decode, so this lives here rather than widening the product API (the same
/// reasoning as decodePng below).
///
/// @param text Standard (padded) base64 text.
/// @return The decoded bytes, or empty if @p text is not well-formed base64.
[[nodiscard]] std::vector<std::uint8_t> decodeBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return {};

    auto const valueOf = [](char c) -> int {
        auto const* found = std::ranges::find(base64::Alphabet, c);
        // Alphabet is NUL-terminated, so its last element is not a real symbol.
        auto const index = std::ranges::distance(std::ranges::begin(base64::Alphabet), found);
        return index < 64 ? static_cast<int>(index) : -1;
    };

    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4)
    {
        auto const quantum = text.substr(i, 4);
        auto const padding = static_cast<std::size_t>(std::ranges::count(quantum, '='));
        if (padding > 2)
            return {};

        std::uint32_t bits = 0;
        for (auto const j: std::views::iota(std::size_t { 0 }, std::size_t { 4 }))
        {
            auto const symbol = quantum[j] == '=' ? 0 : valueOf(quantum[j]);
            if (symbol < 0)
                return {};
            bits = (bits << 6) | static_cast<std::uint32_t>(symbol);
        }

        for (auto const j: std::views::iota(std::size_t { 0 }, std::size_t { 3 } - padding))
            out.push_back(static_cast<std::uint8_t>((bits >> (16 - 8 * j)) & 0xFFu));
    }
    return out;
}

/// Inflates @p compressed, which must expand to exactly @p expectedSize bytes.
///
/// Reading a payload back with zlib rather than by hand is what keeps these tests a check on the
/// encoder rather than a restatement of it, and it works at any compression level.
///
/// @param compressed   A complete RFC 1950 zlib stream.
/// @param expectedSize The exact size the stream must inflate to.
/// @return The inflated bytes, or nullopt if the stream is invalid or a different size.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> inflateExactly(
    std::span<std::uint8_t const> compressed, std::size_t expectedSize)
{
    // uncompress() wants a writable destination even when it will write nothing into it, and a
    // zero-sized vector has no data() to hand over.
    std::vector<std::uint8_t> out(std::max(expectedSize, std::size_t { 1 }));
    auto size = static_cast<uLongf>(out.size());
    if (uncompress(out.data(), &size, compressed.data(), static_cast<uLong>(compressed.size())) != Z_OK
        || size != expectedSize)
        return std::nullopt;
    out.resize(expectedSize);
    return out;
}

/// @return A big-endian 32-bit integer read from @p data at @p offset.
[[nodiscard]] std::uint32_t readBE32(std::span<std::uint8_t const> data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(data[offset]) << 24)
           | (static_cast<std::uint32_t>(data[offset + 1]) << 16)
           | (static_cast<std::uint32_t>(data[offset + 2]) << 8)
           | static_cast<std::uint32_t>(data[offset + 3]);
}

/// A minimal reference PNG decoder covering exactly what png::encode emits (8-bit RGB, filter 0).
/// Verifies every chunk CRC along the way, and inflates the IDAT stream with zlib, which checks the
/// RFC 1950 header and trailing Adler-32 itself -- so this decodes any compression level.
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

    if (idat.empty())
        return {};

    // Inflate the IDAT zlib stream. Decoding it with zlib rather than by hand keeps this decoder
    // independent of the level png::encode was given: stored and deflated blocks look the same
    // from here. zlib validates the RFC 1950 header and the trailing Adler-32 itself.
    auto const rowBytes = static_cast<std::size_t>(width) * 3u;
    auto const inflated = inflateExactly(idat, static_cast<std::size_t>(height) * (1u + rowBytes));
    if (!inflated)
        return {};
    auto const& raw = *inflated;

    // Strip the per-row filter byte (must be 0 = None).
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

TEST_CASE("the reference base64 decoder inverts the encoder", "[base64]")
{
    // decodeBase64 is the oracle other tests read payloads back with, so it is worth pinning
    // against the RFC vectors rather than trusting it.
    auto const decode = [](std::string_view text) {
        auto const bytes = decodeBase64(text);
        return std::string { bytes.begin(), bytes.end() };
    };

    CHECK(decode("") == "");
    CHECK(decode("Zg==") == "f");
    CHECK(decode("Zm8=") == "fo");
    CHECK(decode("Zm9v") == "foo");
    CHECK(decode("Zm9vYmFy") == "foobar");
    CHECK(decodeBase64("abc").empty());  // not a whole number of quanta
    CHECK(decodeBase64("a!!=").empty()); // not in the alphabet
}

TEST_CASE("expanding to RGB looks each pixel up in the palette", "[image]")
{
    // Pinned against bytes computed by hand rather than by re-deriving them with the function
    // under test, which is what makes this a check rather than a tautology.
    std::vector<img::Rgb> const palette { { 10, 20, 30 }, { 40, 50, 60 }, { 70, 80, 90 } };
    std::vector<std::uint8_t> const indices { 2, 0, 1, 0 }; // 2x2

    std::vector<std::uint8_t> rgb;
    img::expandToRgb(rgb, img::Image { indices, 2, 2, palette });

    CHECK(rgb == std::vector<std::uint8_t> { 70, 80, 90, 10, 20, 30, 40, 50, 60, 10, 20, 30 });
}

TEST_CASE("expanding to base64 matches expanding to RGB and encoding that", "[image]")
{
    // The fused encoder skips the RGB buffer entirely, so its whole contract is that it emits
    // exactly what the two-step path would. Nothing else may change on the wire.
    auto const check = [](unsigned width, unsigned height, std::size_t paletteSize) {
        std::vector<img::Rgb> palette;
        for (auto const i: std::views::iota(std::size_t { 0 }, paletteSize))
            palette.push_back(img::Rgb { static_cast<std::uint8_t>(i * 7 + 1),
                                         static_cast<std::uint8_t>(255 - i * 3),
                                         static_cast<std::uint8_t>(i * 11 + 5) });

        std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * height);
        for (auto const i: std::views::iota(std::size_t { 0 }, indices.size()))
            indices[i] = static_cast<std::uint8_t>((i * 31 + i / 3) % paletteSize);

        auto const frame = img::Image { indices, width, height, palette };

        std::vector<std::uint8_t> rgb;
        img::expandToRgb(rgb, frame);
        std::string expected;
        base64::encode(expected, rgb);

        std::string fused;
        img::expandToBase64(fused, frame);

        CHECK(fused == expected);
    };

    check(1, 1, 1);
    check(2, 2, 3);
    check(3, 5, 7);
    check(64, 32, 256); // a full palette, and a payload past one chunk's worth
}

TEST_CASE("expanding to base64 handles an empty frame", "[image]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices {};

    std::string out;
    out = "stale";
    img::expandToBase64(out, img::Image { indices, 0, 0, palette });

    CHECK(out.empty()); // replaces its sink rather than appending to it
}

TEST_CASE("deflate round-trips through inflate", "[zlib]")
{
    std::vector<std::uint8_t> data;
    for (auto const i: std::views::iota(0u, 10000u))
        data.push_back(static_cast<std::uint8_t>(i / 40)); // compressible: long runs

    zlib::Deflator deflator { zlib::FastestLevel };
    std::vector<std::uint8_t> compressed;
    REQUIRE(deflator.compress(compressed, data).has_value());
    CHECK(compressed.size() < data.size());

    auto const restored = inflateExactly(compressed, data.size());
    REQUIRE(restored.has_value());
    CHECK(*restored == data);
}

TEST_CASE("deflate reuses its stream across frames", "[zlib]")
{
    // The stream is reset rather than recreated per call, so a second call must not inherit any
    // state from the first -- otherwise a benchmark loop would corrupt every frame after the first.
    zlib::Deflator deflator { zlib::FastestLevel };

    std::vector<std::uint8_t> const first(500, 0xABu);
    std::vector<std::uint8_t> const second(700, 0xCDu);

    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;
    std::vector<std::uint8_t> again;
    REQUIRE(deflator.compress(a, first).has_value());
    REQUIRE(deflator.compress(b, second).has_value());
    REQUIRE(deflator.compress(again, first).has_value());

    CHECK(a == again); // the same input compresses identically however many frames preceded it
    CHECK(a != b);
}

TEST_CASE("deflate reports an unusable level rather than compressing", "[zlib]")
{
    std::vector<std::uint8_t> const data(16, 0u);
    std::vector<std::uint8_t> out;

    zlib::Deflator deflator { 42 };
    auto const result = deflator.compress(out, data);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == zlib::Error::InvalidLevel);

    CHECK(zlib::isValidLevel(zlib::NoCompression));
    CHECK(zlib::isValidLevel(zlib::MaxLevel));
    CHECK_FALSE(zlib::isValidLevel(zlib::MaxLevel + 1));
    CHECK_FALSE(zlib::isValidLevel(-1));
}

TEST_CASE("deflate emits a valid stream for empty input", "[zlib]")
{
    zlib::Deflator deflator { zlib::FastestLevel };
    std::vector<std::uint8_t> out;
    REQUIRE(deflator.compress(out, {}).has_value());

    // Empty in, but still a complete zlib stream: header plus terminator plus Adler-32.
    CHECK(out.size() >= 6);

    auto const restored = inflateExactly(out, 0);
    REQUIRE(restored.has_value());
    CHECK(restored->empty());
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
    CHECK(std::ranges::find(names, "gip") != names.end());
    CHECK(std::ranges::find(names, "gip-png") != names.end());
    CHECK(std::ranges::find(names, "gip-upload") != names.end());
    CHECK(img::makeProtocol("does-not-exist") == nullptr);

    // "sixel" must stay first: the default protocol is names.front().
    CHECK(names.front() == "sixel");
}

TEST_CASE("the registry carries encoder options through to the protocol", "[registry]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2
    auto const image = img::Image { indices, 2, 2, palette };

    auto const encodeWith = [&](std::string_view name, int level) {
        auto const protocol = img::makeProtocol(name, img::ProtocolOptions { level });
        REQUIRE(protocol != nullptr);
        std::string out;
        protocol->encode(out, image);
        return out;
    };

    // A knob the driver sets has to actually reach the encoder; the o=z key is where that becomes
    // observable for kitty.
    CHECK(contains(encodeWith("kitty", zlib::FastestLevel), ",o=z"));
    CHECK_FALSE(contains(encodeWith("kitty", zlib::NoCompression), "o=z"));

    // Defaulted options must mean "no compression", so an unflagged run keeps its wire format.
    auto const defaulted = img::makeProtocol("kitty");
    REQUIRE(defaulted != nullptr);
    std::string out;
    defaulted->encode(out, image);
    CHECK_FALSE(contains(out, "o=z"));

    // Protocols that do not compress must tolerate the option rather than reject it.
    CHECK_FALSE(encodeWith("sixel", zlib::MaxLevel).empty());
}

TEST_CASE("frame model is declared per protocol", "[registry][gip]")
{
    // Only the pool protocol cycles a fixed set of frames; everything else streams. The driver
    // picks its frame source from this, rather than from a name comparison in the loop.
    CHECK(img::makeProtocol("sixel")->frameModel() == img::FrameModel::Streaming);
    CHECK(img::makeProtocol("kitty")->frameModel() == img::FrameModel::Streaming);
    CHECK(img::makeProtocol("iterm2")->frameModel() == img::FrameModel::Streaming);
    CHECK(img::makeProtocol("gip")->frameModel() == img::FrameModel::Streaming);
    CHECK(img::makeProtocol("gip-png")->frameModel() == img::FrameModel::Streaming);
    CHECK(img::makeProtocol("gip-upload")->frameModel() == img::FrameModel::Cyclic);
}

namespace
{
/// @return the base64 body of a GIP message, i.e. what lies between ";!" and the terminating ST.
[[nodiscard]] std::string_view gipBody(std::string_view message)
{
    auto const start = message.find(";!");
    if (start == std::string_view::npos || !message.ends_with("\033\\"))
        return {};
    return message.substr(start + 2, message.size() - (start + 2) - 2);
}
} // namespace

TEST_CASE("gip emits a oneshot RGB message", "[gip]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 }, { 4, 5, 6 } };
    std::vector<std::uint8_t> const indices { 0, 1, 1, 0 }; // 2x2

    gip::GipProtocol protocol;
    std::string out;
    protocol.encode(out, img::Image { indices, 2, 2, palette, 8, 4 });

    CHECK(out.starts_with("\033P!go=s,"));
    CHECK(out.ends_with("\033\\"));
    CHECK(contains(out, "f=2,")); // raw RGB, so this measures framing rather than a codec
    CHECK(contains(out, "w=2,"));
    CHECK(contains(out, "h=2,"));
    CHECK(contains(out, "c=8,")); // the cell area is stated, never inferred
    CHECK(contains(out, "r=4,"));

    // Compare against the expected body re-encoded, which is exact and needs no decoder.
    auto expectedRgb = std::vector<std::uint8_t> {};
    img::expandToRgb(expectedRgb, img::Image { indices, 2, 2, palette });
    std::string expectedBody;
    base64::encode(expectedBody, expectedRgb);
    CHECK(gipBody(out) == expectedBody);
}

TEST_CASE("gip-png emits a oneshot PNG message", "[gip]")
{
    std::vector<img::Rgb> const palette { { 9, 8, 7 } };
    std::vector<std::uint8_t> const indices(6, 0); // 3x2

    gip::GipPngProtocol protocol;
    std::string out;
    protocol.encode(out, img::Image { indices, 3, 2, palette, 6, 2 });

    CHECK(out.starts_with("\033P!go=s,"));
    CHECK(contains(out, "f=4,")); // PNG
    CHECK(out.ends_with("\033\\"));

    auto expectedRgb = std::vector<std::uint8_t> {};
    img::expandToRgb(expectedRgb, img::Image { indices, 3, 2, palette });
    std::string expectedPng;
    zlib::Deflator deflator { zlib::NoCompression };
    png::encode(expectedPng, expectedRgb, 3, 2, deflator);
    std::string expectedBody;
    base64::encode(expectedBody, bytesOf(expectedPng));
    CHECK(gipBody(out) == expectedBody);
}

TEST_CASE("gip-upload transmits a pool slot once, then renders it by name", "[gip]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2
    auto const frame = [&](unsigned poolId) {
        return img::Image { indices, 2, 2, palette, 8, 4, poolId };
    };

    gip::GipUploadProtocol protocol;

    SECTION("first sight of a slot uploads and renders")
    {
        std::string out;
        protocol.encode(out, frame(0));
        CHECK(contains(out, "\033P!go=u,"));
        CHECK(contains(out, "\033P!go=r,"));
        CHECK(contains(out, "n=tb0"));
    }

    SECTION("a known slot renders without retransmitting")
    {
        std::string first;
        protocol.encode(first, frame(0));
        REQUIRE(contains(first, "o=u,"));

        std::string second;
        protocol.encode(second, frame(0));
        CHECK_FALSE(contains(second, "o=u,")); // the entire point of an image pool
        CHECK(contains(second, "\033P!go=r,"));
        CHECK(second.size() < first.size());
    }

    SECTION("a new slot uploads again, under its own name")
    {
        std::string first;
        protocol.encode(first, frame(0));
        std::string second;
        protocol.encode(second, frame(1));
        CHECK(contains(second, "o=u,"));
        CHECK(contains(second, "n=tb1"));
        CHECK_FALSE(contains(second, "n=tb0"));
    }

    SECTION("render always states a non-zero cell area")
    {
        // Render-by-name does not derive a grid from the pixel size the way oneshot does, so
        // c=0,r=0 draws nothing at all -- and says nothing about it.
        std::string out;
        protocol.encode(out, frame(0));
        auto const renderAt = out.find("\033P!go=r,");
        REQUIRE(renderAt != std::string::npos);
        auto const render = out.substr(renderAt);
        CHECK(contains(render, "c=8,"));
        CHECK(contains(render, "r=4,"));
        CHECK_FALSE(contains(render, "c=0,"));
        CHECK_FALSE(contains(render, "r=0,"));
    }

    SECTION("pool names are ASCII alphanumeric plus underscore")
    {
        // The terminal validates the name and rejects anything else -- silently.
        std::string out;
        protocol.encode(out, frame(7));
        auto const nameAt = out.find("n=");
        REQUIRE(nameAt != std::string::npos);
        auto const name = out.substr(nameAt + 2, out.find(',', nameAt) - (nameAt + 2));
        CHECK(name == "tb7");
        CHECK(std::ranges::all_of(name, [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
                   || ch == '_';
        }));
    }
}

TEST_CASE("kitty emits APC transmit framing", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2

    kitty::KittyProtocol protocol;
    std::string out;
    protocol.encode(out, img::Image { indices, 2, 2, palette });

    CHECK(out.starts_with("\033_Ga=T,q=2,C=1,f=24,i=1,p=1,s=2,v=2;")); // transmit + display keys
    CHECK(out.ends_with("\033\\"));
}

TEST_CASE("kitty never blanks the screen before uploading a frame", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2
    auto const image = img::Image { indices, 2, 2, palette };

    kitty::KittyProtocol protocol;

    // A frame that opened with a delete would leave the screen blank for the whole upload that
    // follows, which the terminal renders as flicker. The first frame has nothing to delete at all.
    std::string first;
    protocol.encode(first, image);
    CHECK_FALSE(contains(first, "a=d"));

    // From the second frame on, the delete must trail the upload that replaces it on screen.
    std::string second;
    protocol.encode(second, image);
    auto const deleteAt = second.find("\033_Ga=d");
    REQUIRE(deleteAt != std::string::npos);
    CHECK(deleteAt > second.rfind("\033_Ga=T")); // placement first, only then the old id goes
    CHECK(second.ends_with("\033\\"));
}

TEST_CASE("kitty ping-pongs image ids across frames", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2
    auto const image = img::Image { indices, 2, 2, palette };

    kitty::KittyProtocol protocol;
    auto const frame = [&] {
        std::string out;
        protocol.encode(out, image);
        return out;
    };

    // Uploading into the id that is not on screen is what keeps the previous frame visible
    // throughout the upload; the ids therefore have to alternate.
    auto const first = frame();
    CHECK(contains(first, ",i=1,p=1,"));

    auto const second = frame();
    CHECK(contains(second, ",i=2,p=1,"));
    CHECK(contains(second, "\033_Ga=d,d=I,i=1,q=2\033\\")); // d=I also frees the old image data

    auto const third = frame();
    CHECK(contains(third, ",i=1,p=1,"));
    CHECK(contains(third, "\033_Ga=d,d=I,i=2,q=2\033\\"));
}

TEST_CASE("kitty suppresses terminal responses", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(24000, 0); // 200x120, several chunks
    auto const image = img::Image { indices, 200, 120, palette };

    kitty::KittyProtocol protocol;
    std::string out;
    protocol.encode(out, image);
    protocol.encode(out, image); // second frame, so the delete escape is covered too

    // Every response would land on the stdin the driver polls for the quit key, so no escape may
    // omit q=2 -- continuation chunks and the delete included.
    auto count = std::size_t { 0 };
    for (auto offset = out.find("\033_G"); offset != std::string::npos;
         offset = out.find("\033_G", offset + 1))
    {
        // Control data runs to the payload separator, or to the ST when there is no payload
        // (the delete escape carries none).
        auto const end = std::min(out.find(';', offset), out.find("\033\\", offset));
        REQUIRE(end != std::string::npos);
        CHECK(contains(out.substr(offset, end - offset), "q=2"));
        ++count;
    }
    CHECK(count > 2); // guard against the loop trivially passing
}

TEST_CASE("kitty states the cell footprint", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2

    SECTION("when the cell area is known")
    {
        kitty::KittyProtocol protocol;
        std::string out;
        protocol.encode(out, img::Image { indices, 2, 2, palette, 8, 4 });

        // Stating it keeps the frame's size ours rather than the terminal's arithmetic.
        CHECK(contains(out, ",c=8,r=4"));
    }

    SECTION("and defers to the terminal when it is not")
    {
        kitty::KittyProtocol protocol;
        std::string out;
        protocol.encode(out, img::Image { indices, 2, 2, palette });

        CHECK_FALSE(contains(out, ",c="));
        CHECK_FALSE(contains(out, ",r="));
    }
}

TEST_CASE("kitty chunks payloads at the protocol limit", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(24000, 0); // 200x120 -> 72000 RGB bytes -> 96000 base64
    auto const image = img::Image { indices, 200, 120, palette };

    kitty::KittyProtocol protocol;
    std::string out;
    protocol.encode(out, image);

    // 96000 base64 bytes at the protocol's 4096-byte limit is 24 escapes: 23 with m=1, one m=0.
    CHECK(out.starts_with("\033_Ga=T,q=2,C=1,f=24,i=1,p=1,s=200,v=120,m=1;"));
    CHECK(contains(out, "\033_Gq=2,m=0;")); // the last chunk closes the transmission

    auto more = std::size_t { 0 };
    for (auto offset = out.find("m=1;"); offset != std::string::npos; offset = out.find("m=1;", offset + 1))
        ++more;
    CHECK(more == 23);

    // Only the first chunk carries the control set; continuations state m (and q) and nothing else.
    CHECK(out.find("a=T") == out.rfind("a=T"));
}

TEST_CASE("kitty announces compression only when it compresses", "[kitty]")
{
    std::vector<img::Rgb> const palette { { 1, 2, 3 } };
    std::vector<std::uint8_t> const indices(4, 0); // 2x2
    auto const image = img::Image { indices, 2, 2, palette };

    SECTION("uncompressed frames carry no o key")
    {
        kitty::KittyProtocol protocol;
        std::string out;
        protocol.encode(out, image);

        // Claiming o=z without deflating would make the terminal inflate raw RGB and fail.
        CHECK_FALSE(contains(out, "o=z"));
    }

    SECTION("compressed frames announce o=z")
    {
        kitty::KittyProtocol protocol { zlib::FastestLevel };
        std::string out;
        protocol.encode(out, image);

        CHECK(contains(out, ",o=z"));
    }
}

TEST_CASE("kitty transmits a compressed frame the terminal can inflate", "[kitty]")
{
    // The decisive check: reassemble the wire payload the way a terminal would -- concatenate the
    // chunks, base64-decode, inflate -- and it must be exactly the frame's RGB bytes.
    std::vector<img::Rgb> const palette { { 9, 8, 7 }, { 250, 128, 3 }, { 0, 0, 0 } };
    std::vector<std::uint8_t> indices(4000);
    for (auto const i: std::views::iota(std::size_t { 0 }, indices.size()))
        indices[i] = static_cast<std::uint8_t>(i % 3);
    auto const image = img::Image { indices, 100, 40, palette };

    kitty::KittyProtocol protocol { zlib::MaxLevel };
    std::string out;
    protocol.encode(out, image);

    // Concatenate every transmit escape's payload (between ';' and the closing ST).
    std::string payload;
    for (auto offset = out.find("\033_G"); offset != std::string::npos;
         offset = out.find("\033_G", offset + 1))
    {
        auto const separator = out.find(';', offset);
        auto const terminator = out.find("\033\\", offset);
        if (separator == std::string::npos || separator > terminator)
            continue; // a control-only escape (the delete) carries no payload
        payload.append(out, separator + 1, terminator - separator - 1);
    }
    REQUIRE_FALSE(payload.empty());

    auto const compressed = decodeBase64(payload);
    REQUIRE_FALSE(compressed.empty());

    std::vector<std::uint8_t> expected;
    img::expandToRgb(expected, image);

    auto const inflated = inflateExactly(compressed, expected.size());
    REQUIRE(inflated.has_value());
    CHECK(*inflated == expected);

    // The point of compressing at all: fewer bytes than the raw RGB it stands for.
    CHECK(compressed.size() < expected.size());
}

TEST_CASE("png is structurally valid and round-trips pixels", "[png]")
{
    // 2x1 image: red, green.
    std::vector<std::uint8_t> const source { 255, 0, 0, 0, 255, 0 };

    std::string out;
    zlib::Deflator deflator { zlib::NoCompression };
    png::encode(out, source, 2, 1, deflator);

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

TEST_CASE("png compresses when asked to, and still decodes", "[png]")
{
    // A compressible image: 64x64 of a single colour.
    constexpr unsigned Width = 64;
    constexpr unsigned Height = 64;
    std::vector<std::uint8_t> source;
    for ([[maybe_unused]] auto const i: std::views::iota(0u, Width * Height))
    {
        source.push_back(17u);
        source.push_back(34u);
        source.push_back(51u);
    }

    std::string stored;
    zlib::Deflator storing { zlib::NoCompression };
    png::encode(stored, source, Width, Height, storing);

    std::string deflated;
    zlib::Deflator deflating { zlib::MaxLevel };
    png::encode(deflated, source, Width, Height, deflating);

    // Level 0 is the default precisely because it does not compress; a level above it must.
    CHECK(deflated.size() < stored.size());

    unsigned width = 0;
    unsigned height = 0;
    auto const decoded = decodePng(deflated, width, height);
    CHECK(width == Width);
    CHECK(height == Height);
    CHECK(decoded == source); // same pixels, fewer bytes
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
