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

#include <tb/image_protocol.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sixel
{

/// Reusable scratch buffers for encode(), so the hot path avoids per-frame allocation.
///
/// The encoder restores every entry it touches, so the buffers are left all-zero between calls;
/// a single Scratch can therefore be reused across frames of identical geometry with no
/// re-initialization.
struct Scratch
{
    std::vector<std::uint8_t> colBits; ///< [color*width + x] -> 6-bit column value for the current band.
    std::vector<std::uint8_t> seen;    ///< Per-color presence flag within the current band.
    std::vector<unsigned> present;     ///< Colors present in the current band (in encode order).
};

/// Encodes an indexed image as a Sixel DCS payload appended to @p out.
///
/// Emits `ESC P q "1;1;W;H  <palette registers>  <sixel bands>  ESC \`. Each 6-pixel-tall band
/// is written one color plane at a time (only the colors actually present in that band), and each
/// plane's columns are run-length encoded. Planes within a band are separated by `$` (graphical
/// carriage return); bands by `-` (graphical newline).
///
/// @param out     Sink the DCS payload is appended to.
/// @param indices Row-major palette indices, size == width*height.
/// @param width   Image width in pixels.
/// @param height  Image height in pixels (need not be a multiple of 6).
/// @param palette Palette entries; must cover every index used (size <= 256).
/// @param scratch Reusable scratch buffers (see Scratch); sized on demand, reused across frames.
inline void encode(std::string& out,
                   std::span<std::uint8_t const> indices,
                   unsigned width,
                   unsigned height,
                   std::span<img::Rgb const> palette,
                   Scratch& scratch)
{
    // DCS introducer + raster attributes ("Pan;Pad;Ph;Pv with 1:1 pixel aspect).
    out += "\033Pq\"1;1;";
    img::appendDecimal(out, width);
    out.push_back(';');
    img::appendDecimal(out, height);

    // Color registers: #i;2;R;G;B, where "2" selects RGB and channels are rescaled 0..255 -> 0..100.
    auto const paletteSize = palette.size();
    for (auto const i: std::views::iota(std::size_t { 0 }, paletteSize))
    {
        auto const color = palette[i];
        out.push_back('#');
        img::appendDecimal(out, i);
        out += ";2;";
        img::appendDecimal(out, (static_cast<unsigned>(color.r) * 100u + 127u) / 255u);
        out.push_back(';');
        img::appendDecimal(out, (static_cast<unsigned>(color.g) * 100u + 127u) / 255u);
        out.push_back(';');
        img::appendDecimal(out, (static_cast<unsigned>(color.b) * 100u + 127u) / 255u);
    }

    if (width == 0 || height == 0 || paletteSize == 0)
    {
        out += "\033\\";
        return;
    }

    // Scratch reused across bands (and, via `scratch`, across frames): colBits[color*width + x]
    // holds the 6-bit column value for `color` in the current band; only entries of colors listed
    // in `present` are meaningful. The buffers are all-zero on entry (each call restores what it
    // touches), so they are only (re-)zeroed when the geometry changes.
    auto& colBits = scratch.colBits;
    auto& seen = scratch.seen;
    auto& present = scratch.present;
    if (colBits.size() != paletteSize * width)
        colBits.assign(paletteSize * width, 0);
    if (seen.size() != paletteSize)
        seen.assign(paletteSize, 0);
    present.clear();
    present.reserve(paletteSize);

    auto const bandCount = (height + 5u) / 6u;
    for (auto const band: std::views::iota(0u, bandCount))
    {
        auto const bandTop = band * 6u;
        auto const rows = std::min(6u, height - bandTop);

        present.clear();
        for (auto const row: std::views::iota(0u, rows))
        {
            auto const bit = static_cast<std::uint8_t>(1u << row);
            auto const rowOffset = static_cast<std::size_t>(bandTop + row) * width;
            for (auto const x: std::views::iota(0u, width))
            {
                auto const color = indices[rowOffset + x];
                if (!seen[color])
                {
                    seen[color] = 1;
                    present.push_back(color);
                }
                auto const cell = static_cast<std::size_t>(color) * width + x;
                colBits[cell] = static_cast<std::uint8_t>(colBits[cell] | bit);
            }
        }

        // Deterministic plane order keeps output stable (and testable).
        std::ranges::sort(present);

        auto firstPlane = true;
        for (auto const color: present)
        {
            if (!firstPlane)
                out.push_back('$'); // graphical CR: overlay the next plane on the same band
            firstPlane = false;

            out.push_back('#');
            img::appendDecimal(out, color);

            auto const base = static_cast<std::size_t>(color) * width;
            unsigned x = 0;
            while (x < width)
            {
                auto const value = colBits[base + x];
                unsigned run = 1;
                while (x + run < width && colBits[base + x + run] == value)
                    ++run;

                auto const glyph = static_cast<char>(value + 63u);
                if (run >= 4) // "!<n><glyph>" only pays off from 4 repeats up
                {
                    out.push_back('!');
                    img::appendDecimal(out, run);
                    out.push_back(glyph);
                }
                else
                    out.append(run, glyph);
                x += run;
            }

            // Reset this color's scratch row and presence for the next band.
            std::ranges::fill_n(
                colBits.begin() + static_cast<std::ptrdiff_t>(base), width, std::uint8_t { 0 });
            seen[color] = 0;
        }

        if (band + 1 != bandCount)
            out.push_back('-'); // graphical NL: advance to the next 6-pixel band
    }

    out += "\033\\"; // String Terminator (ST)
}

/// Convenience overload that allocates temporary scratch (for one-off / test use).
/// @see encode(std::string&, std::span, unsigned, unsigned, std::span, Scratch&)
inline void encode(std::string& out,
                   std::span<std::uint8_t const> indices,
                   unsigned width,
                   unsigned height,
                   std::span<img::Rgb const> palette)
{
    Scratch scratch;
    encode(out, indices, width, height, palette, scratch);
}

/// The Sixel (DCS) image protocol.
class SixelProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "sixel"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        sixel::encode(out, frame.indices, frame.width, frame.height, frame.palette, _scratch);
    }

  private:
    Scratch _scratch; ///< Reused across frames to keep the encode path allocation-free.
};

} // namespace sixel
