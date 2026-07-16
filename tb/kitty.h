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
#include <tb/image_protocol.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
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
class KittyProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "kitty"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        img::expandToRgb(_rgb, frame);

        _b64.clear();
        base64::encode(_b64, _rgb);

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
    unsigned _live { 0 };           ///< Image id currently placed; 0 = nothing placed yet.
    std::vector<std::uint8_t> _rgb; ///< Reused RGB expansion buffer.
    std::string _b64;               ///< Reused base64 buffer.
};

} // namespace kitty
