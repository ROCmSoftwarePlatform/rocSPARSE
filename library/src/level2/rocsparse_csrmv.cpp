/* ************************************************************************
 * Copyright 2018 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "definitions.h"
#include "rocsparse.h"
#include "rocsparse_csrmv.hpp"

__attribute__((unused))
static unsigned int flp2(unsigned int x)
{
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    return x - (x >> 1);
}

// Short rows in CSR-Adaptive are batched together into a single row block.
// If there are a relatively small number of these, then we choose to do
// a horizontal reduction (groups of threads all reduce the same row).
// If there are many threads (e.g. more threads than the maximum size
// of our workgroup) then we choose to have each thread serially reduce
// the row.
// This function calculates the number of threads that could team up
// to reduce these groups of rows. For instance, if you have a
// workgroup size of 256 and 4 rows, you could have 64 threads
// working on each row. If you have 5 rows, only 32 threads could
// reliably work on each row because our reduction assumes power-of-2.
static unsigned long long numThreadsForReduction(unsigned long long num_rows)
{
#if defined(__INTEL_COMPILER)
    return WG_SIZE >> (_bit_scan_reverse(num_rows - 1) + 1);
#elif(defined(__HIP_PLATFORM_NVCC__))
    return flp2(WG_SIZE / num_rows);
#elif(defined(__clang__) && __has_builtin(__builtin_clz)) || \
    !defined(__clang) && defined(__GNUG__) &&                \
        ((__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) > 30202)
    return (WG_SIZE >> (8 * sizeof(int) - __builtin_clz(num_rows - 1)));
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
    unsigned long long bit_returned;
    _BitScanReverse(&bit_returned, (num_rows - 1));
    return WG_SIZE >> (bit_returned + 1);
#else
    return flp2(WG_SIZE / num_rows);
#endif
}

static void ComputeRowBlocks(unsigned long long* rowBlocks,
                             size_t& rowBlockSize,
                             const int* rowDelimiters,
                             int nRows,
                             bool allocate_row_blocks = true)
{
    unsigned long long* rowBlocksBase;
    int total_row_blocks = 1; // Start at one because of rowBlock[0]

    if(allocate_row_blocks)
    {
        rowBlocksBase = rowBlocks;
        *rowBlocks    = 0;
        rowBlocks++;
    }
    unsigned long long sum = 0;
    unsigned long long i, last_i = 0;

    // Check to ensure nRows can fit in 32 bits
    if((unsigned long long)nRows > (unsigned long long)std::pow(2, ROW_BITS))
    {
        fprintf(stderr, "nrow does not fit in 32 bits\n");
        exit(1);
    }

    int consecutive_long_rows = 0;
    for(i = 1; i <= (unsigned long long)nRows; i++)
    {
        int row_length = (rowDelimiters[i] - rowDelimiters[i - 1]);
        sum += row_length;

        // The following section of code calculates whether you're moving between
        // a series of "short" rows and a series of "long" rows.
        // This is because the reduction in CSR-Adaptive likes things to be
        // roughly the same length. Long rows can be reduced horizontally.
        // Short rows can be reduced one-thread-per-row. Try not to mix them.
        if(row_length > 128)
            consecutive_long_rows++;
        else if(consecutive_long_rows > 0)
        {
            // If it turns out we WERE in a long-row region, cut if off now.
            if(row_length < 32) // Now we're in a short-row region
                consecutive_long_rows = -1;
            else
                consecutive_long_rows++;
        }

        // If you just entered into a "long" row from a series of short rows,
        // then we need to make sure we cut off those short rows. Put them in
        // their own workgroup.
        if(consecutive_long_rows == 1)
        {
            // Assuming there *was* a previous workgroup. If not, nothing to do here.
            if(i - last_i > 1)
            {
                if(allocate_row_blocks)
                {
                    *rowBlocks = ((i - 1) << (64 - ROW_BITS));
                    // If this row fits into CSR-Stream, calculate how many rows
                    // can be used to do a parallel reduction.
                    // Fill in the low-order bits with the numThreadsForRed
                    if(((i - 1) - last_i) > (unsigned long long)ROWS_FOR_VECTOR)
                        *(rowBlocks - 1) |= numThreadsForReduction((i - 1) - last_i);
                    rowBlocks++;
                }
                total_row_blocks++;
                last_i = i - 1;
                sum    = row_length;
            }
        }
        else if(consecutive_long_rows == -1)
        {
            // We see the first short row after some long ones that
            // didn't previously fill up a row block.
            if(allocate_row_blocks)
            {
                *rowBlocks = ((i - 1) << (64 - ROW_BITS));
                if(((i - 1) - last_i) > (unsigned long long)ROWS_FOR_VECTOR)
                    *(rowBlocks - 1) |= numThreadsForReduction((i - 1) - last_i);
                rowBlocks++;
            }
            total_row_blocks++;
            last_i                = i - 1;
            sum                   = row_length;
            consecutive_long_rows = 0;
        }

        // Now, what's up with this row? What did it do?

        // exactly one row results in non-zero elements to be greater than blockSize
        // This is csr-vector case; bottom WGBITS == workgroup ID
        if((i - last_i == 1) && sum > (unsigned long long)BLOCKSIZE)
        {
            int numWGReq =
                static_cast<int>(std::ceil((double)row_length / (BLOCK_MULTIPLIER * BLOCKSIZE)));

            // Check to ensure #workgroups can fit in WGBITS bits, if not
            // then the last workgroup will do all the remaining work
            numWGReq = (numWGReq < (int)std::pow(2, WG_BITS)) ? numWGReq : (int)std::pow(2, WG_BITS);

            if(allocate_row_blocks)
            {
                for(int w = 1; w < numWGReq; w++)
                {
                    *rowBlocks = ((i - 1) << (64 - ROW_BITS));
                    *rowBlocks |= static_cast<unsigned long long>(w);
                    rowBlocks++;
                }
                *rowBlocks = (i << (64 - ROW_BITS));
                rowBlocks++;
            }
            total_row_blocks += numWGReq;
            last_i                = i;
            sum                   = 0;
            consecutive_long_rows = 0;
        }
        // more than one row results in non-zero elements to be greater than blockSize
        // This is csr-stream case; bottom WGBITS = number of parallel reduction threads
        else if((i - last_i > 1) && sum > (unsigned long long)BLOCKSIZE)
        {
            i--; // This row won't fit, so back off one.
            if(allocate_row_blocks)
            {
                *rowBlocks = (i << (64 - ROW_BITS));
                if((i - last_i) > (unsigned long long)ROWS_FOR_VECTOR)
                    *(rowBlocks - 1) |= numThreadsForReduction(i - last_i);
                rowBlocks++;
            }
            total_row_blocks++;
            last_i                = i;
            sum                   = 0;
            consecutive_long_rows = 0;
        }
        // This is csr-stream case; bottom WGBITS = number of parallel reduction threads
        else if(sum == (unsigned long long)BLOCKSIZE)
        {
            if(allocate_row_blocks)
            {
                *rowBlocks = (i << (64 - ROW_BITS));
                if((i - last_i) > (unsigned long long)ROWS_FOR_VECTOR)
                    *(rowBlocks - 1) |= numThreadsForReduction(i - last_i);
                rowBlocks++;
            }
            total_row_blocks++;
            last_i                = i;
            sum                   = 0;
            consecutive_long_rows = 0;
        }
    }

    // If we didn't fill a row block with the last row, make sure we don't lose it.
    if(allocate_row_blocks && (*(rowBlocks - 1) >> (64 - ROW_BITS)) != static_cast<unsigned long long>(nRows))
    {
        *rowBlocks = (static_cast<unsigned long long>(nRows) << (64 - ROW_BITS));
        if((nRows - last_i) > (unsigned long long)ROWS_FOR_VECTOR)
            *(rowBlocks - 1) |= numThreadsForReduction(i - last_i);
        rowBlocks++;
    }
    total_row_blocks++;

    if(allocate_row_blocks)
    {
        size_t dist = std::distance(rowBlocksBase, rowBlocks);
        assert((2 * dist) <= rowBlockSize);
        // Update the size of rowBlocks to reflect the actual amount of memory used
        // We're multiplying the size by two because the extended precision form of
        // CSR-Adaptive requires more space for the final global reduction.
        rowBlockSize = 2 * dist;
    }
    else
        rowBlockSize = 2 * total_row_blocks;
}

/*
 * ===========================================================================
 *    C wrapper
 * ===========================================================================
 */

