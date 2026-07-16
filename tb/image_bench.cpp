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

#include <tb/image_protocol.h>
#include <tb/plasma.h>

#include <libtermbench/termbench.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
    #include <sys/ioctl.h>

    #include <cerrno>

    #include <poll.h>
    #include <termios.h>
    #include <unistd.h>
#else
    #include <Windows.h>
    #include <conio.h>
    #include <io.h>
#endif

using namespace std::string_view_literals;
using std::chrono::steady_clock;

namespace
{

/// Set from the SIGINT/SIGTERM handler to request a clean shutdown.
volatile std::sig_atomic_t g_stopRequested = 0;

extern "C" void onSignal(int /*signum*/)
{
    g_stopRequested = 1;
}

// {{{ configuration & defaults (data-driven knobs)

constexpr unsigned DefaultColors = 256;
constexpr unsigned DefaultCellWidth = 10;  ///< Fallback cell width (px) when the terminal is silent.
constexpr unsigned DefaultCellHeight = 20; ///< Fallback cell height (px) when the terminal is silent.
constexpr unsigned DefaultColumns = 80;
constexpr unsigned DefaultLines = 24;
constexpr unsigned HudLines = 1; ///< Terminal rows reserved at the bottom for the metrics HUD.

/// @return The default protocol name — the first entry in the registry, so the default travels
///         with the registry table rather than being hardcoded in the driver.
[[nodiscard]] std::string defaultProtocol()
{
    auto const names = img::protocolNames();
    return names.empty() ? std::string {} : std::string { names.front() };
}

/// Parsed command-line configuration.
struct Args
{
    std::string protocol = defaultProtocol();
    unsigned colors = DefaultColors;
    std::optional<unsigned> pixelWidth;
    std::optional<unsigned> pixelHeight;
    std::optional<unsigned> cellWidth;
    std::optional<unsigned> cellHeight;
    std::optional<unsigned> fpsCap;
    std::optional<double> durationSeconds;
    double speed = 1.0;
};

/// Errors that can arise while probing the terminal.
enum class SetupError
{
    InvalidGeometry, ///< The resolved image dimensions were degenerate.
};

// }}}
// {{{ platform I/O

/// Writes @p data to standard output in full, retrying on partial/interrupted writes.
void writeAll(std::string_view data)
{
#if !defined(_WIN32)
    auto const* pointer = data.data();
    auto remaining = data.size();
    while (remaining > 0)
    {
        auto const written = ::write(STDOUT_FILENO, pointer, remaining);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        pointer += written;
        remaining -= static_cast<std::size_t>(written);
    }
#else
    HANDLE const handle = GetStdHandle(STD_OUTPUT_HANDLE);
    auto const* pointer = data.data();
    auto remaining = data.size();
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(handle, pointer, static_cast<DWORD>(remaining), &written, nullptr))
            break;
        pointer += written;
        remaining -= static_cast<std::size_t>(written);
    }
#endif
}

/// @return true if the user pressed ESC or `q` (drains all currently-available input).
[[nodiscard]] bool pollQuit()
{
#if !defined(_WIN32)
    std::array<char, 64> buffer {};
    auto const count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
    if (count <= 0)
        return false;
    for (auto const i: std::views::iota(std::ptrdiff_t { 0 }, static_cast<std::ptrdiff_t>(count)))
        if (auto const ch = buffer[static_cast<std::size_t>(i)]; ch == 27 || ch == 'q' || ch == 'Q')
            return true;
    return false;
#else
    bool quit = false;
    while (_kbhit())
        if (auto const ch = _getch(); ch == 27 || ch == 'q' || ch == 'Q')
            quit = true;
    return quit;
#endif
}

/// @return true if both standard input and output are attached to a terminal.
[[nodiscard]] bool standardStreamsAreInteractive()
{
#if !defined(_WIN32)
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#else
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#endif
}

/// Owns the terminal's interactive state: raw mode, alternate screen, and hidden cursor.
///
/// Only engages when both streams are a TTY; the destructor restores the original state so a
/// crash or Ctrl-C never leaves the terminal wedged.
class TerminalSession
{
  public:
    TerminalSession(): _interactive { standardStreamsAreInteractive() }
    {
        if (!_interactive)
            return;

#if !defined(_WIN32)
        if (tcgetattr(STDIN_FILENO, &_originalTermios) == 0)
        {
            auto raw = _originalTermios;
            raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO)); // unbuffered, no echo (keep ISIG)
            raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));  // no flow control / CR->NL
            raw.c_oflag &= static_cast<tcflag_t>(~OPOST);           // raw output for binary payloads
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            _rawEnabled = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
        }
