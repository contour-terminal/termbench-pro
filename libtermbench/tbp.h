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

#ifndef __LIBTERMBENCHPRO_TBP_H
#define __LIBTERMBENCHPRO_TBP_H 1

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct tbp_benchmark tbp_benchmark_t;
typedef struct tbp_buffer tbp_buffer_t;
typedef struct tbp_testcase tbp_testcase_t;

typedef void (*tbp_testcase_syn_fn)(void* /*user*/, unsigned short _columns, unsigned short _lines);
typedef void (*tbp_testcase_run_fn)(void* /*user*/, tbp_buffer_t* /*sink*/);
typedef void (*tbp_testcase_fin_fn)(void* /*user*/, unsigned short _columns, unsigned short _lines);

typedef void (*tbp_writer_fn)(void* /*user*/, char const* /*_data*/, size_t /*_len*/);
typedef void (*tbp_pretest_fn)(void* /*user*/, tbp_testcase_t const* /*_testcase*/);

/* XXX buffer API */

/** appends a new chunk into the buffer. */
void tbp_buffer_write(tbp_buffer_t* _buffer, char const* _data, size_t _len);

/** Retrieves the size in bytes of the given buffer. */
size_t tbp_buffer_size(tbp_buffer_t* _buffer);

/** Tests if the buffer is still good to be written to.  */
int tbp_buffer_good(tbp_buffer_t* _buffer);

/* XXX test case API */

int tbp_testcase_name_get(tbp_testcase_t* _test, char* _buffer, size_t _bufferCapacity);

int tbp_testcase_description_get(tbp_testcase_t* _test, char* _buffer, size_t _bufferCapacity);

/* XXX benchmark API */

tbp_benchmark_t* tbp_benchmark_create(tbp_writer_fn _writer,
                                      size_t _testSizeMB,
                                      unsigned short _columns,
                                      unsigned short _lines);

void tbp_benchmark_text_add(tbp_benchmark_t* _bench,
                            char const* _name,
                            char const* _description,
                            tbp_testcase_syn_fn _init,
                            tbp_testcase_run_fn _run,
                            tbp_testcase_fin_fn _fini);

void tbp_benchmark_run_all(tbp_benchmark_t* _benchmark);

void tbp_benchmark_summarize(tbp_benchmark_t* _benchmark,
                             tbp_writer_fn _output);

void tbp_benchmark_destroy(tbp_benchmark_t* _bench);

#if defined(__cplusplus)
}
#endif

#endif