extern "C" rocsparse_status rocsparse_csrmv_analysis(rocsparse_handle handle,
                                                     rocsparse_operation trans,
                                                     rocsparse_int m,
                                                     rocsparse_int n,
                                                     rocsparse_int nnz,
                                                     const rocsparse_mat_descr descr,
                                                     const rocsparse_int* csr_row_ptr,
                                                     const rocsparse_int* csr_col_ind,
                                                     rocsparse_mat_info info)
{
    // Check for valid handle and matrix descriptor
    if(handle == nullptr)
    {
        return rocsparse_status_invalid_handle;
    }
    else if(descr == nullptr)
    {
        return rocsparse_status_invalid_pointer;
    }
    else if(info == nullptr)
    {
        return rocsparse_status_invalid_pointer;
    }

    // Logging TODO bench logging
    log_trace(handle,
              "rocsparse_csrmv_analysis",
              trans,
              m,
              n,
              nnz,
              (const void*&)descr,
              (const void*&)csr_row_ptr,
              (const void*&)csr_col_ind,
              (const void*&)info);

    // Check index base
    if(descr->base != rocsparse_index_base_zero && descr->base != rocsparse_index_base_one)
    {
        return rocsparse_status_invalid_value;
    }
    if(descr->type != rocsparse_matrix_type_general)
    {
        // TODO
        return rocsparse_status_not_implemented;
    }

    // Check sizes
    if(m < 0)
    {
        return rocsparse_status_invalid_size;
    }
    else if(n < 0)
    {
        return rocsparse_status_invalid_size;
    }
    else if(nnz < 0)
    {
        return rocsparse_status_invalid_size;
    }

    // Check pointer arguments
    if(csr_row_ptr == nullptr)
    {
        return rocsparse_status_invalid_pointer;
    }
    else if(csr_col_ind == nullptr)
    {
        return rocsparse_status_invalid_pointer;
    }

    // Quick return if possible
    if(m == 0 || n == 0 || nnz == 0)
    {
        return rocsparse_status_success;
    }

    // Clear csrmv info
    RETURN_IF_ROCSPARSE_ERROR(rocsparse_destroy_csrmv_info(info->csrmv_info));

    // Create csrmv info
    RETURN_IF_ROCSPARSE_ERROR(rocsparse_create_csrmv_info(&info->csrmv_info));

    // row blocks size
    info->csrmv_info->size = 0;

    // Temporary arrays to hold device data
    std::vector<rocsparse_int> hptr(m + 1);
    RETURN_IF_HIP_ERROR(hipMemcpy(hptr.data(), csr_row_ptr, sizeof(rocsparse_int) * (m + 1), hipMemcpyDeviceToHost));

    // Determine row blocks array size
    ComputeRowBlocks((unsigned long long*)NULL, info->csrmv_info->size, hptr.data(), m, false);

    // Create row blocks structure
    std::vector<unsigned long long> row_blocks(info->csrmv_info->size, 0);

    ComputeRowBlocks(row_blocks.data(),
                     info->csrmv_info->size,
                     hptr.data(),
                     m,
                     true);

    // Allocate memory on device to hold csrmv info
    RETURN_IF_HIP_ERROR(hipMalloc((void**)&info->csrmv_info->row_blocks, sizeof(unsigned long long) * info->csrmv_info->size));

    // Copy row blocks information to device
    RETURN_IF_HIP_ERROR(hipMemcpy(info->csrmv_info->row_blocks, row_blocks.data(), sizeof(unsigned long long) * info->csrmv_info->size, hipMemcpyHostToDevice));

    // Store some pointers to verify correct execution
    info->csrmv_info->trans = trans;
    info->csrmv_info->m = m;
    info->csrmv_info->n = n;
    info->csrmv_info->nnz = nnz;
    info->csrmv_info->descr = descr;
    info->csrmv_info->csr_row_ptr = csr_row_ptr;
    info->csrmv_info->csr_col_ind = csr_col_ind;

    // Set built flag
    info->csrmv_info->built = true;

    return rocsparse_status_success;
}