#else
        HANDLE const out = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE const in = GetStdHandle(STD_INPUT_HANDLE);
        if (GetConsoleMode(out, &_originalOutMode) && GetConsoleMode(in, &_originalInMode))
        {
            SetConsoleMode(out, _originalOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            SetConsoleMode(in,
                           _originalInMode & static_cast<DWORD>(~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT)));
            _modeSaved = true;
        }
#endif
        writeAll("\033[?1049h\033[?25l\033[2J\033[H"sv); // alt screen, hide cursor, clear, home
    }

    ~TerminalSession()
    {
        if (!_interactive)
            return;

        writeAll("\033[?25h\033[?1049l"sv); // show cursor, leave alt screen
#if !defined(_WIN32)
        if (_rawEnabled)
            tcsetattr(STDIN_FILENO, TCSANOW, &_originalTermios);
#else
        if (_modeSaved)
        {
            SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), _originalOutMode);
            SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), _originalInMode);
        }
#endif
    }

    TerminalSession(TerminalSession const&) = delete;
    TerminalSession& operator=(TerminalSession const&) = delete;
    TerminalSession(TerminalSession&&) = delete;
    TerminalSession& operator=(TerminalSession&&) = delete;

    [[nodiscard]] bool interactive() const noexcept { return _interactive; }

  private:
    bool _interactive;
#if !defined(_WIN32)
    termios _originalTermios {};
    bool _rawEnabled = false;
#else
    DWORD _originalOutMode = 0;
    DWORD _originalInMode = 0;
    bool _modeSaved = false;
#endif
};

// }}}
// {{{ geometry

/// The resolved rendering geometry.
struct Geometry
{
    unsigned columns = DefaultColumns; ///< Terminal width in cells.
    unsigned lines = DefaultLines;     ///< Terminal height in cells.
    unsigned imageWidth = 0;           ///< Image width in pixels.
    unsigned imageHeight = 0;          ///< Image height in pixels.
};

#if !defined(_WIN32)
/// Queries the terminal's text-area size in pixels via `CSI 14 t`.
/// @return {width, height} in pixels, or nullopt on timeout / no reply.
[[nodiscard]] std::optional<std::pair<unsigned, unsigned>> queryTextAreaPixels()
{
    writeAll("\033[14t"sv);

    std::string reply;
    auto const deadline = steady_clock::now() + std::chrono::milliseconds { 200 };
    while (steady_clock::now() < deadline && reply.find('t') == std::string::npos)
    {
        pollfd descriptor { STDIN_FILENO, POLLIN, 0 };
        auto const remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - steady_clock::now()).count();
        if (::poll(&descriptor, 1, static_cast<int>(std::max<long long>(0, remaining))) <= 0)
            break;

        std::array<char, 64> buffer {};
        auto const count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count <= 0)
            break;
        reply.append(buffer.data(), static_cast<std::size_t>(count));
    }

    unsigned height = 0;
    unsigned width = 0;
    if (auto const pos = reply.find("\033[4;");
        pos != std::string::npos && std::sscanf(reply.c_str() + pos, "\033[4;%u;%ut", &height, &width) == 2
        && width > 0 && height > 0)
        return std::pair { width, height };
    return std::nullopt;
}
#endif

