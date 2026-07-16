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

#include <cstdint>
#include <span>
#include <string>

namespace base64
{

/// The standard base64 alphabet (RFC 4648).
inline constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Encodes @p data as standard (padded) base64, appending the result to @p out.
///
/// Pure and allocation-light: the encoded text is appended to the caller-owned sink, so this
/// performs no I/O and is trivially unit-testable.
///
/// @param out  Sink the base64 text is appended to.
/// @param data Raw bytes to encode.
inline void encode(std::string& out, std::span<std::uint8_t const> data)
{
    auto const size = data.size();
    out.reserve(out.size() + ((size + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 3 <= size; i += 3)
    {
        auto const b0 = static_cast<unsigned>(data[i]);
        auto const b1 = static_cast<unsigned>(data[i + 1]);
        auto const b2 = static_cast<unsigned>(data[i + 2]);
        out.push_back(Alphabet[b0 >> 2]);
        out.push_back(Alphabet[((b0 & 0x03u) << 4) | (b1 >> 4)]);
        out.push_back(Alphabet[((b1 & 0x0Fu) << 2) | (b2 >> 6)]);
        out.push_back(Alphabet[b2 & 0x3Fu]);
    }

    if (auto const remaining = size - i; remaining == 1)
    {
        auto const b0 = static_cast<unsigned>(data[i]);
        out.push_back(Alphabet[b0 >> 2]);
        out.push_back(Alphabet[(b0 & 0x03u) << 4]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (remaining == 2)
    {
        auto const b0 = static_cast<unsigned>(data[i]);
        auto const b1 = static_cast<unsigned>(data[i + 1]);
        out.push_back(Alphabet[b0 >> 2]);
        out.push_back(Alphabet[((b0 & 0x03u) << 4) | (b1 >> 4)]);
        out.push_back(Alphabet[(b1 & 0x0Fu) << 2]);
        out.push_back('=');
    }
}

} // namespace base64
