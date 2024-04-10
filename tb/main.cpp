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

#include <libtermbench/termbench.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <string_view>

using std::cerr;
using std::cout;

using namespace std::string_view_literals;
using namespace std::placeholders;

#if !defined(_WIN32)
    #include <sys/ioctl.h>
    #include <sys/stat.h>

    #include <unistd.h>
#else
    #include <Windows.h>
#endif

#ifndef STDOUT_FILENO
    #define STDOUT_FILENO 1
#endif

#define STDOUT_FASTPATH_FD 3

using termbench::TerminalSize;

namespace
{

TerminalSize getTerminalSize() noexcept
{
    auto const DefaultSize = TerminalSize { 80, 24 };

#if !defined(_WIN32)
    winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0)
        return DefaultSize;
    return { ws.ws_col, ws.ws_row };
#else
    CONSOLE_SCREEN_BUFFER_INFOEX info {
        .cbSize = sizeof(info),
    };
    if (!GetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return DefaultSize;
    return {
        static_cast<unsigned short>(info.srWindow.Right - info.srWindow.Left + 1),
        static_cast<unsigned short>(info.srWindow.Bottom - info.srWindow.Top + 1),
    };
#endif
}

void nullWrite(char const*, size_t)
{
}

template <const std::size_t StdoutFileNo>
void chunkedWriteToStdout(char const* _data, size_t _size)
{
    auto constexpr PageSize = 4096; // 8192;

#if !defined(_WIN32)
    while (_size >= PageSize)
    {
        auto const n = write(StdoutFileNo, _data, PageSize);
        if (n < 0)
            perror("write");
        else
        {
            _data += n;
            _size -= static_cast<size_t>(n);
        }
    }
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-result"
    write(StdoutFileNo, _data, _size);
    #pragma GCC diagnostic pop
#else
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD nwritten {};
    while (_size >= PageSize)
    {
        WriteFile(stdoutHandle, _data, static_cast<DWORD>(_size), &nwritten, nullptr);
        _data += nwritten;
        _size -= static_cast<size_t>(nwritten);
    }
    WriteFile(stdoutHandle, _data, static_cast<DWORD>(_size), &nwritten, nullptr);
#endif
}

struct TestsToRun
{
    bool manyLines { true };
    bool longLines { true };
    bool sgrLines { true };
    bool sgrFgBgLines { true };
    bool binary { true };
    bool columnByColumn { false };
};

struct BenchSettings
{
    TerminalSize requestedTerminalSize {};
    size_t testSizeMB = 32;
    bool nullSink = false;
    bool stdoutFastPath = false;
    std::vector<std::filesystem::path> craftedTests {};
    std::string fileout {};
    std::optional<int> earlyExitCode = std::nullopt;
    TestsToRun tests {};
};

BenchSettings parseArguments(int argc, char const* argv[], TerminalSize const& initialTerminalSize)
{
    auto settings = BenchSettings { .requestedTerminalSize = initialTerminalSize };
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == "--null-sink"sv)
        {
            cout << std::format("Using null-sink.\n");
            settings.nullSink = true;
        }
        else if (argv[i] == "--fixed-size"sv)
        {
            settings.requestedTerminalSize.columns = 100;
            settings.requestedTerminalSize.lines = 30;
        }
        else if (argv[i] == "--stdout-fastpath"sv)
        {
#if !defined(_WIN32)
            struct stat st;
            settings.stdoutFastPath = fstat(STDOUT_FASTPATH_FD, &st) == 0;
#else
            std::cout << std::format("Ignoring {}\n", argv[i]);
#endif
        }
        else if (argv[i] == "--column-by-column"sv)
        {
            cout << std::format("Enabling column-by-column tests.\n");
            settings.tests.columnByColumn = true;
            settings.tests.manyLines = false;
            settings.tests.longLines = false;
            settings.tests.sgrLines = false;
            settings.tests.sgrFgBgLines = false;
            settings.tests.binary = false;
        }
        else if (argv[i] == "--size"sv && i + 1 < argc)
        {
            ++i;
            settings.testSizeMB = static_cast<size_t>(std::stoul(argv[i]));
        }
        else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
        {
            cout << std::format("{} [--null-sink] [--fixed-size] [--stdout-fastpath] [--column-by-column] "
                                "[--size MB] [--from-file FILE] [--output FILE] [--help]\n",
                                argv[0]);
            return { .earlyExitCode = EXIT_SUCCESS };
        }
        else if (argv[i] == "--output"sv && i + 1 < argc)
        {
            ++i;
            settings.fileout = argv[i];
        }
        else if (argv[i] == "--from-file"sv && i + 1 < argc)
        {
            ++i;
            if (!std::filesystem::exists(argv[i]))
            {
                cerr << std::format("Failed to open file '{}'.\n", argv[i]);
                return { .earlyExitCode = EXIT_FAILURE };
            }
            settings.craftedTests.emplace_back(argv[i]);
        }
        else
        {
            cerr << std::format("Invalid argument usage.\n");
            return { .earlyExitCode = EXIT_FAILURE };
        }
    }
    return settings;
}

