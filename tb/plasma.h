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
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <ranges>
#include <span>
#include <vector>

namespace plasma
{

namespace detail
{
    inline constexpr std::size_t SinBits = 12;
    inline constexpr std::size_t SinSize = std::size_t { 1 } << SinBits; ///< 4096-entry sine table.

    /// @return A shared, lazily-built table of sin() sampled uniformly over one period.
    [[nodiscard]] inline std::array<float, SinSize> const& sinLut()
    {
        static std::array<float, SinSize> const lut = [] {
            std::array<float, SinSize> table {};
            for (auto const i: std::views::iota(std::size_t { 0 }, SinSize))
            {
                auto const angle =
                    2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(SinSize);
                table[i] = static_cast<float>(std::sin(angle));
            }
            return table;
        }();
        return lut;
    }

    /// @return An approximate sin(@p radians) via lookup in @p lut (data-driven; keeps compute
    ///         cheap so the terminal — not the field generator — stays the benchmark's bottleneck).
    /// @param lut     A one-period sine table (see sinLut()); passed in so callers bind it once.
    /// @param radians The angle to approximate.
    [[nodiscard]] inline float sampleSin(std::array<float, SinSize> const& lut, double radians) noexcept
    {
        constexpr double InvTwoPi = 1.0 / (2.0 * std::numbers::pi);
        auto const turns = radians * InvTwoPi;
        auto const frac = turns - std::floor(turns); // [0, 1)
        auto const index = static_cast<std::size_t>(frac * static_cast<double>(SinSize)) & (SinSize - 1);
        return lut[index];
    }
} // namespace detail

/// @return Payload throughput in MiB/s for @p bytes transferred in @p frameMs milliseconds
///         (0 when @p frameMs is not positive).
[[nodiscard]] constexpr double throughputMBps(std::size_t bytes, double frameMs) noexcept
{
    constexpr double MiB = 1024.0 * 1024.0;
    return frameMs > 0.0 ? (static_cast<double>(bytes) / MiB) / (frameMs / 1000.0) : 0.0;
}

/// Builds a smooth, cyclic RGB palette of @p size entries by sweeping hue at full saturation/value.
/// @param size Number of palette entries (clamped to 1..256).
/// @return The palette.
[[nodiscard]] inline std::vector<img::Rgb> makePalette(unsigned size = 256)
{
    size = std::clamp(size, 1u, 256u);

    auto const channel = [](double value) noexcept {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };

    std::vector<img::Rgb> palette;
    palette.reserve(size);
    for (auto const i: std::views::iota(0u, size))
    {
        auto const hue = static_cast<double>(i) / static_cast<double>(size) * 6.0; // sector in [0, 6)
        auto const sector = static_cast<int>(hue) % 6;
        auto const f = hue - std::floor(hue);

        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
        switch (sector)
        {
            case 0:
                r = 1.0;
                g = f;
                break;
            case 1:
                r = 1.0 - f;
                g = 1.0;
                break;
            case 2:
                g = 1.0;
                b = f;
                break;
            case 3:
                g = 1.0 - f;
                b = 1.0;
                break;
            case 4:
                r = f;
                b = 1.0;
                break;
            default:
                r = 1.0;
                b = 1.0 - f;
                break;
        }
        palette.push_back(img::Rgb { channel(r), channel(g), channel(b) });
    }
    return palette;
}

/// Fills @p indices with a plasma field evaluated at animation phase @p time.
///
/// Deterministic: identical arguments always produce identical output. Every written index is
/// guaranteed to be < @p paletteSize (and hence a valid palette lookup).
///
/// @param indices     Destination, row-major, size == width*height.
/// @param width       Field width in pixels.
/// @param height      Field height in pixels.
/// @param time        Animation phase (seconds); advancing it animates the field.
/// @param paletteSize Number of palette entries the indices must address (1..256).
inline void render(
    std::span<std::uint8_t> indices, unsigned width, unsigned height, double time, unsigned paletteSize)
{
    paletteSize = std::clamp(paletteSize, 1u, 256u);
    auto const scale = static_cast<double>(paletteSize);
    auto const& lut = detail::sinLut(); // bind once; avoids a magic-static guard per pixel

    for (auto const y: std::views::iota(0u, height))
    {
        auto const fy = static_cast<double>(y);
        auto const rowOffset = static_cast<std::size_t>(y) * width;
        auto const yTerm =
            static_cast<double>(detail::sampleSin(lut, fy * 0.04 + time * 1.1)); // row-invariant
        for (auto const x: std::views::iota(0u, width))
        {
            auto const fx = static_cast<double>(x);
            auto const radius = std::sqrt(fx * fx + fy * fy);

            auto const value = static_cast<double>(detail::sampleSin(lut, fx * 0.03 + time)) + yTerm
                               + static_cast<double>(detail::sampleSin(lut, (fx + fy) * 0.02 + time * 0.7))
                               + static_cast<double>(detail::sampleSin(lut, radius * 0.02 - time * 1.3));

            auto const normalized = (value + 4.0) / 8.0; // map [-4, 4] -> [0, 1]
            auto index = static_cast<unsigned>(normalized * scale);
            if (index >= paletteSize)
                index = paletteSize - 1;
            indices[rowOffset + x] = static_cast<std::uint8_t>(index);
        }
    }
}

/// One rendered frame's timing and size sample.
struct Sample
{
    double computeMs {};  ///< Time spent generating the plasma field.
    double encodeMs {};   ///< Time spent encoding the image protocol payload.
    double writeMs {};    ///< Time spent writing the payload to the terminal.
    std::size_t bytes {}; ///< Encoded payload size in bytes.