/// Determines the terminal size (cells + pixels) and derives the image geometry.
///
/// Prefers `ioctl(TIOCGWINSZ)` pixel fields, falls back to a `CSI 14 t` query (when interactive),
/// then to a default cell size; CLI overrides win over all of them.
///
/// @param args        Parsed CLI configuration (may override cell/image size).
/// @param interactive Whether a `CSI 14 t` query is permissible.
/// @return The geometry, or SetupError::InvalidGeometry if it resolves to a degenerate size.
[[nodiscard]] std::expected<Geometry, SetupError> detectGeometry(Args const& args, bool interactive)
{
    unsigned columns = DefaultColumns;
    unsigned lines = DefaultLines;
    unsigned pixelsX = 0;
    unsigned pixelsY = 0;

#if !defined(_WIN32)
    winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    {
        if (ws.ws_col != 0)
            columns = ws.ws_col;
        if (ws.ws_row != 0)
            lines = ws.ws_row;
        pixelsX = ws.ws_xpixel;
        pixelsY = ws.ws_ypixel;
    }
#else
    CONSOLE_SCREEN_BUFFER_INFO info {};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
    {
        columns = static_cast<unsigned>(info.srWindow.Right - info.srWindow.Left + 1);
        lines = static_cast<unsigned>(info.srWindow.Bottom - info.srWindow.Top + 1);
    }
#endif

    unsigned cellWidth = (pixelsX != 0 && columns != 0) ? pixelsX / columns : 0;
    unsigned cellHeight = (pixelsY != 0 && lines != 0) ? pixelsY / lines : 0;

#if !defined(_WIN32)
    if ((cellWidth == 0 || cellHeight == 0) && interactive)
        if (auto const pixels = queryTextAreaPixels())
        {
            cellWidth = pixels->first / columns;
            cellHeight = pixels->second / lines;
        }
#else
    (void) interactive;
#endif

    cellWidth = args.cellWidth.value_or(cellWidth != 0 ? cellWidth : DefaultCellWidth);
    cellHeight = args.cellHeight.value_or(cellHeight != 0 ? cellHeight : DefaultCellHeight);

    auto const usableLines = lines > HudLines ? lines - HudLines : lines;
    auto const imageWidth = args.pixelWidth.value_or(columns * cellWidth);
    auto const imageHeight = args.pixelHeight.value_or(usableLines * cellHeight);

    if (imageWidth == 0 || imageHeight == 0)
        return std::unexpected(SetupError::InvalidGeometry);

    return Geometry { columns, lines, imageWidth, imageHeight };
}

// }}}
// {{{ HUD & summary

/// Builds the bottom-row metrics HUD (cursor positioning + styled, width-clamped status line).
[[nodiscard]] std::string buildHud(Args const& args,
                                   Geometry const& geometry,
                                   img::ImageProtocol const& protocol,
                                   double smoothedFps,
                                   plasma::Sample const& sample,
                                   std::size_t frameId)
{
    auto line = std::format(" {} | {:5.1f} fps | frame {:6.2f}ms (cmp {:4.1f} enc {:5.1f} wr {:5.1f}) |"
                            "{} /f | {:6.1f} MiB/s | {}x{}px | {}c | #{} | q/ESC quits ",
                            protocol.name(),
                            smoothedFps,
                            sample.frameMs(),
                            sample.computeMs,
                            sample.encodeMs,
                            sample.writeMs,
                            termbench::sizeStr(static_cast<double>(sample.bytes)),
                            plasma::throughputMBps(sample.bytes, sample.frameMs()),
                            geometry.imageWidth,
                            geometry.imageHeight,
                            args.colors,
                            frameId);

    if (line.size() > geometry.columns)
        line.resize(geometry.columns);
    else
        line.append(geometry.columns - line.size(), ' ');

    // The HUD occupies the last terminal row (HudLines reserved it in detectGeometry).
    return std::format("\033[{};1H\033[7m{}\033[0m", geometry.lines, line);
}

/// Prints the end-of-run benchmark summary.
void printSummary(std::ostream& os, Args const& args, Geometry const& geometry, plasma::Summary const& s)
{
    os << '\n';
    os << std::format("Image protocol benchmark — {}\n", args.protocol);
    os << std::format(
        "  Resolution     : {}x{} px, {} colors\n", geometry.imageWidth, geometry.imageHeight, args.colors);
    os << std::format("  Frames         : {}\n", s.frames);
    os << std::format("  Duration       : {:.2f} s\n", s.elapsedSeconds);
    os << std::format("  Throughput     : {} total, {:.2f} MiB/s avg, {:.2f} MiB/s peak\n",
                      termbench::sizeStr(static_cast<double>(s.totalBytes)),
                      s.avgMBps,
                      s.peakMBps);
    os << std::format("  Frame time     : {:.2f} ms avg, {:.2f} ms min, {:.2f} ms max\n",
                      s.avgFrameMs,
                      s.minFrameMs,
                      s.maxFrameMs);
    os << std::format("  Phase averages : compute {:.2f} ms, encode {:.2f} ms, write {:.2f} ms\n",
                      s.avgComputeMs,
                      s.avgEncodeMs,
                      s.avgWriteMs);
    os << std::format("  Frame rate     : {:.1f} fps avg, {:.1f} fps peak\n", s.avgFps, s.peakFps);
}

// }}}
// {{{ argument parsing

