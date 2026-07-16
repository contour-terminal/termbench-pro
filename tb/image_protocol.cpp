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
#include <tb/gip.h>
#include <tb/image_protocol.h>
#include <tb/iterm2.h>
#include <tb/kitty.h>
#include <tb/sixel.h>

#include <span>

namespace img
{

namespace
{
    /// One row of the protocol registry: a name plus a factory for its encoder.
    struct Entry
    {
        std::string_view name;
        std::unique_ptr<ImageProtocol> (*make)();
    };

    /// The registry table. Adding a protocol is a one-row change here (plus its header).
    [[nodiscard]] std::span<Entry const> registry() noexcept
    {
        static Entry const table[] = {
            { "sixel",
              [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<sixel::SixelProtocol>(); } },
            { "kitty",
              [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<kitty::KittyProtocol>(); } },
            { "iterm2",
              [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<iterm2::Iterm2Protocol>(); } },
            { "gip", [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<gip::GipProtocol>(); } },
            { "gip-png",
              [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<gip::GipPngProtocol>(); } },
            { "gip-upload",
              [] -> std::unique_ptr<ImageProtocol> { return std::make_unique<gip::GipUploadProtocol>(); } },
        };
        return table;
    }
} // namespace

std::unique_ptr<ImageProtocol> makeProtocol(std::string_view name)
{
    for (auto const& entry: registry())
        if (entry.name == name)
            return entry.make();
    return nullptr;
}

std::vector<std::string_view> protocolNames()
{
    std::vector<std::string_view> names;
    names.reserve(registry().size());
    for (auto const& entry: registry())
        names.push_back(entry.name);
    return names;
}

} // namespace img
