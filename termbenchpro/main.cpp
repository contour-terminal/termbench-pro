/**
 * This file is part of the "termbench-pro" project
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
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <fmt/format.h>

using std::cerr;
using std::cout;
using std::stoul;
using std::tuple;

using namespace std::string_view_literals;
using namespace std::placeholders;

#if !defined(_WIN32)
#include <unistd.h>
#include <sys/ioctl.h>
#else
#include <Windows.h>
#endif

namespace
{
    std::pair<unsigned short, unsigned short> getTerminalSize() noexcept
    {
        auto const DefaultSize = std::pair<unsigned short, unsigned short>{80, 24};

#if !defined(_WIN32)
        winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0)
            return DefaultSize;
        return {ws.ws_col, ws.ws_row};
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
        DWORD nwritten{};
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
}

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

    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] == "--null-sink"sv)
        {
            cout << fmt::format("Using null-sink.\n");
            nullSink = true;
        }
        else if (argv[i] == "--size"sv && i + 1 < argc)
        {
            ++i;
            testSizeMB = static_cast<size_t>(stoul(argv[i]));
        }
        else if (argv[i] == "--help"sv || argv[i] == "-h"sv)
        {
            cout << fmt::format("{} [--null-sink] [--size MB]\n", argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            cerr << fmt::format("Invalid argument usage.\n");
            return EXIT_FAILURE;
        }
    }

    contour::termbench::Benchmark tb{
        nullSink ? nullWrite : chunkedWriteToStdout,
        testSizeMB, // MB per test
        width,
        height
    };

    // mlfgb
    tb.add(contour::termbench::tests::many_lines());
    tb.add(contour::termbench::tests::long_lines());
    tb.add(contour::termbench::tests::sgr_fg_lines());
    tb.add(contour::termbench::tests::sgr_fgbg_lines());
    //tb.add(contour::termbench::tests::binary());

    tb.runAll();

    cout << "\033c\033[m\033[H\033J";
    tb.summarize(cout);

    return EXIT_SUCCESS;
}