extern "C" rocsparse_status rocsparse_csrmv_analysis_clear(rocsparse_handle handle,
                                                           rocsparse_mat_info info)
{
    // Check for valid handle and matrix descriptor
    if(handle == nullptr)
    {
        return rocsparse_status_invalid_handle;
    }
    else if(info == nullptr)
    {
        return rocsparse_status_invalid_pointer;
    }

    // Logging TODO bench logging
    log_trace(handle,
              "rocsparse_csrmv_analysis_clear",
              (const void*&)info);

    return rocsparse_destroy_csrmv_info(info->csrmv_info);
}

extern "C" rocsparse_status rocsparse_scsrmv(rocsparse_handle handle,
                                             rocsparse_operation trans,
                                             rocsparse_int m,
                                             rocsparse_int n,
                                             rocsparse_int nnz,
                                             const float* alpha,
                                             const rocsparse_mat_descr descr,
                                             const float* csr_val,
                                             const rocsparse_int* csr_row_ptr,
                                             const rocsparse_int* csr_col_ind,
                                             const float* x,
                                             const float* beta,
                                             float* y,
                                             const rocsparse_mat_info info)
{
    return rocsparse_csrmv_template<float>(
        handle, trans, m, n, nnz, alpha, descr, csr_val, csr_row_ptr, csr_col_ind, x, beta, y, info);
}

extern "C" rocsparse_status rocsparse_dcsrmv(rocsparse_handle handle,
                                             rocsparse_operation trans,
                                             rocsparse_int m,
                                             rocsparse_int n,
                                             rocsparse_int nnz,
                                             const double* alpha,
                                             const rocsparse_mat_descr descr,
                                             const double* csr_val,
                                             const rocsparse_int* csr_row_ptr,
                                             const rocsparse_int* csr_col_ind,
                                             const double* x,
                                             const double* beta,
                                             double* y,
                                             const rocsparse_mat_info info)
{
    return rocsparse_csrmv_template<double>(
        handle, trans, m, n, nnz, alpha, descr, csr_val, csr_row_ptr, csr_col_ind, x, beta, y, info);
}
