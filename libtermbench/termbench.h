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

#include <chrono>
#include <cstring>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

namespace termbench
{

struct TerminalSize
{
    unsigned short columns = 0;
    unsigned short lines = 0;

    constexpr auto operator<=>(TerminalSize const&) const noexcept = default;
};

struct Buffer
{
  public:
    explicit Buffer(size_t _maxWriteSizeMB) noexcept: _maxSize { _maxWriteSizeMB * 1024 * 1024 } {}

    bool good() const noexcept { return _data.size() < _maxSize; }

    bool write(std::string_view chunk) noexcept
    {
        if (_data.size() > _maxSize)
            return false;
        _data.append(chunk.data(), chunk.size());
        return true;
    }

    std::string_view output() const noexcept { return std::string_view { _data.data(), _data.size() }; }

    void clear() noexcept { _data.clear(); }
    bool empty() const noexcept { return _data.empty(); }
    size_t size() const noexcept { return _data.size(); }

  private:
    std::string _data;
    std::size_t _maxSize;
};

/// Describes a single test.
struct Test
{
    std::string name;
    std::string description;

    virtual ~Test() = default;

    Test(std::string _name, std::string _description) noexcept: name { _name }, description { _description }
    {
    }

    virtual void setup(TerminalSize /*terminalSize*/) {}
    virtual void fill(Buffer& /*stdoutBuffer*/) noexcept = 0;
    virtual void teardown(Buffer& /*stdoutBuffer*/) {}
};

struct Result
{
    std::reference_wrapper<Test> test;
    std::chrono::milliseconds time;
    size_t bytesWritten;
};

class Benchmark
{
  public:
    Benchmark(std::function<void(char const*, size_t n)> _writer,
              size_t _testSizeMB,
              TerminalSize terminalSize,
              std::function<void(Test const&)> _beforeTest = {});

    void add(std::unique_ptr<Test> _test);

    void runAll();

    void summarize(std::ostream& os);
    void summarizeToJson(std::ostream& os);

    std::vector<Result> const& results() const noexcept { return results_; }

    constexpr size_t totalSizeBytes() const noexcept { return testSizeMB_ * 1024 * 1024; }

  private:
    void writeOutput(Buffer const& testBuffer);
    void updateWindowTitle(std::string_view _title);

    std::function<void(char const*, size_t)> writer_;
    std::function<void(Test const&)> beforeTest_;
    size_t testSizeMB_;
    TerminalSize terminalSize_;
    std::chrono::steady_clock::time_point lastWindowTitleUpdate_;

    std::vector<std::unique_ptr<Test>> tests_;
    std::vector<Result> results_;
};

inline std::string sizeStr(double _value)
{
    if ((long double) (_value) >= (1024ull * 1024ull * 1024ull)) // GB
        return std::format("{:7.3f} GB", _value / 1024.0 / 1024.0 / 1024.0);
    if (_value >= (1024 * 1024)) // MB
        return std::format("{:7.3f} MB", _value / 1024.0 / 1024.0);
    if (_value >= 1024) // KB
        return std::format("{:7.3f} KB", _value / 1024.0);
    return std::format("{:7.3f} bytes", _value);
}

} // namespace termbench

namespace glz
{

template <>
struct meta<termbench::Result>
{
    using T = termbench::Result;
    static constexpr auto value = glz::object(
        "name",
        [](T const& result) { return result.test.get().name; },
        "bytes written",
        &T::bytesWritten,
        "time",
        [](T const& result) { return result.time.count(); },
        "MB/s",
        [](T const& result) {
            double bytesPerSecond = double(result.bytesWritten) / (double(result.time.count()) / 1000.0);
            return bytesPerSecond / 1024.0 / 1024.0;
        });
};
} // namespace glz

// Holds a set of pre-defined terminal benchmark tests.
namespace termbench::tests
{
std::unique_ptr<Test> many_lines();
std::unique_ptr<Test> long_lines();
std::unique_ptr<Test> sgr_fg_lines();
std::unique_ptr<Test> sgr_fgbg_lines();
std::unique_ptr<Test> binary();
std::unique_ptr<Test> ascii_line(size_t);
std::unique_ptr<Test> sgr_line(size_t);
std::unique_ptr<Test> sgrbg_line(size_t);
std::unique_ptr<Test> unicode_simple(size_t);
std::unique_ptr<Test> unicode_two_codepoints(size_t);
std::unique_ptr<Test> unicode_three_codepoints(size_t);
std::unique_ptr<Test> unicode_flag(size_t);
std::unique_ptr<Test> unicode_fire_as_text(size_t); // U+FEOE
std::unique_ptr<Test> unicode_fire(size_t);
std::unique_ptr<Test> crafted(std::string name, std::string description, std::string text);
} // namespace termbench::tests
