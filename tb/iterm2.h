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
#include <tb/png.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace iterm2
{

/// The iTerm2 inline-image protocol (OSC 1337 "File=inline", a base64-encoded PNG).
///
/// The frame is expanded to RGB, encoded as a PNG (via the self-contained png encoder), then
/// base64-encoded into a single OSC 1337 sequence. The program homes the cursor each frame so
/// successive images overwrite in place.
///
/// The compression level reaches the PNG encoder through the deflator this owns; at
/// zlib::NoCompression — the default — the PNG payload is stored rather than compressed, which is
/// what this protocol has always transmitted.
class Iterm2Protocol final: public img::ImageProtocol
{
  public:
    /// @param compressionLevel zlib deflate level for the PNG payload.
    explicit Iterm2Protocol(int compressionLevel = zlib::NoCompression): _deflator { compressionLevel } {}

    [[nodiscard]] std::string_view name() const noexcept override { return "iterm2"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        img::expandToRgb(_rgb, frame);

        _png.clear();
        png::encode(_png, _rgb, frame.width, frame.height, _deflator);

        _b64.clear();
        base64::encode(_b64, { reinterpret_cast<std::uint8_t const*>(_png.data()), _png.size() });

        out += "\033]1337;File=inline=1;width=";
        img::appendDecimal(out, frame.width);
        out += "px;height=";
        img::appendDecimal(out, frame.height);
        out += "px;preserveAspectRatio=0:";
        out += _b64;
        out.push_back('\007'); // BEL terminates the OSC
    }

  private:
    zlib::Deflator _deflator;       ///< Deflates the PNG payload; carries the level.
    std::vector<std::uint8_t> _rgb; ///< Reused RGB expansion buffer.
    std::string _png;               ///< Reused PNG buffer.
    std::string _b64;               ///< Reused base64 buffer.
};

} // namespace iterm2