std::string loadFileContents(std::filesystem::path const& path)
{
    std::ifstream file { path };
    if (!file)
        return {};

    std::string content;
    std::size_t const fileSize = std::filesystem::file_size(path);
    content.resize(fileSize);
    file.read(content.data(), static_cast<std::streamsize>(fileSize));
    return content;
}

bool addTestsToBenchmark(termbench::Benchmark& tb, BenchSettings const& settings)
{

    if (settings.tests.manyLines)
        tb.add(termbench::tests::many_lines());
    if (settings.tests.longLines)
        tb.add(termbench::tests::long_lines());
    if (settings.tests.sgrLines)
        tb.add(termbench::tests::sgr_fg_lines());
    if (settings.tests.sgrFgBgLines)
        tb.add(termbench::tests::sgr_fgbg_lines());
    if (settings.tests.binary)
        tb.add(termbench::tests::binary());

    for (auto const& test: settings.craftedTests)
    {
        auto content = loadFileContents(test);
        if (content.empty())
        {
            cerr << std::format("Failed to load file '{}'.\n", test.string());
            return false;
        }
        tb.add(termbench::tests::crafted(test.filename().string(), "", std::move(content)));
    }

    if (settings.tests.columnByColumn)
    {
        auto const maxColumns { settings.requestedTerminalSize.columns * 2u };

        auto add_test = [&](auto&& test) {
            for (size_t i = 0; i < maxColumns; ++i)
                tb.add(std::invoke(test, i));
        };

        add_test(termbench::tests::ascii_line);
        add_test(termbench::tests::unicode_simple);
        add_test(termbench::tests::unicode_two_codepoints);
        add_test(termbench::tests::unicode_three_codepoints);
        add_test(termbench::tests::unicode_fire_as_text);
        add_test(termbench::tests::unicode_fire);
        add_test(termbench::tests::unicode_flag);
        add_test(termbench::tests::sgr_line);
        add_test(termbench::tests::sgrbg_line);
    }
    return true;
}

void changeTerminalSize(TerminalSize requestedTerminalSize)
{
    cout << std::format("\033[8;{};{}t", requestedTerminalSize.lines, requestedTerminalSize.columns);
    cout.flush();
}

struct WithScopedTerminalSize
{
    TerminalSize initialTerminalSize;
    TerminalSize requestedTerminalSize;
    std::function<void()> inner;

    void operator()()
    {
        if (requestedTerminalSize != initialTerminalSize)
            changeTerminalSize(requestedTerminalSize);

        inner();
    }

    ~WithScopedTerminalSize()
    {
        if (requestedTerminalSize != initialTerminalSize)
            changeTerminalSize(initialTerminalSize);
    }
};

} // namespace

int main(int argc, char const* argv[])
{
#if defined(_WIN32)
    const auto stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    const auto stdoutCP = GetConsoleOutputCP();
    DWORD stdoutMode;
    GetConsoleMode(stdoutHandle, &stdoutMode);
    SetConsoleMode(stdoutHandle,
                   ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
#endif

    auto const initialTerminalSize = getTerminalSize();
    auto const settings = parseArguments(argc, argv, initialTerminalSize);

    if (settings.earlyExitCode)
        return settings.earlyExitCode.value();

    auto const writer = settings.nullSink         ? nullWrite
                        : settings.stdoutFastPath ? chunkedWriteToStdout<STDOUT_FASTPATH_FD>
                                                  : chunkedWriteToStdout<STDOUT_FILENO>;

    termbench::Benchmark tb { writer,
                              settings.testSizeMB, // MB per test
                              settings.requestedTerminalSize };

    if (!addTestsToBenchmark(tb, settings))
        return EXIT_FAILURE;

    WithScopedTerminalSize {
        initialTerminalSize,
        settings.requestedTerminalSize,
        [&]() { tb.runAll(); },
    }();

    cout << "\033[m\033[H\033[J";
    cout.flush();
    if (settings.fileout.empty())
        tb.summarize(cout);
    else
    {
        cout << "Writing summary into " << settings.fileout << std::endl;
        std::ofstream writerToFile;
        writerToFile.open(settings.fileout);
        tb.summarizeToJson(writerToFile);
    }

#if defined(_WIN32)
    SetConsoleMode(stdoutHandle, stdoutMode);
    SetConsoleOutputCP(stdoutCP);
#endif
    return EXIT_SUCCESS;
}
