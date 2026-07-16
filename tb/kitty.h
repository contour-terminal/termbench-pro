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
/// Each frame deletes the previously transmitted image, then transmits+displays the new one,
/// base64-chunked into <=4096-byte pieces (the protocol's per-escape payload limit).
class KittyProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "kitty"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        img::expandToRgb(_rgb, frame);

        _b64.clear();
        base64::encode(_b64, _rgb);

        // Replace the previous frame rather than stacking images.
        out += "\033_Ga=d\033\\";

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
                // a=T: transmit & display; f=24: RGB; s/v: pixel dimensions.
                out += "a=T,f=24,s=";
                img::appendDecimal(out, frame.width);
                out += ",v=";
                img::appendDecimal(out, frame.height);
                if (more)
                    out += ",m=1";
            }
            else
            {
                out += "m=";
                out.push_back(more ? '1' : '0');
            }
            out.push_back(';');
            out.append(_b64, start, len);
            out += "\033\\";
        }
    }

  private:
    std::vector<std::uint8_t> _rgb; ///< Reused RGB expansion buffer.
    std::string _b64;               ///< Reused base64 buffer.
};

} // namespace kitty
