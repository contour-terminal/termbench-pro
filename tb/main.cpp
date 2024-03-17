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
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>

using std::cerr;
using std::cout;
using std::stoul;
using std::tuple;

using namespace std::string_view_literals;
using namespace std::placeholders;

#if !defined(_WIN32)
    #include <sys/ioctl.h>

    #include <unistd.h>
#else
    #include <Windows.h>
#endif

namespace
{
std::pair<unsigned short, unsigned short> getTerminalSize() noexcept
{
    auto const DefaultSize = std::pair<unsigned short, unsigned short> { 80, 24 };

#if !defined(_WIN32)
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
        return DefaultSize;
    return { ws.ws_col, ws.ws_row };
#else
    // TODO: Windows
    return DefaultSize;
#endif
}

void nullWrite(char const*, size_t)
{
}

void chunkedWriteToStdout(char const* _data, size_t _size)
{
    auto constexpr PageSize = 4096; // 8192;

#if defined(_WIN32)
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD nwritten {};
#endif

    while (_size >= PageSize)
    {
#if !defined(_WIN32)
        auto const n = write(STDOUT_FILENO, _data, PageSize);
        if (n < 0)
            perror("write");
        _data += n;
        _size -= static_cast<size_t>(n);
#else
        WriteConsoleA(stdoutHandle, _data, static_cast<DWORD>(_size), &nwritten, nullptr);
#endif
    }

#if !defined(_WIN32)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-result"
    write(STDOUT_FILENO, _data, _size);
    #pragma GCC diagnostic pop
#else
    WriteConsoleA(stdoutHandle, _data, static_cast<DWORD>(_size), &nwritten, nullptr);
#endif
}
} // namespace

int main(int argc, char const* argv[])
{
#if defined(_WIN32)
    {
        HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleMode(stdoutHandle, ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
    // TODO: Also run against NullSink to get a base value.

    auto [width, height] = getTerminalSize();
    size_t testSizeMB = 32;
    bool nullSink = false;
    std::string fileout {};

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == "--null-sink"sv)
        {
            cout << std::format("Using null-sink.\n");
            nullSink = true;
        }
        else if (argv[i] == "--size"sv && i + 1 < argc)
        {
            ++i;
            testSizeMB = static_cast<size_t>(stoul(argv[i]));
        }
        else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
        {
            cout << std::format("{} [--null-sink] [--size MB]\n", argv[0]);
            return EXIT_SUCCESS;
        }
        else if (argv[i] == "--output"sv && i + 1 < argc)
        {
            ++i;
            fileout = argv[i];
        }
        else
        {
            cerr << std::format("Invalid argument usage.\n");
            return EXIT_FAILURE;
        }
    }

    contour::termbench::Benchmark tb { nullSink ? nullWrite : chunkedWriteToStdout,
                                       testSizeMB, // MB per test
                                       width,
                                       height };

    // mlfgb
    tb.add(contour::termbench::tests::many_lines());
    tb.add(contour::termbench::tests::long_lines());
    tb.add(contour::termbench::tests::sgr_fg_lines());
    tb.add(contour::termbench::tests::sgr_fgbg_lines());
    tb.add(contour::termbench::tests::binary());
    // tb.add(contour::termbench::tests::binary());
    constexpr size_t Max_lines { 200 };
    for (size_t i = 0; i < Max_lines; ++i)
        tb.add(contour::termbench::tests::ascii_line(i));
    for (size_t i = 0; i < Max_lines; ++i)
        tb.add(contour::termbench::tests::sgr_line(i));
    for (size_t i = 0; i < Max_lines; ++i)
        tb.add(contour::termbench::tests::sgrbg_line(i));

    cout << "\033[8;30;100t";
    cout.flush();

    tb.runAll();

    cout << "\033[m\033[H\033[J";
    cout.flush();
    if (fileout.empty())
        tb.summarize(cout);
    else
    {
        cout << "Writing summary into " << fileout << std::endl;
        std::ofstream writerToFile;
        writerToFile.open(fileout);
        tb.summarize(writerToFile);
    }

    return EXIT_SUCCESS;
}
