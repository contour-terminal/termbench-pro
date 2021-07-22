/**
 * This file is part of the "terminal-pro" project
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

#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string_view>
#include <vector>

namespace contour::termbench
{

struct Buffer
{
public:
    explicit Buffer(size_t _maxWriteSizeMB) noexcept:
        maxWriteSize{_maxWriteSizeMB * 1024 * 1024}
    {}

    bool good() const noexcept
    {
        return nwritten < maxWriteSize;
    }

    void write(std::string_view _data) noexcept
    {
        auto const n = nwritten + _data.size() < maxWriteSize
                     ? _data.size()
                     : maxWriteSize - nwritten;
        auto i = _data.data();
        auto p = data.data() + nwritten;
        auto e = data.data() + nwritten + n;
        while (p != e)
            *p++ = *i++;
        // std::memcpy(data.data() + nwritten, _data.data(), n);
        nwritten += n;
    }

    std::string_view output() const noexcept { return std::string_view{data.data(), nwritten}; }

    void clear() noexcept { nwritten = 0; }
    bool empty() const noexcept { return nwritten == 0; }

private:
    size_t maxWriteSize = 4 * 1024 * 1024;

    std::array<char, 64 * 1024 * 1024> data{};
    size_t nwritten = 0;
};

/// Describes a single test.
struct Test
{
    std::string_view name;
    std::string_view description;

    virtual ~Test() = default;

    Test(std::string_view _name, std::string_view _description) noexcept:
        name{_name},
        description{_description}
    {}

    virtual void setup(unsigned short, unsigned short) noexcept {}
    virtual void run(Buffer&) noexcept = 0;
    virtual void teardown(Buffer&) noexcept {}
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
              unsigned short _width,
              unsigned short _height,
              std::function<void(Test const&)> _beforeTest = {});

    void add(std::unique_ptr<Test> _test);

    void runAll();

    void summarize(std::ostream& os);

    std::vector<Result> const& results() const noexcept { return results_; }

private:
    std::function<void(char const*, size_t)> writer_;
    std::function<void(Test const&)> beforeTest_;
    size_t testSizeMB_;
    unsigned short width_;
    unsigned short height_;

    std::vector<std::unique_ptr<Test>> tests_;
    std::vector<Result> results_;
};

}

// Holds a set of pre-defined terminal benchmark tests.
namespace contour::termbench::tests
{
    std::unique_ptr<Test> many_lines();
    std::unique_ptr<Test> long_lines();
    std::unique_ptr<Test> sgr_fg_lines();
    std::unique_ptr<Test> sgr_fgbg_lines();
    std::unique_ptr<Test> binary();
}
