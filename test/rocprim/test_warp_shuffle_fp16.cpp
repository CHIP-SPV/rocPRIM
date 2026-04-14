// MIT License
//
// Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Regression test for IGC fp16 narrow<->wide shuffle miscompile.
//
// Reproduces the pattern emitted by rocprim::warp_shuffle_op for
// sizeof(T) < sizeof(int): the SPIR-V translator inserts an
// OpUConvert %uint <- %ushort before OpSubgroupShuffleINTEL and a
// matching OpUConvert %ushort <- %uint afterwards. When the result
// is consumed inside a divergent block (OpBranchConditional), IGC
// on Intel Arc miscompiles the chain and the second shuffle yields 0.

#include "../common_test_header.hpp"
#include "test_utils.hpp"

#include <rocprim/intrinsics/warp_shuffle.hpp>
#include <rocprim/types.hpp>

// Two chained warp_shuffles on __half followed by a divergent store.
// This is the minimum shape that triggers the IGC bug; a single
// unconditional shuffle passes.
template<unsigned int LogicalWarpSize>
__global__ __launch_bounds__(64)
void chained_half_shuffle_divergent_kernel(const __half* in, __half* out)
{
    const unsigned int tid = threadIdx.x;
    __half v = in[tid];

    // Mirror the rocprim::warp_reduce_shuffle inner loop:
    // for(offset=1; offset<WarpSize; offset*=2) value = shuffle_down(output, offset);
    // This is the exact shape that emits `UConvert uint <- ushort` before and
    // `UConvert ushort <- uint` after OpSubgroupShuffleINTEL and triggers the
    // IGC miscompile on Arc.
    __half output = v;
    __half s1 = rocprim::warp_shuffle_down(output, 1, LogicalWarpSize);
    output = output + s1;

    __half s2 = rocprim::warp_shuffle_down(output, 2, LogicalWarpSize);
    output = output + s2;

    __half a2 = output;

    // Divergent consumer: only lane 0 of every logical warp writes.
    if ((tid % LogicalWarpSize) == 0)
    {
        out[tid / LogicalWarpSize] = a2;
    }
}

TEST(RocprimWarpShuffleFp16, ChainedDivergentHalfShuffle)
{
    int device_id = test_common_utils::obtain_device_from_ctest();
    HIP_CHECK(hipSetDevice(device_id));

    constexpr unsigned int LogicalWarpSize = 4;
    constexpr unsigned int BlockSize       = 64;
    constexpr unsigned int NumWarps        = BlockSize / LogicalWarpSize;

    std::vector<__half> h_in(BlockSize);
    for(unsigned int i = 0; i < BlockSize; ++i)
    {
        // Values 2,3,4,5 per 4-lane group -> expected sum = 14.
        h_in[i] = __half(static_cast<float>((i % LogicalWarpSize) + 2));
    }
    std::vector<__half> h_out(NumWarps, __half(0.0f));

    __half* d_in  = nullptr;
    __half* d_out = nullptr;
    HIP_CHECK(hipMalloc(&d_in,  h_in.size()  * sizeof(__half)));
    HIP_CHECK(hipMalloc(&d_out, h_out.size() * sizeof(__half)));
    HIP_CHECK(hipMemcpy(d_in,  h_in.data(),  h_in.size()  * sizeof(__half),
                        hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_out, h_out.data(), h_out.size() * sizeof(__half),
                        hipMemcpyHostToDevice));

    hipLaunchKernelGGL(HIP_KERNEL_NAME(chained_half_shuffle_divergent_kernel<LogicalWarpSize>),
                       dim3(1), dim3(BlockSize), 0, 0, d_in, d_out);
    HIP_CHECK(hipGetLastError());
    HIP_CHECK(hipDeviceSynchronize());

    HIP_CHECK(hipMemcpy(h_out.data(), d_out, h_out.size() * sizeof(__half),
                        hipMemcpyDeviceToHost));

    constexpr float expected = 2.0f + 3.0f + 4.0f + 5.0f; // 14
    unsigned int mismatches = 0;
    for(unsigned int w = 0; w < NumWarps; ++w)
    {
        const float got = static_cast<float>(h_out[w]);
        if(std::abs(got - expected) > 0.01f)
        {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0u)
        << "IGC fp16 narrow<->wide chained-shuffle miscompile triggered "
        << "(got 0 for second shuffle under divergent consumer).";

    HIP_CHECK(hipFree(d_in));
    HIP_CHECK(hipFree(d_out));
}