    /// @return Total time for the frame (compute + encode + write), in milliseconds.
    [[nodiscard]] constexpr double frameMs() const noexcept { return computeMs + encodeMs + writeMs; }
};

/// Aggregated benchmark result produced by Metrics::summarize().
struct Summary
{
    std::size_t frames {};     ///< Number of frames rendered.
    double elapsedSeconds {};  ///< Wall-clock duration of the run.
    std::size_t totalBytes {}; ///< Total payload bytes written.
    double avgFrameMs {};      ///< Mean per-frame time.
    double minFrameMs {};      ///< Fastest frame time.
    double maxFrameMs {};      ///< Slowest frame time.
    double avgComputeMs {};    ///< Mean field-generation time.
    double avgEncodeMs {};     ///< Mean encode time.
    double avgWriteMs {};      ///< Mean write time.
    double avgFps {};          ///< Mean frames per second (wall clock).
    double peakFps {};         ///< Best single-frame FPS (1000 / fastest frame).
    double avgMBps {};         ///< Mean payload throughput (wall clock), MiB/s.
    double peakMBps {};        ///< Best single-frame payload throughput, MiB/s.
};

/// Accumulates per-frame Samples and produces a Summary.
class Metrics
{
  public:
    /// Records one frame's sample.
    void add(Sample const& sample) noexcept
    {
        auto const frameMs = sample.frameMs();
        if (_frames == 0)
        {
            _minFrameMs = frameMs;
            _maxFrameMs = frameMs;
        }
        else
        {
            _minFrameMs = std::min(_minFrameMs, frameMs);
            _maxFrameMs = std::max(_maxFrameMs, frameMs);
        }

        ++_frames;
        _totalBytes += sample.bytes;
        _sumFrameMs += frameMs;
        _sumComputeMs += sample.computeMs;
        _sumEncodeMs += sample.encodeMs;
        _sumWriteMs += sample.writeMs;

        if (frameMs > 0.0)
        {
            _peakFps = std::max(_peakFps, 1000.0 / frameMs);
            _peakMBps = std::max(_peakMBps, throughputMBps(sample.bytes, frameMs));
        }
    }

    /// Summarizes all recorded frames.
    /// @param elapsedSeconds Wall-clock duration of the run (drives avg FPS / throughput).
    /// @return The aggregated Summary.
    [[nodiscard]] Summary summarize(double elapsedSeconds) const noexcept
    {
        Summary summary;
        summary.frames = _frames;
        summary.elapsedSeconds = elapsedSeconds;
        summary.totalBytes = _totalBytes;
        summary.peakFps = _peakFps;
        summary.peakMBps = _peakMBps;
        if (_frames == 0)
            return summary;

        auto const frames = static_cast<double>(_frames);
        summary.avgFrameMs = _sumFrameMs / frames;
        summary.minFrameMs = _minFrameMs;
        summary.maxFrameMs = _maxFrameMs;
        summary.avgComputeMs = _sumComputeMs / frames;
        summary.avgEncodeMs = _sumEncodeMs / frames;
        summary.avgWriteMs = _sumWriteMs / frames;
        if (elapsedSeconds > 0.0)
        {
            summary.avgFps = frames / elapsedSeconds;
            summary.avgMBps = throughputMBps(_totalBytes, elapsedSeconds * 1000.0);
        }
        return summary;
    }

  private:
    std::size_t _frames {};
    std::size_t _totalBytes {};
    double _sumFrameMs {};
    double _sumComputeMs {};
    double _sumEncodeMs {};
    double _sumWriteMs {};
    double _minFrameMs {};
    double _maxFrameMs {};
    double _peakFps {};
    double _peakMBps {};
};

} // namespace plasma
