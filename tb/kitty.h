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
#include <tb/image_protocol.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kitty
{

/// The Kitty graphics protocol (APC "_G", 24-bit RGB transmitted as base64).
///
/// Each frame is transmitted+displayed into whichever of two image ids is *not* currently on
/// screen, and only then is the previously displayed id deleted. The old frame therefore stays
/// visible for the whole (multi-megabyte) upload, so the screen is never blank.
///
/// Deleting first — or re-transmitting a single fixed id, which the spec defines as deleting the
/// existing image and its placements before the new data displays — would blank the screen for the
/// duration of the upload, which the terminal's ~60Hz renderer shows as flicker.
///
/// The payload is base64-chunked into <=4096-byte pieces (the protocol's per-escape payload limit).
///
/// At a non-zero compression level the pixel data is deflated before being base64-encoded, which
/// the protocol's o=z key announces. That trades client CPU for wire bytes and measures the
/// terminal's inflate path rather than its raw parse path, so its throughput figures are not
/// comparable with the uncompressed ones.
class KittyProtocol final: public img::ImageProtocol
{
  public:
    /// @param compressionLevel zlib deflate level; zlib::NoCompression transmits raw RGB.
    explicit KittyProtocol(int compressionLevel = zlib::NoCompression)
    {
        // Engaged only when compressing, so the uncompressed path pays none of zlib's stream
        // setup or its internal allocations.
        if (compressionLevel != zlib::NoCompression)
            _deflator.emplace(compressionLevel);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "kitty"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        buildPayload(frame);

        // Upload into whichever id is not on screen, so the live frame survives the upload.
        auto const target = _live == 1 ? 2u : 1u;

        constexpr std::size_t ChunkSize = 4096;
        auto const total = _b64.size();
        auto const chunkCount = total == 0 ? std::size_t { 1 } : (total + ChunkSize - 1) / ChunkSize;
        for (auto const chunk: std::views::iota(std::size_t { 0 }, chunkCount))
        {
            auto const start = chunk * ChunkSize;
            auto const len = std::min(ChunkSize, total - start);
            auto const more = (chunk + 1 < chunkCount);

            out += "\033_G";
            if (chunk == 0)
            {
                // a=T: transmit & display; q=2: stay silent; C=1: leave the cursor put, so the
                // placement cannot scroll the screen; f=24: RGB; i/p: image & placement id;
                // s/v: pixel dimensions.
                out += "a=T,q=2,C=1,f=24,i=";
                img::appendDecimal(out, target);
                out += ",p=1,s=";
                img::appendDecimal(out, frame.width);
                out += ",v=";
                img::appendDecimal(out, frame.height);
                // o=z: the payload is deflated. Only f=100 (PNG) additionally needs the data size
                // stated; for f=24 the s/v dimensions already imply the decompressed length.
                if (_deflator)
                    out += ",o=z";
                // State the cell footprint when known, so the frame's size is ours rather than the
                // terminal's arithmetic (0 means: let the terminal decide).
                if (frame.columns != 0 && frame.rows != 0)
                {
                    out += ",c=";
                    img::appendDecimal(out, frame.columns);
                    out += ",r=";
                    img::appendDecimal(out, frame.rows);
                }
                if (more)
                    out += ",m=1";
            }
            else
            {
                // Continuation chunks need no keys beyond m (and q, to stay silent).
                out += "q=2,m=";
                out.push_back(more ? '1' : '0');
            }
            out.push_back(';');
            out.append(_b64, start, len);
            out += "\033\\";
        }

        // Only now that the new frame is placed, drop the old one. d=I (uppercase) also frees the
        // stored image data, which d=i would retain across every frame.
        if (_live != 0)
        {
            out += "\033_Ga=d,d=I,i=";
            img::appendDecimal(out, _live);
            out += ",q=2\033\\";
        }

        _live = target;
    }

  private:
    /// Builds the frame's base64 payload into _b64, compressing it first when asked to.
    /// @param frame The frame to encode.
    void buildPayload(img::Image const& frame)
    {
        if (!_deflator)
        {
            // Uncompressed: go straight from palette indices to base64, skipping the intermediate
            // RGB buffer entirely. Byte-for-byte the same payload as expanding then encoding.
            img::expandToBase64(_b64, frame);
            return;
        }

        img::expandToRgb(_rgb, frame);
        if (auto const result = _deflator->compress(_deflated, _rgb); !result)
            // The level is validated at the CLI boundary and deflateBound sized the output, so a
            // failure here is not a recoverable condition but a broken invariant (or OOM). The
            // pure layer reports it as a value; encode() cannot, so it escalates.
            throw std::runtime_error { "kitty: deflating the frame failed" };

        _b64.clear();
        base64::encode(_b64, _deflated);
    }

    unsigned _live { 0 };                    ///< Image id currently placed; 0 = nothing placed yet.
    std::optional<zlib::Deflator> _deflator; ///< Engaged only when compressing (o=z).
    std::vector<std::uint8_t> _rgb;          ///< Reused RGB expansion buffer (compressed path).
    std::vector<std::uint8_t> _deflated;     ///< Reused deflate output buffer (compressed path).
    std::string _b64;                        ///< Reused base64 buffer.
};

} // namespace kitty
