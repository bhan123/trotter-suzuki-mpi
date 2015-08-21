/**
 * Distributed Trotter-Suzuki solver
 * Copyright (C) 2012 Peter Wittek, 2010-2012 Carlos Bederián
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
 
#include <cassert>

#include <mpi.h>

#include "common.h"
#include "cpublocksse.h"

/***************
* SSE variants *
***************/
template <int offset_y>
static inline void update_shifty_sse(size_t stride, size_t width, size_t height, float a, float b, float * __restrict__ r1, float * __restrict__ i1, float * __restrict__ r2, float * __restrict__ i2) {
    __m128 aq, bq;
    aq = _mm_load1_ps(&a);
    bq = _mm_load1_ps(&b);

    for (int i = 0; i < height - offset_y; i++) {
        int idx1 = i * stride;
        int idx2 = (i+offset_y) * stride;
        int j = 0;
        for (; j < width - width % 4; j += 4, idx1 += 4, idx2 += 4) {
            __m128 r1q = _mm_load_ps(&r1[idx1]);
            __m128 i1q = _mm_load_ps(&i1[idx1]);
            __m128 r2q = _mm_load_ps(&r2[idx2]);
            __m128 i2q = _mm_load_ps(&i2[idx2]);
            __m128 next_r1q = _mm_sub_ps(_mm_mul_ps(r1q, aq), _mm_mul_ps(i2q, bq));
            __m128 next_i1q = _mm_add_ps(_mm_mul_ps(i1q, aq), _mm_mul_ps(r2q, bq));
            __m128 next_r2q = _mm_sub_ps(_mm_mul_ps(r2q, aq), _mm_mul_ps(i1q, bq));
            __m128 next_i2q = _mm_add_ps(_mm_mul_ps(i2q, aq), _mm_mul_ps(r1q, bq));
            _mm_store_ps(&r1[idx1], next_r1q);
            _mm_store_ps(&i1[idx1], next_i1q);
            _mm_store_ps(&r2[idx2], next_r2q);
            _mm_store_ps(&i2[idx2], next_i2q);
        }
        for (; j < width; ++j, ++idx1, ++idx2) {
            float next_r1 = a * r1[idx1] - b * i2[idx2];
            float next_i1 = a * i1[idx1] + b * r2[idx2];
            float next_r2 = a * r2[idx2] - b * i1[idx1];
            float next_i2 = a * i2[idx2] + b * r1[idx1];
            r1[idx1] = next_r1;
            i1[idx1] = next_i1;
            r2[idx2] = next_r2;
            i2[idx2] = next_i2;
        }
    }
}

template <int offset_x>
static inline void update_shiftx_sse(size_t stride, size_t width, size_t height, float a, float b, float * __restrict__ r1, float * __restrict__ i1, float * __restrict__ r2, float * __restrict__ i2) {
    __m128 aq, bq;
    aq = _mm_load1_ps(&a);
    bq = _mm_load1_ps(&b);
    for (int i = 0; i < height; i++) {
        int idx1 = i * stride;
        int idx2 = i * stride + offset_x;
        int j = 0;
        for (; j < width - offset_x - (width - offset_x) % 4; j += 4, idx1 += 4, idx2 += 4) {
            __m128 r1q = _mm_load_ps(&r1[idx1]);
            __m128 i1q = _mm_load_ps(&i1[idx1]);
            __m128 r2q;
            __m128 i2q;
            if (offset_x == 0) {
                r2q = _mm_load_ps(&r2[idx2]);
                i2q = _mm_load_ps(&i2[idx2]);
            } else {
                r2q = _mm_loadu_ps(&r2[idx2]);
                i2q = _mm_loadu_ps(&i2[idx2]);
            }
            __m128 next_r1q = _mm_sub_ps(_mm_mul_ps(r1q, aq), _mm_mul_ps(i2q, bq));
            __m128 next_i1q = _mm_add_ps(_mm_mul_ps(i1q, aq), _mm_mul_ps(r2q, bq));
            __m128 next_r2q = _mm_sub_ps(_mm_mul_ps(r2q, aq), _mm_mul_ps(i1q, bq));
            __m128 next_i2q = _mm_add_ps(_mm_mul_ps(i2q, aq), _mm_mul_ps(r1q, bq));
            _mm_store_ps(&r1[idx1], next_r1q);
            _mm_store_ps(&i1[idx1], next_i1q);
            if (offset_x == 0) {
                _mm_store_ps(&r2[idx2], next_r2q);
                _mm_store_ps(&i2[idx2], next_i2q);
            } else {
                _mm_storeu_ps(&r2[idx2], next_r2q);
                _mm_storeu_ps(&i2[idx2], next_i2q);
            }
        }
        for (; j < width - offset_x; ++j, ++idx1, ++idx2) {
            float next_r1 = a * r1[idx1] - b * i2[idx2];
            float next_i1 = a * i1[idx1] + b * r2[idx2];
            float next_r2 = a * r2[idx2] - b * i1[idx1];
            float next_i2 = a * i2[idx2] + b * r1[idx1];
            r1[idx1] = next_r1;
            i1[idx1] = next_i1;
            r2[idx2] = next_r2;
            i2[idx2] = next_i2;
        }
    }
}

