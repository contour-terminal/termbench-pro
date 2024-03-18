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
#include "termbench.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <ostream>
#include <utility>

using namespace std::chrono;
using namespace std::string_view_literals;

namespace contour::termbench
{

namespace
{
    std::string sizeStr(double _value)
    {
        if ((long double) (_value) >= (1024ull * 1024ull * 1024ull)) // GB
            return std::format("{:7.3f} GB", _value / 1024.0 / 1024.0 / 1024.0);
        if (_value >= (1024 * 1024)) // MB
            return std::format("{:7.3f} MB", _value / 1024.0 / 1024.0);
        if (_value >= 1024) // KB
            return std::format("{:7.3f} KB", _value / 1024.0);
        return std::format("{:7.3f} bytes", _value);
    }
} // namespace

using u16 = unsigned short;

Benchmark::Benchmark(std::function<void(char const*, size_t n)> _writer,
                     size_t _testSizeMB,
                     TerminalSize terminalSize,
                     std::function<void(Test const&)> _beforeTest):
    writer_ { std::move(_writer) },
    beforeTest_ { std::move(_beforeTest) },
    testSizeMB_ { _testSizeMB },
    terminalSize_ { terminalSize }
{
}

void Benchmark::add(std::unique_ptr<Test> _test)
{
    tests_.emplace_back(std::move(_test));
}

void Benchmark::writeOutput(Buffer const& testBuffer)
{
    auto const output = testBuffer.output();
    auto remainingBytes = totalSizeBytes();
    while (remainingBytes > 0)
    {
        auto const n = std::min(output.size(), remainingBytes);
        writer_(output.data(), n);
        remainingBytes -= n;
    }
}

void Benchmark::runAll()
{
    auto buffer = std::make_unique<Buffer>(std::min(static_cast<size_t>(64u), testSizeMB_));

    for (auto& test: tests_)
    {
        if (beforeTest_)
            beforeTest_(*test);

        test->setup(terminalSize_);
        test->run(*buffer);

        auto const beginTime = steady_clock::now();
        writeOutput(*buffer);
        buffer->clear();
        auto const diff = duration_cast<milliseconds>(steady_clock::now() - beginTime);

        results_.emplace_back(*test, diff, totalSizeBytes());

        test->teardown(*buffer);
        if (!buffer->empty())
        {
            writer_(buffer->output().data(), buffer->output().size());
            buffer->clear();
        }
    }
}
//
void Benchmark::summarize(std::ostream& os)
{
    os << std::format("All {} tests finished.\n", results_.size());
    os << std::format("---------------------\n\n");
    auto const gridCellCount = terminalSize_.columns * terminalSize_.lines;

    std::chrono::milliseconds totalTime {};
    size_t totalBytes = 0;
    for (auto const& result: results_)
    {
        Test const& test = result.test.get();
        auto const bps = double(result.bytesWritten) / (double(result.time.count()) / 1000.0);
        totalBytes += result.bytesWritten;
        totalTime += result.time;
        os << std::format("{:>40}: {:>3}.{:04} seconds, {}/s (normalized: {}/s)\n",
                          test.name,
                          result.time.count() / 1000,
                          result.time.count() % 1000,
                          sizeStr(bps),
                          sizeStr(bps / static_cast<double>(gridCellCount)));
    }

    auto const bps = double(totalBytes) / (double(totalTime.count()) / 1000.0);
    os << std::format("{:>40}: {:>3}.{:04} seconds, {}/s (normalized: {}/s)\n",
                      "all tests",
                      totalTime.count() / 1000,
                      totalTime.count() % 1000,
                      sizeStr(bps),
                      sizeStr(bps / static_cast<double>(gridCellCount)));
    os << "\n";
    os << std::format(" screen size: {}x{}\n", terminalSize_.columns, terminalSize_.lines);
    os << std::format("   data size: {}\n", sizeStr(static_cast<double>(testSizeMB_ * 1024 * 1024)));
}

} // namespace contour::termbench

namespace contour::termbench::tests
{

namespace
{

    static char randomAsciiChar()
    {
        auto constexpr Min = 'a'; // 0x20;
        auto constexpr Max = 'z'; // 0x7E;
        return static_cast<char>(Min + rand() % (Max - Min + 1));
    }

    void writeChar(Buffer& _sink, char ch)
    {
        _sink.write(std::string_view { &ch, 1 });
    }

    void writeNumber(Buffer& _sink, unsigned _value)
    {
        unsigned remains = _value;
        for (unsigned divisor = 1000000000; divisor != 0; divisor /= 10)
        {
            auto const digit = remains / divisor;
            remains -= digit * divisor;

            if (digit || (_value != remains) || (divisor == 1))
                writeChar(_sink, static_cast<char>('0' + digit));
        }
    }

    void moveCursor(Buffer& _sink, unsigned x, unsigned y)
    {
        writeChar(_sink, '\033');
        writeChar(_sink, '[');
        writeNumber(_sink, y);
        writeChar(_sink, ';');
        writeNumber(_sink, x);
        writeChar(_sink, 'H');
    }

    void setTextColor(Buffer& _sink, uint8_t r, uint8_t g, uint8_t b)
    {
        _sink.write("\033[38;2;");
        writeNumber(_sink, r & 0xFF);
        writeChar(_sink, ';');
        writeNumber(_sink, g & 0xFF);
        writeChar(_sink, ';');
        writeNumber(_sink, b & 0xFF);
        writeChar(_sink, 'm');
    }

    void setBackgroundColor(Buffer& _sink, uint8_t r, uint8_t g, uint8_t b)
    {
        _sink.write("\033[48;2;");
        writeNumber(_sink, r & 0xFF);
        writeChar(_sink, ';');
        writeNumber(_sink, g & 0xFF);
        writeChar(_sink, ';');
        writeNumber(_sink, b & 0xFF);
        writeChar(_sink, 'm');
    }

