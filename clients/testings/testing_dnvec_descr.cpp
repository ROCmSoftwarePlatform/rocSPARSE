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

#include "testing.hpp"

void testing_dnvec_descr_bad_arg(const Arguments& arg)
{
    int64_t size = 100;

    rocsparse_datatype type = rocsparse_datatype_f32_r;

    // Allocate memory on device
    device_vector<float> values(size);

    if(!values)
    {
        CHECK_HIP_ERROR(hipErrorOutOfMemory);
        return;
    }

    rocsparse_dnvec_descr x;

    // rocsparse_create_dnvec_descr
    EXPECT_ROCSPARSE_STATUS(rocsparse_create_dnvec_descr(nullptr, size, values, type),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_create_dnvec_descr(&x, -1, values, type),
                            rocsparse_status_invalid_size);
    EXPECT_ROCSPARSE_STATUS(rocsparse_create_dnvec_descr(&x, size, nullptr, type),
                            rocsparse_status_invalid_pointer);

    // rocsparse_destroy_dnvec_descr
    EXPECT_ROCSPARSE_STATUS(rocsparse_destroy_dnvec_descr(nullptr),
                            rocsparse_status_invalid_pointer);

    // Check valid descriptor creations
    EXPECT_ROCSPARSE_STATUS(rocsparse_create_dnvec_descr(&x, 0, nullptr, type),
                            rocsparse_status_success);
    EXPECT_ROCSPARSE_STATUS(rocsparse_destroy_dnvec_descr(x), rocsparse_status_success);

    // Create valid descriptor
    EXPECT_ROCSPARSE_STATUS(rocsparse_create_dnvec_descr(&x, size, values, type),
                            rocsparse_status_success);

    // rocsparse_dnvec_get
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get(nullptr, &size, (void**)&values, &type),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get(x, nullptr, (void**)&values, &type),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get(x, &size, nullptr, &type),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get(x, &size, (void**)&values, nullptr),
                            rocsparse_status_invalid_pointer);

    // rocsparse_dnvec_get_values
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get_values(nullptr, (void**)&values),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_get_values(x, nullptr),
                            rocsparse_status_invalid_pointer);

    // rocsparse_dnvec_set_values
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_set_values(nullptr, values),
                            rocsparse_status_invalid_pointer);
    EXPECT_ROCSPARSE_STATUS(rocsparse_dnvec_set_values(x, nullptr),
                            rocsparse_status_invalid_pointer);

    // Destroy valid descriptor
    EXPECT_ROCSPARSE_STATUS(rocsparse_destroy_dnvec_descr(x), rocsparse_status_success);
}