void printUsage(std::string_view program)
{
    auto names = std::string {};
    for (auto const& name: img::protocolNames())
    {
        if (!names.empty())
            names += '|';
        names += name;
    }

    std::cout << std::format("{} — animated plasma benchmark for terminal image protocols\n\n"
                             "Usage: {} [options]\n\n"
                             "  --protocol NAME   Image protocol to benchmark ({}); default: {}\n"
                             "  --colors N        Palette size, 1..256; default: {}\n"
                             "  --width PX        Force image width in pixels\n"
                             "  --height PX       Force image height in pixels\n"
                             "  --cell-width PX   Override the terminal cell width in pixels\n"
                             "  --cell-height PX  Override the terminal cell height in pixels\n"
                             "  --fps N           Cap the frame rate to N frames per second\n"
                             "  --duration S      Auto-exit after S seconds (for non-interactive runs)\n"
                             "  --speed F         Animation speed multiplier; default: 1.0\n"
                             "  --help, -h        Show this help\n\n"
                             "Press ESC or q to quit an interactive run.\n",
                             program,
                             program,
                             names,
                             defaultProtocol(),
                             DefaultColors);
}

/// Parses @p argv into Args.
/// @return The configuration, or an exit code to return (EXIT_SUCCESS for --help, EXIT_FAILURE for
///         a usage error).
[[nodiscard]] std::expected<Args, int> parseArgs(int argc, char const* const argv[])
{
    auto const program = argc > 0 ? std::string_view { argv[0] } : "image-bench"sv;

    auto requireValue = [&](int& i) -> std::optional<std::string_view> {
        if (i + 1 >= argc)
            return std::nullopt;
        return std::string_view { argv[++i] };
    };
    auto parseUnsigned = [](std::string_view text) -> std::optional<unsigned> {
        try
        {
            return static_cast<unsigned>(std::stoul(std::string { text }));
        }
        catch (...)
        {
            return std::nullopt;
        }
    };

    // Fetch-and-assign helpers for the many uniform value options (each returns success).
    auto takeUnsigned = [&](int& i, std::optional<unsigned>& target, std::string_view label) {
        auto const value = requireValue(i);
        auto const parsed = value ? parseUnsigned(*value) : std::nullopt;
        if (parsed)
            target = *parsed;
        else
            std::cerr << std::format("{} requires an unsigned integer value.\n", label);
        return parsed.has_value();
    };
    auto takeDouble = [&](int& i, std::string_view label) -> std::optional<double> {
        if (auto const value = requireValue(i))
        {
            try
            {
                return std::stod(std::string { *value });
            }
            catch (...)
            {
            }
        }
        std::cerr << std::format("{} requires a numeric value.\n", label);
        return std::nullopt;
    };

    Args args;
    for (int i = 1; i < argc; ++i)
    {
        auto const arg = std::string_view { argv[i] };
        if (arg == "--help"sv || arg == "-h"sv)
        {
            printUsage(program);
            return std::unexpected(EXIT_SUCCESS);
        }
        else if (arg == "--protocol"sv)
        {
            auto const value = requireValue(i);
            if (!value || !img::makeProtocol(*value))
            {
                std::cerr << std::format("Unknown or missing protocol for {}.\n", arg);
                return std::unexpected(EXIT_FAILURE);
            }
            args.protocol = *value;
        }
        else if (arg == "--colors"sv)
        {
            auto const value = requireValue(i);
            auto const parsed = value ? parseUnsigned(*value) : std::nullopt;
            if (!parsed || *parsed < 1 || *parsed > 256)
            {
                std::cerr << "--colors requires a value in 1..256.\n";
                return std::unexpected(EXIT_FAILURE);
            }
            args.colors = *parsed;
        }
        else if (arg == "--width"sv)
        {
            if (!takeUnsigned(i, args.pixelWidth, arg))
                return std::unexpected(EXIT_FAILURE);
        }
        else if (arg == "--height"sv)
        {
            if (!takeUnsigned(i, args.pixelHeight, arg))
                return std::unexpected(EXIT_FAILURE);
        }
        else if (arg == "--cell-width"sv)
        {
            if (!takeUnsigned(i, args.cellWidth, arg))
                return std::unexpected(EXIT_FAILURE);
        }
        else if (arg == "--cell-height"sv)
        {
            if (!takeUnsigned(i, args.cellHeight, arg))
                return std::unexpected(EXIT_FAILURE);
        }
        else if (arg == "--fps"sv)
        {
            auto const value = requireValue(i);
            auto const parsed = value ? parseUnsigned(*value) : std::nullopt;
            if (!parsed || *parsed == 0)
            {
                std::cerr << "--fps requires a positive value.\n";
                return std::unexpected(EXIT_FAILURE);
            }
            args.fpsCap = *parsed;
        }
        else if (arg == "--duration"sv)
        {
            auto const value = takeDouble(i, arg);
            if (!value)
                return std::unexpected(EXIT_FAILURE);
            args.durationSeconds = *value;
        }
        else if (arg == "--speed"sv)
        {
            auto const value = takeDouble(i, arg);
            if (!value)
                return std::unexpected(EXIT_FAILURE);
            args.speed = *value;
        }
        else
        {
            std::cerr << std::format("Unknown argument: {}\n", arg);
            printUsage(program);
            return std::unexpected(EXIT_FAILURE);
        }
    }
    return args;
}