    class ManyLines: public Test
    {
      public:
        ManyLines() noexcept: Test("many_lines", "") {}

        void setup(TerminalSize) override
        {
            text.resize(4 * 1024 * 1024);
            for (auto i = text.data(), e = i + text.size(); i != e; ++i)
            {
                char const value = randomAsciiChar();
                if (value % 26 != 0)
                    *i = value;
                else
                    *i = '\n';
            }
        }

        void run(Buffer& _sink) noexcept override
        {
            while (_sink.good())
                _sink.write(text);
        }

      private:
        std::string text;
    };

    class LongLines: public Test
    {
      public:
        LongLines() noexcept: Test("long_lines", "") {}

        void run(Buffer& _sink) noexcept override
        {
            while (_sink.good())
            {
                writeChar(_sink, randomAsciiChar());
            }
        }
    };

    class SgrFgColoredText: public Test
    {
      public:
        SgrFgColoredText() noexcept: Test("sgr_fg_lines", "") {}

        TerminalSize terminalSize;

        void setup(TerminalSize size) noexcept override
        {
            terminalSize = size;
        }

        void run(Buffer& _sink) noexcept override
        {
            for (unsigned frameID = 0; _sink.good(); ++frameID)
            {
                for (u16 y = 0; y < terminalSize.lines; ++y)
                {
                    moveCursor(_sink, 1, y + 1u);
                    for (u16 x = 0; x < terminalSize.columns; ++x)
                    {
                        auto const r = frameID;
                        auto const g = frameID + y;
                        auto const b = frameID + y + x;

                        setTextColor(_sink, r & 0xff, g & 0xff, b & 0xff);
                        writeChar(_sink, static_cast<char>('a' + (frameID + x + y) % ('z' - 'a')));
                    }
                }
            }
        }
    };

    class SgrFgBgColoredText: public Test
    {
      public:
        SgrFgBgColoredText() noexcept: Test("sgr_fg_bg_lines", "") {}

        TerminalSize terminalSize;

        void setup(TerminalSize size) noexcept override
        {
            terminalSize = size;
        }

        void run(Buffer& _sink) noexcept override
        {
            for (unsigned frameID = 0; _sink.good(); ++frameID)
            {
                for (u16 y = 0; y < terminalSize.lines; ++y)
                {
                    moveCursor(_sink, 1, y + 1u);
                    for (u16 x = 0; x < terminalSize.columns; ++x)
                    {
                        auto r = static_cast<uint8_t>(frameID);
                        auto g = static_cast<uint8_t>(frameID + y);
                        auto b = static_cast<uint8_t>(frameID + y + x);
                        setTextColor(_sink, r, g, b);

                        r = static_cast<uint8_t>(frameID + y + x);
                        g = static_cast<uint8_t>(frameID + y);
                        b = static_cast<uint8_t>(frameID);
                        setBackgroundColor(_sink, r, g, b);

                        writeChar(_sink, static_cast<char>('a' + (frameID + x + y) % ('z' - 'a')));
                    }
                }
            }
        }
    };

    class Binary: public Test
    {
      public:
        Binary() noexcept: Test("binary", "") {}

        void setup(TerminalSize) override
        {
            text.resize(4 * 1024 * 1024);
            for (auto i = text.data(), e = i + text.size(); i != e; ++i)
            {
                char const value = randomAsciiChar();
                if (value % 26 != 0)
                    *i = value;
                else
                    *i = '\n';
            }
        }

        void run(Buffer& _sink) noexcept override
        {
            while (_sink.good())
            {
                _sink.write(text);
            }
        }

        void teardown(Buffer& _sink) noexcept override { _sink.write("\033c"); }

      private:
        std::string text;
    };

    class Line: public Test
    {
      public:
        Line(std::string name, std::string text): Test(name, ""), text { text } {}
        void setup(TerminalSize) override {}

        void run(Buffer& _sink) noexcept override
        {
            for (size_t i = 0; i < 1000; ++i)
            {
                while (_sink.good())
                    _sink.write(text);
            }
        }

      private:
        std::string text;
    };
} // namespace

std::unique_ptr<Test> many_lines()
{
    return std::make_unique<ManyLines>();
}

std::unique_ptr<Test> long_lines()
{
    return std::make_unique<LongLines>();
}

std::unique_ptr<Test> sgr_fg_lines()
{
    return std::make_unique<SgrFgColoredText>();
}

std::unique_ptr<Test> sgr_fgbg_lines()
{
    return std::make_unique<SgrFgBgColoredText>();
}

std::unique_ptr<Test> binary()
{
    return std::make_unique<Binary>();
}

std::unique_ptr<Test> ascii_line(size_t N)
{
    auto name = std::to_string(N) + " chars per line";
    auto text = std::string(N, 'a') + std::string { "\n" };
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> sgr_line(size_t N)
{
    auto name = std::to_string(N) + " chars with sgr per line";
    std::string text {};
    text += std::string { "\033[38;2;20;200;200m" };
    text += std::string(N, 'a');
    text += std::string { "\n" };
    text += std::string { "\033[38;2;255;255;255m" };
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> sgrbg_line(size_t N)
{
    auto name = std::to_string(N) + " chars with sgr and bg per line";
    std::string text {};
    text += std::string { "\033[38;2;20;200;200m\033[48;2;100;100;100m" };
    text += std::string(N, 'a');
    text += std::string { "\033[38;2;255;255;255m\033[48;2;0;0;0m" };
    text += std::string { "\n" };
    return std::make_unique<Line>(name, text);
}

} // namespace contour::termbench::tests
