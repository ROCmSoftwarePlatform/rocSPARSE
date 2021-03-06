/*! \file */
/* ************************************************************************
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ************************************************************************ */

#pragma once
#ifndef CSRSM_DEVICE_H
#define CSRSM_DEVICE_H

#include "common.h"

template <unsigned int BLOCKSIZE, unsigned int WF_SIZE, bool SLEEP, typename T>
__device__ void csrsm_device(rocsparse_int m,
                             rocsparse_int nrhs,
                             T             alpha,
                             const rocsparse_int* __restrict__ csr_row_ptr,
                             const rocsparse_int* __restrict__ csr_col_ind,
                             const T* __restrict__ csr_val,
                             T* __restrict__ B,
                             rocsparse_int ldb,
                             int* __restrict__ done_array,
                             rocsparse_int* __restrict__ map,
                             rocsparse_int* __restrict__ zero_pivot,
                             rocsparse_index_base idx_base,
                             rocsparse_fill_mode  fill_mode,
                             rocsparse_diag_type  diag_type)
{
    // Index into the row map
    rocsparse_int idx = hipBlockIdx_x % m;

    // Shared memory to hold columns and values
    __shared__ rocsparse_int scsr_col_ind[BLOCKSIZE];
    __shared__ T             scsr_val[BLOCKSIZE];

    // Get the row this warp will operate on
    rocsparse_int row = map[idx];

    // Current row entry point and exit point
    rocsparse_int row_begin = csr_row_ptr[row] - idx_base;
    rocsparse_int row_end   = csr_row_ptr[row + 1] - idx_base;

    // Column index into B
    rocsparse_int col_B = hipBlockIdx_x / m * BLOCKSIZE + hipThreadIdx_x;

    // Index into B (i,j)
    rocsparse_int idx_B = row * ldb + col_B;

    // Index into done array
    rocsparse_int id = hipBlockIdx_x / m * m;

    // Initialize local sum with alpha and X
    T local_sum = (col_B < nrhs) ? alpha * B[idx_B] : static_cast<T>(0);

    // Initialize diagonal entry
    T diagonal = static_cast<T>(1);

    for(rocsparse_int j = row_begin; j < row_end; ++j)
    {
        // Project j onto [0, BLOCKSIZE-1]
        rocsparse_int k = (j - row_begin) & (BLOCKSIZE - 1);

        // Preload column indices and values into shared memory
        // This happens only once for each chunk of BLOCKSIZE elements
        if(k == 0)
        {
            scsr_col_ind[hipThreadIdx_x]
                = (hipThreadIdx_x < row_end - j) ? csr_col_ind[hipThreadIdx_x + j] - idx_base : -1;
            scsr_val[hipThreadIdx_x]
                = (hipThreadIdx_x < row_end - j) ? csr_val[hipThreadIdx_x + j] : -1;
        }

        // Wait for preload to finish
        __syncthreads();

        // Current column this lane operates on
        rocsparse_int local_col = scsr_col_ind[k];

        // Local value this lane operates with
        T local_val = scsr_val[k];

        // Check for numerical zero
        if(local_val == static_cast<T>(0) && local_col == row
           && diag_type == rocsparse_diag_type_non_unit)
        {
            // Numerical zero pivot found, avoid division by 0
            // and store index for later use.
            if(hipThreadIdx_x == 0)
            {
                atomicMin(zero_pivot, row + idx_base);
            }

            local_val = static_cast<T>(1);
        }

        // Differentiate upper and lower triangular mode
        if(fill_mode == rocsparse_fill_mode_upper)
        {
            // Processing upper triangular

            // Ignore all entries that are below the diagonal
            if(local_col < row)
            {
                continue;
            }

            // Diagonal entry
            if(local_col == row)
            {
                // If diagonal type is non unit, do division by diagonal entry
                if(diag_type == rocsparse_diag_type_non_unit)
                {
                    diagonal = static_cast<T>(1) / local_val;
                }

                // Skip diagonal entry
                continue;
            }
        }
        else if(fill_mode == rocsparse_fill_mode_lower)
        {
            // Processing lower triangular

            // Ignore all entries that are above the diagonal
            if(local_col > row)
            {
                break;
            }

            // Diagonal entry
            if(local_col == row)
            {
                // If diagonal type is non unit, do division by diagonal entry
                if(diag_type == rocsparse_diag_type_non_unit)
                {
                    diagonal = static_cast<T>(1) / local_val;
                }

                // Skip diagonal entry
                break;
            }
        }

        // Spin loop until dependency has been resolved
        if(hipThreadIdx_x == 0)
        {
            int          local_done    = atomicOr(&done_array[local_col + id], 0);
            unsigned int times_through = 0;
            while(!local_done)
            {
                if(SLEEP)
                {
                    for(unsigned int i = 0; i < times_through; ++i)
                    {
                        __builtin_amdgcn_s_sleep(1);
                    }

                    if(times_through < 3907)
                    {
                        ++times_through;
                    }
                }

                local_done = atomicOr(&done_array[local_col + id], 0);
            }
        }

        // Wait for spin looping thread to finish as the whole block depends on this row
        __syncthreads();

        // Make sure updated B is visible globally
        __threadfence();

        // Index into X
        rocsparse_int idx_X = local_col * ldb + col_B;

        // Local sum computation for each lane
        local_sum
            = (col_B < nrhs) ? rocsparse_fma(-local_val, B[idx_X], local_sum) : static_cast<T>(0);
    }

    // If we have non unit diagonal, take the diagonal into account
    // For unit diagonal, this would be multiplication with one
    if(diag_type == rocsparse_diag_type_non_unit)
    {
        local_sum = local_sum * diagonal;
    }

    // Store result in B
    if(col_B < nrhs)
    {
        B[idx_B] = local_sum;
    }

    // Wait for all threads to finish writing into global memory before we mark the row "done"
    __syncthreads();

    // Make sure B is written to global memory before setting row is done flag
    __threadfence();

    if(hipThreadIdx_x == 0)
    {
        // Write the "row is done" flag
        atomicOr(&done_array[row + id], 1);
    }
}

#endif // CSRSM_DEVICE_H