// }}}
// {{{ render loop

/// Runs the animation/benchmark loop until quit/duration/signal, returning the aggregated summary.
[[nodiscard]] plasma::Summary runLoop(Args const& args,
                                      Geometry const& geometry,
                                      TerminalSession const& session)
{
    auto const palette = plasma::makePalette(args.colors);
    auto protocol = img::makeProtocol(args.protocol);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(geometry.imageWidth) * geometry.imageHeight,
                                      std::uint8_t { 0 });

    std::string frame;
    frame.reserve(std::size_t { 1 } << 20);

    plasma::Metrics metrics;

    auto const toMs = [](steady_clock::duration d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };
    auto const frameInterval =
        args.fpsCap
            ? std::optional { std::chrono::duration<double> { 1.0 / static_cast<double>(*args.fpsCap) } }
            : std::nullopt;

    auto const startTime = steady_clock::now();
    auto lastFrameStamp = startTime;
    double smoothedFps = 0.0;
    std::size_t frameId = 0;

    while (g_stopRequested == 0)
    {
        if (session.interactive() && pollQuit())
            break;

        auto const frameStart = steady_clock::now();
        auto const elapsed = std::chrono::duration<double>(frameStart - startTime).count();
        if (args.durationSeconds && elapsed >= *args.durationSeconds)
            break;

        auto const t0 = steady_clock::now();
        plasma::render(indices, geometry.imageWidth, geometry.imageHeight, elapsed * args.speed, args.colors);
        auto const t1 = steady_clock::now();

        frame.clear();
        frame += "\033[H";
        protocol->encode(frame, img::Image { indices, geometry.imageWidth, geometry.imageHeight, palette });
        auto const t2 = steady_clock::now();

        writeAll(frame);
        auto const t3 = steady_clock::now();

        auto const sample = plasma::Sample { toMs(t1 - t0), toMs(t2 - t1), toMs(t3 - t2), frame.size() };
        metrics.add(sample);
        ++frameId;

        auto const now = steady_clock::now();
        auto const delta = std::chrono::duration<double>(now - lastFrameStamp).count();
        lastFrameStamp = now;
        auto const instantFps = delta > 0.0 ? 1.0 / delta : 0.0;
        smoothedFps = smoothedFps == 0.0 ? instantFps : (smoothedFps * 0.9 + instantFps * 0.1);

        if (session.interactive())
            writeAll(buildHud(args, geometry, *protocol, smoothedFps, sample, frameId));

        if (frameInterval)
        {
            auto const spent = steady_clock::now() - frameStart;
            auto const target = std::chrono::duration_cast<steady_clock::duration>(*frameInterval);
            if (spent < target)
                std::this_thread::sleep_for(target - spent);
        }
    }

    auto const totalElapsed = std::chrono::duration<double>(steady_clock::now() - startTime).count();
    return metrics.summarize(totalElapsed);
}

// }}}

} // namespace

int main(int argc, char const* argv[])
{
    auto const parsed = parseArgs(argc, argv);
    if (!parsed)
        return parsed.error();
    auto const& args = *parsed;

#if !defined(_WIN32)
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
#else
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#endif

    Geometry geometry;
    plasma::Summary summary;
    bool geometryOk = false;
    {
        TerminalSession session;
        auto const geometryResult = detectGeometry(args, session.interactive());
        if (geometryResult)
        {
            geometry = *geometryResult;
            geometryOk = true;
            summary = runLoop(args, geometry, session);
        }
    }

    if (!geometryOk)
    {
        std::cerr << "Failed to determine a usable terminal geometry.\n";
        return EXIT_FAILURE;
    }

    printSummary(std::cout, args, geometry, summary);
    return EXIT_SUCCESS;
}
