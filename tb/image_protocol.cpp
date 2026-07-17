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
    ///
    /// The factory takes the driver's options and decides what its protocol makes of them, so a
    /// protocol that honours a knob does not oblige the others to know about it.
    struct Entry
    {
        std::string_view name;
        std::unique_ptr<ImageProtocol> (*make)(ProtocolOptions const&);
    };

    /// The registry table. Adding a protocol is a one-row change here (plus its header).
    [[nodiscard]] std::span<Entry const> registry() noexcept
    {
        static Entry const table[] = {
            { "sixel",
              [](ProtocolOptions const&) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<sixel::SixelProtocol>();
              } },
            { "kitty",
              [](ProtocolOptions const& options) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<kitty::KittyProtocol>(options.compressionLevel);
              } },
            { "iterm2",
              [](ProtocolOptions const& options) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<iterm2::Iterm2Protocol>(options.compressionLevel);
              } },
            { "gip",
              [](ProtocolOptions const&) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<gip::GipProtocol>();
              } },
            { "gip-png",
              [](ProtocolOptions const& options) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<gip::GipPngProtocol>(options.compressionLevel);
              } },
            { "gip-upload",
              [](ProtocolOptions const&) -> std::unique_ptr<ImageProtocol> {
                  return std::make_unique<gip::GipUploadProtocol>();
              } },
        };
        return table;
    }
} // namespace

std::unique_ptr<ImageProtocol> makeProtocol(std::string_view name, ProtocolOptions const& options)
{
    for (auto const& entry: registry())
        if (entry.name == name)
            return entry.make(options);
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
