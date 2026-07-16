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
#include <tb/png.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gip
{

/// The Good Image Protocol: a DCS-framed, header/body message protocol.
///
/// Wire shape is `ESC P ! g <key>=<value>,... ; <body> ESC \`, where a leading `!` on the body
/// marks it base64. Operations select on the `o` header: `s` oneshot, `u` upload, `r` render by
/// name, `d` release, `q` query.
///
/// Note the terminal caps the message body (16 MB in Contour) and silently drops the excess, so a
/// caller must bound the frame size itself rather than discover the truncation as corruption.
namespace detail
{
    /// Format selectors for the `f` header.
    constexpr auto FormatRgb = 2;
    constexpr auto FormatPng = 4;

    /// Layer selector for the `L` header: replace whatever is underneath.
    constexpr auto LayerReplace = 1;

    /// Alignment selector for the `a` header: top-left.
    constexpr auto AlignTopStart = 1;

    /// Resize selector for the `z` header: don't resize.
    constexpr auto ResizeNone = 0;

    /// Appends `<key>=<value>` followed by a comma.
    inline void appendHeader(std::string& out, std::string_view key, unsigned long long value)
    {
        out += key;
        out.push_back('=');
        img::appendDecimal(out, value);
        out.push_back(',');
    }

    /// Appends the pixel geometry and the cell area a frame should occupy.
    ///
    /// The cell area is always stated. Render-by-name in particular *requires* it: the terminal
    /// only derives a grid from the pixel size on the oneshot path, so `c=0,r=0` renders nothing at
    /// all -- silently.
    inline void appendGeometry(std::string& out, img::Image const& frame)
    {
        appendHeader(out, "w", frame.width);
        appendHeader(out, "h", frame.height);
        appendHeader(out, "c", frame.columns);
        appendHeader(out, "r", frame.rows);
    }

    /// Appends the name of the pool slot @p poolId occupies.
    /// GIP names are ASCII alphanumeric plus underscore only, so no separator is used.
    inline void appendPoolName(std::string& out, unsigned poolId)
    {
        out += "tb";
        img::appendDecimal(out, poolId);
    }
} // namespace detail

/// GIP transmitting raw RGB (`f=2`) in a oneshot message.
///
/// The fair analogue of Kitty's `f=24`: it isolates the protocol's framing and transport from any
/// image codec, which is what makes it comparable against Sixel.
class GipProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "gip"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        img::expandToRgb(_rgb, frame);

        out += "\033P!go=s,";
        detail::appendHeader(out, "f", detail::FormatRgb);
        detail::appendGeometry(out, frame);
        detail::appendHeader(out, "a", detail::AlignTopStart);
        detail::appendHeader(out, "z", detail::ResizeNone);
        out += "L=";
        img::appendDecimal(out, detail::LayerReplace);
        // No `u`/`l` flags: the loop homes the cursor itself, and autoScroll would scroll the grid
        // out from under the measurement.
        out += ";!";
        base64::encode(out, _rgb);
        out += "\033\\";
    }

  private:
    std::vector<std::uint8_t> _rgb; ///< Reused RGB expansion buffer.
};

/// GIP transmitting PNG (`f=4`) in a oneshot message.
///
/// The other side of the wire-size/CPU trade: in principle fewer bytes for the cost of encoding
/// every frame. In practice this encoder emits *stored* DEFLATE blocks, so it compresses nothing:
/// measured against `gip` on a plasma frame it produces the same 1.83 MB/frame for ~2.6x the encode
/// time. That is a real answer about this codec rather than a reason to omit the row -- and it is
/// why the trade is worth measuring rather than assuming.
class GipPngProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "gip-png"; }

    void encode(std::string& out, img::Image const& frame) override
    {
        img::expandToRgb(_rgb, frame);
        _png.clear();
        png::encode(_png, _rgb, frame.width, frame.height);

        out += "\033P!go=s,";
        detail::appendHeader(out, "f", detail::FormatPng);
        detail::appendGeometry(out, frame);
        detail::appendHeader(out, "a", detail::AlignTopStart);
        detail::appendHeader(out, "z", detail::ResizeNone);
        out += "L=";
        img::appendDecimal(out, detail::LayerReplace);
        out += ";!";
        base64::encode(
            out,
            std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const*>(_png.data()), _png.size()));
        out += "\033\\";
    }

  private:
    std::vector<std::uint8_t> _rgb; ///< Reused RGB expansion buffer.
    std::string _png;               ///< Reused PNG buffer.
};

/// GIP uploading a pool of named images once, then rendering them by name.
///
/// This is the protocol's headline capability and nothing else in the benchmark can express it: an
/// image already known to the terminal costs one short render message, not a retransmission.
///
/// Its FrameModel is Cyclic, so the driver feeds it a fixed pool of pre-rendered frames rather than
/// a fresh one each tick. That keeps the animation genuinely moving -- so frame rate still means
/// something -- while steady state reduces to the render message alone.
class GipUploadProtocol final: public img::ImageProtocol
{
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "gip-upload"; }

    [[nodiscard]] img::FrameModel frameModel() const noexcept override { return img::FrameModel::Cyclic; }

    void encode(std::string& out, img::Image const& frame) override
    {
        // Without a pool id there is nothing to name, so behave as a oneshot would.
        auto const poolId = frame.poolId.value_or(0u);

        if (!_uploaded.contains(poolId))
        {
            _uploaded.insert(poolId);
            img::expandToRgb(_rgb, frame);

            out += "\033P!go=u,";
            out += "n=";
            detail::appendPoolName(out, poolId);
            out.push_back(',');
            detail::appendHeader(out, "f", detail::FormatRgb);
            detail::appendHeader(out, "w", frame.width);
            out += "h=";
            img::appendDecimal(out, frame.height);
            out += ";!";
            base64::encode(out, _rgb);
            out += "\033\\";
        }

        out += "\033P!go=r,";
        out += "n=";
        detail::appendPoolName(out, poolId);
        out.push_back(',');
        detail::appendHeader(out, "c", frame.columns);
        detail::appendHeader(out, "r", frame.rows);
        detail::appendHeader(out, "a", detail::AlignTopStart);
        detail::appendHeader(out, "z", detail::ResizeNone);
        out += "L=";
        img::appendDecimal(out, detail::LayerReplace);
        out += "\033\\";
    }

  private:
    std::vector<std::uint8_t> _rgb;         ///< Reused RGB expansion buffer.
    std::unordered_set<unsigned> _uploaded; ///< Pool slots already transmitted.
};

} // namespace gip