static void full_step_sse(size_t stride, size_t width, size_t height, float a, float b, float * r00, float * r01, float * r10, float * r11, float * i00, float * i01, float * i10, float * i11) {
    // 1
    update_shifty_sse<0>(stride, width, height, a, b, r00, i00, r10, i10);
    update_shifty_sse<1>(stride, width, height, a, b, r11, i11, r01, i01);
    // 2
    update_shiftx_sse<0>(stride, width, height, a, b, r00, i00, r01, i01);
    update_shiftx_sse<1>(stride, width, height, a, b, r11, i11, r10, i10);
    // 3
    update_shifty_sse<0>(stride, width, height, a, b, r01, i01, r11, i11);
    update_shifty_sse<1>(stride, width, height, a, b, r10, i10, r00, i00);
    // 4
    update_shiftx_sse<0>(stride, width, height, a, b, r10, i10, r11, i11);
    update_shiftx_sse<1>(stride, width, height, a, b, r01, i01, r00, i00);
    // 4
    update_shiftx_sse<0>(stride, width, height, a, b, r10, i10, r11, i11);
    update_shiftx_sse<1>(stride, width, height, a, b, r01, i01, r00, i00);
    // 3
    update_shifty_sse<0>(stride, width, height, a, b, r01, i01, r11, i11);
    update_shifty_sse<1>(stride, width, height, a, b, r10, i10, r00, i00);
    // 2
    update_shiftx_sse<0>(stride, width, height, a, b, r00, i00, r01, i01);
    update_shiftx_sse<1>(stride, width, height, a, b, r11, i11, r10, i10);
    // 1
    update_shifty_sse<0>(stride, width, height, a, b, r00, i00, r10, i10);
    update_shifty_sse<1>(stride, width, height, a, b, r11, i11, r01, i01);
}

