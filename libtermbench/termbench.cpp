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

#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <ostream>
#include <utility>

using namespace std::chrono;
using namespace std::string_view_literals;

namespace termbench
{

#if defined(_MSC_VER)
    #define leadingZeroBits(x) __lzcnt(x)
#else
    #define leadingZeroBits(x) __builtin_clz(x)
#endif

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

void Benchmark::updateWindowTitle(std::string_view _title)
{
    auto const now = steady_clock::now();
    if (now - lastWindowTitleUpdate_ < 100ms)
        return;

    lastWindowTitleUpdate_ = now;
    std::cout << std::format("\033]2;{}\033\\", _title);
    std::cout.flush();
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

        while (buffer->good())
            test->fill(*buffer);

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

void Benchmark::summarizeToJson(std::ostream& os)
{
    std::string buffer = glz::write_json(results_).value_or("error");
    os << buffer;
}

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

} // namespace termbench

namespace termbench::tests
{

namespace
{

    static char randomAsciiChar() noexcept
    {
        auto constexpr Min = 'a'; // 0x20;
        auto constexpr Max = 'z'; // 0x7E;

        // Knuth's MMIX
        static uint64_t state = 1442695040888963407;
        const auto v = state * 6364136223846793005 + 1442695040888963407;
        state = v;

        return static_cast<char>(Min + v % (Max - Min + 1));
    }

    void writeChar(Buffer& _sink, char ch)
    {
        _sink.write(std::string_view { &ch, 1 });
    }

    void writeNumber(Buffer& _sink, unsigned v)
    {
        // This implements https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10 but with lzcnt
        // for log2.
        static constexpr uint32_t powers_of_10[] {
            0, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
        };
        const auto t = (32 - leadingZeroBits(v | 1)) * 1233 >> 12;
        const auto log10 = t - (v < powers_of_10[t]);

        // Mapping 2 digits at a time speeds things up a lot because half the divisions are necessary.
        // I got this idea from https://github.com/fmtlib/fmt which in turn got it
        // from the talk "Three Optimization Tips for C++" by Andrei Alexandrescu.
        static constexpr auto lut = "0001020304050607080910111213141516171819"
                                    "2021222324252627282930313233343536373839"
                                    "4041424344454647484950515253545556575859"
                                    "6061626364656667686970717273747576777879"
                                    "8081828384858687888990919293949596979899";

        char buffer[16];
        const auto digits = log10 + 1;
        auto p = &buffer[digits];
        auto r = digits;

        while (r > 1)
        {
            const auto s = &lut[(v % 100) * 2];
            *--p = s[1];
            *--p = s[0];
            v /= 100;
            r -= 2;
        }
        if (r & 1)
        {
            *--p = static_cast<char>('0' + v);
        }

        _sink.write({ &buffer[0], static_cast<size_t>(digits) });
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

    class CraftedTest: public Test
    {
      public:
        explicit CraftedTest(std::string name, std::string description, std::string text) noexcept:
            Test(std::move(name), std::move(description)), _text { std::move(text) }
        {
        }

        void fill(Buffer& _sink) noexcept override { _sink.write(_text); }

      private:
        std::string _text;
    };

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

        void fill(Buffer& _sink) noexcept override { _sink.write(text); }

      private:
        std::string text;
    };

    class LongLines: public Test
    {
      public:
        LongLines() noexcept: Test("long_lines", "") {}

        void fill(Buffer& _sink) noexcept override { writeChar(_sink, randomAsciiChar()); }
    };

    class SgrFgColoredText: public Test
    {
      public:
        SgrFgColoredText() noexcept: Test("sgr_fg_lines", "") {}

        TerminalSize terminalSize;
        unsigned frameID = 0;

        void setup(TerminalSize size) noexcept override { terminalSize = size; }

        void fill(Buffer& _sink) noexcept override
        {
            ++frameID;
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
    };

    class SgrFgBgColoredText: public Test
    {
      public:
        SgrFgBgColoredText() noexcept: Test("sgr_fg_bg_lines", "") {}

        TerminalSize terminalSize;
        unsigned frameID = 0;

        void setup(TerminalSize size) noexcept override { terminalSize = size; }

        void fill(Buffer& _sink) noexcept override
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

        void fill(Buffer& _sink) noexcept override { _sink.write(text); }

        void teardown(Buffer& _sink) noexcept override { _sink.write("\033c"); }

      private:
        std::string text;
    };

    class Line: public Test
    {
      public:
        Line(std::string name, std::string text): Test(name, ""), text { text } {}
        void setup(TerminalSize) override {}

        void fill(Buffer& _sink) noexcept override { _sink.write(text); }

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

std::unique_ptr<Test> ascii_line(size_t line_length)
{
    auto name = std::to_string(line_length) + " chars per line";
    auto text = std::string(line_length, 'a') + std::string { "\n" };
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> sgr_line(size_t line_length)
{
    auto name = std::to_string(line_length) + " chars with sgr per line";
    std::string text {};
    text += "\033[38;2;20;200;200m";
    text += std::string(line_length, 'a');
    text += "\n";
    text += "\033[38;2;255;255;255m";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> sgrbg_line(size_t line_length)
{
    auto name = std::to_string(line_length) + " chars with sgr and bg per line";
    std::string text {};
    text += "\033[38;2;20;200;200m\033[48;2;100;100;100m";
    text += std::string(line_length, 'a');
    text += "\033[38;2;255;255;255m\033[48;2;0;0;0m";
    text += "\n";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_simple(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode simple";
    std::string text {};
    for (size_t i = 0; i < line_length; ++i)
        text += "\u0061";
    text += "\n";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_two_codepoints(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode diacritic";
    std::string text {};
    for (size_t i = 0; i < line_length; ++i)
        text += "\u0061\u0308";
    text += "\n";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_three_codepoints(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode double diacritic";
    std::string text {};
    for (size_t i = 0; i < static_cast<size_t>(line_length / 2); ++i)
        text += "\u0061\u035D\u0062";
    text += "\n";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_fire_as_text(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode fire as text";
    std::string text {};
    for (size_t i = 0; i < static_cast<size_t>(line_length / 2); ++i)
        text += std::string { "\U0001F525\U0000FE0E" };
    text += std::string { "\n" };
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_fire(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode fire";
    std::string text {};
    for (size_t i = 0; i < static_cast<size_t>(line_length / 2); ++i)
        text += std::string { "\U0001F525" };
    text += std::string { "\n" };
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> unicode_flag(size_t line_length)
{
    auto name = std::to_string(line_length) + " unicode flag";
    std::string text {};
    std::string flag {};
    flag += "\U0001F3F4";
    flag += "\U000E0067";
    flag += "\U000E0062";
    flag += "\U000E0065";
    flag += "\U000E006E";
    flag += "\U000E0067";
    flag += "\U000E007F";

    for (size_t i = 0; i < static_cast<size_t>(line_length / 2); ++i)
        text += flag;
    text += "\n";
    return std::make_unique<Line>(name, text);
}

std::unique_ptr<Test> crafted(std::string name, std::string description, std::string text)
{
    return std::make_unique<CraftedTest>(std::move(name), std::move(description), std::move(text));
}

} // namespace termbench::tests
