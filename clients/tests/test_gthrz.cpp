/* ************************************************************************
 * Copyright (c) 2018 Advanced Micro Devices, Inc.
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

#include "testing_gthrz.hpp"
#include "utility.hpp"

#include <gtest/gtest.h>
#include <rocsparse.h>
#include <vector>

typedef rocsparse_index_base       base;
typedef std::tuple<int, int, base> gthrz_tuple;

int gthrz_N_range[]   = {12000, 15332, 22031};
int gthrz_nnz_range[] = {-1, 0, 5, 10, 500, 1000, 7111, 10000};

base gthrz_idx_base_range[] = {rocsparse_index_base_zero, rocsparse_index_base_one};

class parameterized_gthrz : public testing::TestWithParam<gthrz_tuple>
{
protected:
    parameterized_gthrz() {}
    virtual ~parameterized_gthrz() {}
    virtual void SetUp() {}
    virtual void TearDown() {}
};

Arguments setup_gthrz_arguments(gthrz_tuple tup)
{
    Arguments arg;
    arg.N        = std::get<0>(tup);
    arg.nnz      = std::get<1>(tup);
    arg.idx_base = std::get<2>(tup);
    arg.timing   = 0;
    return arg;
}

TEST(gthrz_bad_arg, gthrz_float)
{
    testing_gthrz_bad_arg<float>();
}

TEST_P(parameterized_gthrz, gthrz_float)
{
    Arguments arg = setup_gthrz_arguments(GetParam());

    rocsparse_status status = testing_gthrz<float>(arg);
    EXPECT_EQ(status, rocsparse_status_success);
}

TEST_P(parameterized_gthrz, gthrz_double)
{
    Arguments arg = setup_gthrz_arguments(GetParam());

    rocsparse_status status = testing_gthrz<double>(arg);
    EXPECT_EQ(status, rocsparse_status_success);
}

INSTANTIATE_TEST_CASE_P(gthrz,
                        parameterized_gthrz,
                        testing::Combine(testing::ValuesIn(gthrz_N_range),
                                         testing::ValuesIn(gthrz_nnz_range),
                                         testing::ValuesIn(gthrz_idx_base_range)));