static void process_sides_sse(size_t read_y, size_t read_height, size_t write_offset, size_t write_height, size_t block_width, size_t block_height, size_t tile_width, size_t halo_x, float a, float b, const float * r00, const float * r01, const float * r10, const float * r11, const float * i00, const float * i01, const float * i10, const float * i11, float * next_r00, float * next_r01, float * next_r10, float * next_r11, float * next_i00, float * next_i01, float * next_i10, float * next_i11, float * block_r00, float * block_r01, float * block_r10, float * block_r11, float * block_i00, float * block_i01, float * block_i10, float * block_i11) {
    size_t read_idx;
    size_t read_width;
    size_t block_read_idx;
    size_t write_idx;
    size_t write_width;

    size_t block_stride = (block_width / 2) * sizeof(float);
    size_t matrix_stride = (tile_width / 2) * sizeof(float);

    // First block [0..block_width - halo_x]
    read_idx = (read_y / 2) * (tile_width / 2);
    read_width = (block_width / 2) * sizeof(float);
    memcpy2D(block_r00, block_stride, &r00[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i00, block_stride, &i00[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r01, block_stride, &r01[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i01, block_stride, &i01[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r10, block_stride, &r10[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i10, block_stride, &i10[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r11, block_stride, &r11[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i11, block_stride, &i11[read_idx], matrix_stride, read_width, read_height / 2);

    full_step_sse(block_width / 2, block_width / 2, read_height / 2, a, b, block_r00, block_r01, block_r10, block_r11, block_i00, block_i01, block_i10, block_i11);
    
    block_read_idx = (write_offset / 2) * (block_width / 2);
    write_idx = (read_y / 2 + write_offset / 2) * (tile_width / 2);
    write_width = ((block_width - halo_x) / 2) * sizeof(float);
    memcpy2D(&next_r00[write_idx], matrix_stride, &block_r00[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i00[write_idx], matrix_stride, &block_i00[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r01[write_idx], matrix_stride, &block_r01[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i01[write_idx], matrix_stride, &block_i01[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r10[write_idx], matrix_stride, &block_r10[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i10[write_idx], matrix_stride, &block_i10[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r11[write_idx], matrix_stride, &block_r11[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i11[write_idx], matrix_stride, &block_i11[block_read_idx], block_stride, write_width, write_height / 2);

    // Last block
    size_t block_start=((tile_width-block_width)/(block_width - 2 * halo_x)+1)*(block_width - 2 * halo_x);
    read_idx = (read_y / 2) * (tile_width / 2) + block_start / 2;
    read_width = (tile_width / 2 - block_start / 2) * sizeof(float);
    memcpy2D(block_r00, block_stride, &r00[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i00, block_stride, &i00[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r01, block_stride, &r01[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i01, block_stride, &i01[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r10, block_stride, &r10[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i10, block_stride, &i10[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_r11, block_stride, &r11[read_idx], matrix_stride, read_width, read_height / 2);
    memcpy2D(block_i11, block_stride, &i11[read_idx], matrix_stride, read_width, read_height / 2);

    full_step_sse(block_width / 2, tile_width / 2 - block_start / 2, read_height / 2, a, b, block_r00, block_r01, block_r10, block_r11, block_i00, block_i01, block_i10, block_i11);

    block_read_idx = (write_offset / 2) * (block_width / 2) + halo_x / 2;
    write_idx = (read_y / 2 + write_offset / 2) * (tile_width / 2) + (block_start + halo_x) / 2;
    write_width = (tile_width / 2 - block_start / 2 - halo_x / 2) * sizeof(float);
    memcpy2D(&next_r00[write_idx], matrix_stride, &block_r00[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i00[write_idx], matrix_stride, &block_i00[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r01[write_idx], matrix_stride, &block_r01[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i01[write_idx], matrix_stride, &block_i01[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r10[write_idx], matrix_stride, &block_r10[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i10[write_idx], matrix_stride, &block_i10[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_r11[write_idx], matrix_stride, &block_r11[block_read_idx], block_stride, write_width, write_height / 2);
    memcpy2D(&next_i11[write_idx], matrix_stride, &block_i11[block_read_idx], block_stride, write_width, write_height / 2);
}

static void process_band_sse(size_t read_y, size_t read_height, size_t write_offset, size_t write_height, size_t block_width, size_t block_height, size_t tile_width, size_t halo_x, float a, float b, const float * r00, const float * r01, const float * r10, const float * r11, const float * i00, const float * i01, const float * i10, const float * i11, float * next_r00, float * next_r01, float * next_r10, float * next_r11, float * next_i00, float * next_i01, float * next_i10, float * next_i11, int inner, int sides) {
    float block_r00[(block_height / 2) * (block_width / 2)];
    float block_r01[(block_height / 2) * (block_width / 2)];
    float block_r10[(block_height / 2) * (block_width / 2)];
    float block_r11[(block_height / 2) * (block_width / 2)];
    float block_i00[(block_height / 2) * (block_width / 2)];
    float block_i01[(block_height / 2) * (block_width / 2)];
    float block_i10[(block_height / 2) * (block_width / 2)];
    float block_i11[(block_height / 2) * (block_width / 2)];
        
    size_t read_idx;
    size_t read_width;
    size_t block_read_idx;
    size_t write_idx;
    size_t write_width;

    size_t block_stride = (block_width / 2) * sizeof(float);
    size_t matrix_stride = (tile_width / 2) * sizeof(float);

    if (tile_width <= block_width) {
        if (sides) {
            // One full block
            read_idx = (read_y / 2) * (tile_width / 2);
            read_width = (tile_width / 2) * sizeof(float);
            memcpy2D(block_r00, block_stride, &r00[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_i00, block_stride, &i00[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_r01, block_stride, &r01[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_i01, block_stride, &i01[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_r10, block_stride, &r10[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_i10, block_stride, &i10[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_r11, block_stride, &r11[read_idx], matrix_stride, read_width, read_height / 2);
            memcpy2D(block_i11, block_stride, &i11[read_idx], matrix_stride, read_width, read_height / 2);

            full_step_sse(block_width / 2, tile_width / 2, read_height / 2, a, b, block_r00, block_r01, block_r10, block_r11, block_i00, block_i01, block_i10, block_i11);

            block_read_idx = (write_offset / 2) * (block_width / 2);
            write_idx = (read_y / 2 + write_offset / 2) * (tile_width / 2);
            write_width = read_width;
            memcpy2D(&next_r00[write_idx], matrix_stride, &block_r00[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_i00[write_idx], matrix_stride, &block_i00[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_r01[write_idx], matrix_stride, &block_r01[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_i01[write_idx], matrix_stride, &block_i01[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_r10[write_idx], matrix_stride, &block_r10[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_i10[write_idx], matrix_stride, &block_i10[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_r11[write_idx], matrix_stride, &block_r11[block_read_idx], block_stride, write_width, write_height / 2);
            memcpy2D(&next_i11[write_idx], matrix_stride, &block_i11[block_read_idx], block_stride, write_width, write_height / 2);
        }
    } else {
        if (sides) {
            process_sides_sse(read_y, read_height, write_offset, write_height, block_width, block_height, tile_width, halo_x, a, b, r00, r01, r10, r11, i00, i01, i10, i11, next_r00, next_r01, next_r10, next_r11, next_i00, next_i01, next_i10, next_i11, block_r00, block_r01, block_r10, block_r11, block_i00, block_i01, block_i10, block_i11);
        }
        if (inner) {
            // Regular blocks in the middle
            size_t block_start;
            read_width = (block_width / 2) * sizeof(float);
            block_read_idx = (write_offset / 2) * (block_width / 2) + halo_x / 2;
            write_width = ((block_width - 2 * halo_x) / 2) * sizeof(float);
            for (block_start = block_width - 2 * halo_x; block_start < tile_width - block_width; block_start += block_width - 2 * halo_x) {
                read_idx = (read_y / 2) * (tile_width / 2) + block_start / 2;
                memcpy2D(block_r00, block_stride, &r00[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_i00, block_stride, &i00[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_r01, block_stride, &r01[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_i01, block_stride, &i01[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_r10, block_stride, &r10[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_i10, block_stride, &i10[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_r11, block_stride, &r11[read_idx], matrix_stride, read_width, read_height / 2);
                memcpy2D(block_i11, block_stride, &i11[read_idx], matrix_stride, read_width, read_height / 2);

                full_step_sse(block_width / 2, block_width / 2, read_height / 2, a, b, block_r00, block_r01, block_r10, block_r11, block_i00, block_i01, block_i10, block_i11);

                write_idx = (read_y / 2 + write_offset / 2) * (tile_width / 2) + (block_start + halo_x) / 2;
                memcpy2D(&next_r00[write_idx], matrix_stride, &block_r00[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_i00[write_idx], matrix_stride, &block_i00[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_r01[write_idx], matrix_stride, &block_r01[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_i01[write_idx], matrix_stride, &block_i01[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_r10[write_idx], matrix_stride, &block_r10[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_i10[write_idx], matrix_stride, &block_i10[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_r11[write_idx], matrix_stride, &block_r11[block_read_idx], block_stride, write_width, write_height / 2);
                memcpy2D(&next_i11[write_idx], matrix_stride, &block_i11[block_read_idx], block_stride, write_width, write_height / 2);
            }
        }
    }
}

CPUBlockSSEKernel::CPUBlockSSEKernel(float *_p_real, float *_p_imag, float _a, float _b, int _tile_width, int _tile_height, int _halo_x, int _halo_y):
    p_real(_p_real),
    p_imag(_p_imag),
    a(_a),
    b(_b),
    tile_width(_tile_width),
    tile_height(_tile_height),
    halo_x(_halo_x),
    halo_y(_halo_y),
    sense(0)
{
    assert (tile_width % 2 == 0);
    assert (tile_height % 2 == 0);

    posix_memalign(reinterpret_cast<void**>(&r00[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r00[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r01[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r01[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r10[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r10[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r11[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&r11[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i00[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i00[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i01[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i01[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i10[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i10[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i11[0]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    posix_memalign(reinterpret_cast<void**>(&i11[1]), 64, ((tile_width * tile_height) / 4) * sizeof(float));
    for (int i = 0; i < tile_height/2; i++) {
        for (int j = 0; j < tile_width/2; j++) {
            r00[0][i * tile_width / 2 + j] = p_real[2 * i * tile_width + 2 * j];
            i00[0][i * tile_width / 2 + j] = p_imag[2 * i * tile_width + 2 * j];
            r01[0][i * tile_width / 2 + j] = p_real[2 * i * tile_width + 2 * j + 1];
            i01[0][i * tile_width / 2 + j] = p_imag[2 * i * tile_width + 2 * j + 1];
        }
        for (int j = 0; j < tile_width/2; j++) {
            r10[0][i * tile_width / 2 + j] = p_real[(2 * i + 1) * tile_width + 2 * j];
            i10[0][i * tile_width / 2 + j] = p_imag[(2 * i + 1) * tile_width + 2 * j];
            r11[0][i * tile_width / 2 + j] = p_real[(2 * i + 1) * tile_width + 2 * j + 1];
            i11[0][i * tile_width / 2 + j] = p_imag[(2 * i + 1) * tile_width + 2 * j + 1];
        }
    }
}

CPUBlockSSEKernel::~CPUBlockSSEKernel() {

    free(r00[0]);
    free(r00[1]);
    free(r01[0]);
    free(r01[1]);
    free(r10[0]);
    free(r10[1]);
    free(r11[0]);
    free(r11[1]);
    free(i00[0]);
    free(i00[1]);
    free(i01[0]);
    free(i01[1]);
    free(i10[0]);
    free(i10[1]);
    free(i11[0]);
    free(i11[1]);
}


void CPUBlockSSEKernel::run_kernel_on_halo() {
    int inner=0, sides=0;
    if (tile_height <= BLOCK_HEIGHT) {
        // One full band
        inner=1; sides=1;
        process_band_sse(0, tile_height, 0, tile_height, block_width, block_height, tile_width, halo_x, a, b, r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], r00[1-sense], r01[1-sense], r10[1-sense], r11[1-sense], i00[1-sense], i01[1-sense], i10[1-sense], i11[1-sense], inner, sides);
    } else {
       
        // Sides
        inner=0; sides=1;
        size_t block_start;
        for (block_start = BLOCK_HEIGHT - 2 * halo_y; block_start < tile_height - BLOCK_HEIGHT; block_start += BLOCK_HEIGHT - 2 * halo_y) {
            process_band_sse(block_start, block_height, halo_y, block_height - 2 * halo_y, block_width, block_height, tile_width, halo_x, a, b, r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], r00[1-sense], r01[1-sense], r10[1-sense], r11[1-sense], i00[1-sense], i01[1-sense], i10[1-sense], i11[1-sense], inner, sides);
        }
        
        // First band
        inner=1; sides=1;
        process_band_sse(0, block_height, 0, block_height - halo_y, block_width, block_height, tile_width, halo_x, a, b, r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], r00[1-sense], r01[1-sense], r10[1-sense], r11[1-sense], i00[1-sense], i01[1-sense], i10[1-sense], i11[1-sense], inner, sides);

        // Last band
        inner=1; sides=1;
        process_band_sse(block_start, tile_height - block_start, halo_y, tile_height - block_start - halo_y, block_width, block_height, tile_width, halo_x, a, b, r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], r00[1-sense], r01[1-sense], r10[1-sense], r11[1-sense], i00[1-sense], i01[1-sense], i10[1-sense], i11[1-sense], inner, sides);
  }

}

void CPUBlockSSEKernel::run_kernel() {
    int inner=1, sides=0;
    for (size_t block_start = block_height - 2 * halo_y; block_start < tile_height - block_height; block_start += block_height - 2 * halo_y) {
        process_band_sse(block_start, block_height, halo_y, block_height - 2 * halo_y, block_width, block_height, tile_width, halo_x, a, b, r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], r00[1-sense], r01[1-sense], r10[1-sense], r11[1-sense], i00[1-sense], i01[1-sense], i10[1-sense], i11[1-sense], inner, sides);
    }
    sense = 1 - sense;
}


void CPUBlockSSEKernel::wait_for_completion() {
}


void CPUBlockSSEKernel::get_sample(size_t dest_stride, size_t x, size_t y, size_t width, size_t height, float * dest_real, float * dest_imag) const {
    get_quadrant_sample(r00[sense], r01[sense], r10[sense], r11[sense], i00[sense], i01[sense], i10[sense], i11[sense], tile_width / 2, dest_stride, x, y, width, height, dest_real, dest_imag);
}

void CPUBlockSSEKernel::initialize_MPI(MPI_Comm _cartcomm, int _start_x, int _inner_end_x, int _start_y, int _inner_start_y, int _inner_end_y) {
    cartcomm=_cartcomm;
    MPI_Cart_shift(cartcomm, 0, 1, &neighbors[UP], &neighbors[DOWN]);
    MPI_Cart_shift(cartcomm, 1, 1, &neighbors[LEFT], &neighbors[RIGHT]);
    start_x = _start_x;
    inner_end_x = _inner_end_x;
    start_y = _start_y;
    inner_start_y = _inner_start_y;
    inner_end_y = _inner_end_y;

    // Halo exchange uses wave pattern to communicate
    // halo_x-wide inner rows are sent first to left and right
    // Then full length rows are exchanged to the top and bottom
    int count = (inner_end_y-inner_start_y) / 2;	// The number of rows in the halo submatrix
    int block_length = halo_x / 2;	// The number of columns in the halo submatrix
    int stride = tile_width / 2;	// The combined width of the matrix with the halo
    MPI_Type_vector (count, block_length, stride, MPI_FLOAT, &verticalBorder);
    MPI_Type_commit (&verticalBorder);

    count = halo_y / 2;	// The vertical halo in rows
    block_length = tile_width / 2;	// The number of columns of the matrix
    stride = tile_width / 2;	// The combined width of the matrix with the halo
    MPI_Type_vector (count, block_length, stride, MPI_FLOAT, &horizontalBorder);
    MPI_Type_commit (&horizontalBorder);

}

void CPUBlockSSEKernel::start_halo_exchange() {
    // Halo exchange: LEFT/RIGHT
    int offset = (inner_start_y-start_y)*tile_width/4;
    MPI_Irecv(r00[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 0, cartcomm, req+0);
    MPI_Irecv(r01[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 1, cartcomm, req+1);
    MPI_Irecv(r10[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 2, cartcomm, req+2);
    MPI_Irecv(r11[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 3, cartcomm, req+3);
    MPI_Irecv(i00[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 4, cartcomm, req+4);
    MPI_Irecv(i01[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 5, cartcomm, req+5);
    MPI_Irecv(i10[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 6, cartcomm, req+6);
    MPI_Irecv(i11[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 7, cartcomm, req+7);
    offset = (inner_start_y-start_y)*tile_width/4+(inner_end_x-start_x)/2;
    MPI_Irecv(r00[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 8, cartcomm, req+8);
    MPI_Irecv(r01[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 9, cartcomm, req+9);
    MPI_Irecv(r10[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 10, cartcomm, req+10);
    MPI_Irecv(r11[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 11, cartcomm, req+11);
    MPI_Irecv(i00[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 12, cartcomm, req+12);
    MPI_Irecv(i01[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 13, cartcomm, req+13);
    MPI_Irecv(i10[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 14, cartcomm, req+14);
    MPI_Irecv(i11[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 15, cartcomm, req+15);

    offset=(inner_start_y-start_y)*tile_width/4+(inner_end_x-halo_x-start_x)/2;
    MPI_Isend(r00[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 0, cartcomm, req+16);
    MPI_Isend(r01[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 1, cartcomm, req+17);
    MPI_Isend(r10[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 2, cartcomm, req+18);
    MPI_Isend(r11[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 3, cartcomm, req+19);
    MPI_Isend(i00[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 4, cartcomm, req+20);
    MPI_Isend(i01[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 5, cartcomm, req+21);
    MPI_Isend(i10[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 6, cartcomm, req+22);
    MPI_Isend(i11[1-sense]+offset, 1, verticalBorder, neighbors[RIGHT], 7, cartcomm, req+23);
    offset=(inner_start_y-start_y)*tile_width/4+halo_x/2;
    MPI_Isend(r00[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 8, cartcomm, req+24);
    MPI_Isend(r01[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 9, cartcomm, req+25);
    MPI_Isend(r10[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 10, cartcomm, req+26);
    MPI_Isend(r11[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 11, cartcomm, req+27);
    MPI_Isend(i00[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 12, cartcomm, req+28);
    MPI_Isend(i01[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 13, cartcomm, req+29);
    MPI_Isend(i10[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 14, cartcomm, req+30);
    MPI_Isend(i11[1-sense]+offset, 1, verticalBorder, neighbors[LEFT], 15, cartcomm, req+31);
}

void CPUBlockSSEKernel::finish_halo_exchange() {

    MPI_Waitall(32, req, statuses);

    // Halo exchange: UP/DOWN
    int offset = 0;
    MPI_Irecv(r00[sense]+offset, 1, horizontalBorder, neighbors[UP], 0, cartcomm, req+0);
    MPI_Irecv(r01[sense]+offset, 1, horizontalBorder, neighbors[UP], 1, cartcomm, req+1);
    MPI_Irecv(r10[sense]+offset, 1, horizontalBorder, neighbors[UP], 2, cartcomm, req+2);
    MPI_Irecv(r11[sense]+offset, 1, horizontalBorder, neighbors[UP], 3, cartcomm, req+3);
    MPI_Irecv(i00[sense]+offset, 1, horizontalBorder, neighbors[UP], 4, cartcomm, req+4);
    MPI_Irecv(i01[sense]+offset, 1, horizontalBorder, neighbors[UP], 5, cartcomm, req+5);
    MPI_Irecv(i10[sense]+offset, 1, horizontalBorder, neighbors[UP], 6, cartcomm, req+6);
    MPI_Irecv(i11[sense]+offset, 1, horizontalBorder, neighbors[UP], 7, cartcomm, req+7);
    offset = (inner_end_y-start_y)*tile_width/4;
    MPI_Irecv(r00[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 8, cartcomm, req+8);
    MPI_Irecv(r01[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 9, cartcomm, req+9);
    MPI_Irecv(r10[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 10, cartcomm, req+10);
    MPI_Irecv(r11[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 11, cartcomm, req+11);
    MPI_Irecv(i00[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 12, cartcomm, req+12);
    MPI_Irecv(i01[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 13, cartcomm, req+13);
    MPI_Irecv(i10[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 14, cartcomm, req+14);
    MPI_Irecv(i11[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 15, cartcomm, req+15);

    offset=(inner_end_y-halo_y-start_y)*tile_width/4;
    MPI_Isend(r00[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 0, cartcomm, req+16);
    MPI_Isend(r01[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 1, cartcomm, req+17);
    MPI_Isend(r10[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 2, cartcomm, req+18);
    MPI_Isend(r11[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 3, cartcomm, req+19);
    MPI_Isend(i00[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 4, cartcomm, req+20);
    MPI_Isend(i01[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 5, cartcomm, req+21);
    MPI_Isend(i10[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 6, cartcomm, req+22);
    MPI_Isend(i11[sense]+offset, 1, horizontalBorder, neighbors[DOWN], 7, cartcomm, req+23);
    offset=halo_y*tile_width/4;
    MPI_Isend(r00[sense]+offset, 1, horizontalBorder, neighbors[UP], 8, cartcomm, req+24);
    MPI_Isend(r01[sense]+offset, 1, horizontalBorder, neighbors[UP], 9, cartcomm, req+25);
    MPI_Isend(r10[sense]+offset, 1, horizontalBorder, neighbors[UP], 10, cartcomm, req+26);
    MPI_Isend(r11[sense]+offset, 1, horizontalBorder, neighbors[UP], 11, cartcomm, req+27);
    MPI_Isend(i00[sense]+offset, 1, horizontalBorder, neighbors[UP], 12, cartcomm, req+28);
    MPI_Isend(i01[sense]+offset, 1, horizontalBorder, neighbors[UP], 13, cartcomm, req+29);
    MPI_Isend(i10[sense]+offset, 1, horizontalBorder, neighbors[UP], 14, cartcomm, req+30);
    MPI_Isend(i11[sense]+offset, 1, horizontalBorder, neighbors[UP], 15, cartcomm, req+31);

    MPI_Waitall(32, req, statuses);

}
