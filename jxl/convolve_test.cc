// Copyright (c) the JPEG XL Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "jxl/convolve.h"

#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "jxl/base/compiler_specific.h"
#include "jxl/base/data_parallel.h"
#include "jxl/base/thread_pool_internal.h"
#define HWY_USE_GTEST  // prevent redefining TEST
#include <hwy/tests/test_util.h>

#include "jxl/image_ops.h"
#include "jxl/image_test_utils.h"

#ifndef JXL_DEBUG_CONVOLVE
#define JXL_DEBUG_CONVOLVE 0
#endif

#include <hwy/static_targets.h>

namespace jxl {

#include "jxl/convolve-inl.h"

namespace {

HWY_ATTR void TestNeighborsImpl() {
  const Neighbors::D d;
  const Neighbors::V v = Iota(d, 0);
  HWY_ALIGN float actual[hwy::kTestMaxVectorSize / sizeof(float)] = {0};

  HWY_ALIGN float first_l1[hwy::kTestMaxVectorSize / sizeof(float)] = {
      0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  Store(Neighbors::FirstL1(v), d, actual);
  EXPECT_EQ(std::vector<float>(first_l1, first_l1 + d.N),
            std::vector<float>(actual, actual + d.N));

#if HWY_BITS != 0
  HWY_ALIGN float first_l2[hwy::kTestMaxVectorSize / sizeof(float)] = {
      1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  Store(Neighbors::FirstL2(v), d, actual);
  EXPECT_EQ(std::vector<float>(first_l2, first_l2 + d.N),
            std::vector<float>(actual, actual + d.N));

  HWY_ALIGN float first_l3[hwy::kTestMaxVectorSize / sizeof(float)] = {
      2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  Store(Neighbors::FirstL3(v), d, actual);
  EXPECT_EQ(std::vector<float>(first_l3, first_l3 + d.N),
            std::vector<float>(actual, actual + d.N));
#endif
}

TEST(ConvolveTest, TestNeighbors) { TestNeighborsImpl(); }

template <class Random>
HWY_ATTR void VerifySymmetric3(const size_t xsize, const size_t ysize,
                               ThreadPool* pool, Random* rng) {
  const size_t kRadius = 1;
  JXL_CHECK(xsize > kRadius);
  JXL_CHECK(ysize > kRadius);
  const Rect rect(0, 0, xsize, ysize);

  ImageF in(xsize, ysize);
  GenerateImage(GeneratorRandom<float, Random>(rng, 1.0f), &in);

  ImageF out_expected(xsize, ysize);
  ImageF out_actual(xsize, ysize);

  const WeightsSymmetric3& weights = WeightsSymmetric3Lowpass();
  Symmetric3(in, rect, weights, pool, &out_expected);
  SlowSymmetric3(in, rect, weights, pool, &out_actual);

  VerifyRelativeError(out_expected, out_actual, 1E-5f, 1E-5f);
}

// Ensures Symmetric and Separable give the same result.
template <class Random>
HWY_ATTR void VerifySymmetric5(const size_t xsize, const size_t ysize,
                               ThreadPool* pool, Random* rng) {
  const size_t kRadius = 2;
  JXL_CHECK(xsize > kRadius);
  JXL_CHECK(ysize > kRadius);
  const Rect rect(0, 0, xsize, ysize);

  ImageF in(xsize, ysize);
  GenerateImage(GeneratorRandom<float, Random>(rng, 1.0f), &in);

  ImageF out_expected(xsize, ysize);
  ImageF out_actual(xsize, ysize);

  Separable5(in, Rect(in), WeightsSeparable5Lowpass(), pool, &out_expected);
  Symmetric5(in, rect, WeightsSymmetric5Lowpass(), pool, &out_actual);

  VerifyRelativeError(out_expected, out_actual, 1E-5f, 1E-5f);
}

template <class Random>
HWY_ATTR void VerifySeparable5(const size_t xsize, const size_t ysize,
                               ThreadPool* pool, Random* rng) {
  const size_t kRadius = 2;
  JXL_CHECK(xsize > kRadius);
  JXL_CHECK(ysize > kRadius);
  const Rect rect(0, 0, xsize, ysize);

  ImageF in(xsize, ysize);
  GenerateImage(GeneratorRandom<float, Random>(rng, 1.0f), &in);

  ImageF out_expected(xsize, ysize);
  ImageF out_actual(xsize, ysize);

  const WeightsSeparable5& weights = WeightsSeparable5Lowpass();
  Separable5(in, Rect(in), weights, pool, &out_expected);
  SlowSeparable5(in, rect, weights, pool, &out_actual);

  VerifyRelativeError(out_expected, out_actual, 1E-5f, 1E-5f);
}

// For all xsize/ysize and kernels:
HWY_ATTR void VerifyAll() {
  ThreadPoolInternal pool(0);
  constexpr size_t kMinWidth = HWY_FULL(float)::N + kConvolveMaxRadius;
  pool.Run(kMinWidth, 40, ThreadPool::SkipInit(),
           [](const int task, int /*thread*/) {
             const size_t xsize = task;
             std::mt19937_64 rng(129 + 13 * xsize);

             ThreadPool* null_pool = nullptr;
             ThreadPoolInternal pool3(3);
             for (size_t ysize = kConvolveMaxRadius; ysize < 16; ++ysize) {
               JXL_DEBUG(JXL_DEBUG_CONVOLVE,
                         "%zu x %zu=====================================",
                         xsize, ysize);

               JXL_DEBUG(JXL_DEBUG_CONVOLVE, "Sym3------------------");
               VerifySymmetric3(xsize, ysize, null_pool, &rng);
               VerifySymmetric3(xsize, ysize, &pool3, &rng);

               JXL_DEBUG(JXL_DEBUG_CONVOLVE, "Sym5------------------");
               VerifySymmetric5(xsize, ysize, null_pool, &rng);
               VerifySymmetric5(xsize, ysize, &pool3, &rng);

               JXL_DEBUG(JXL_DEBUG_CONVOLVE, "Sep5------------------");
               VerifySeparable5(xsize, ysize, null_pool, &rng);
               VerifySeparable5(xsize, ysize, &pool3, &rng);
             }
           });
}

TEST(ConvolveTest, TestVerifyAll) { VerifyAll(); }

}  // namespace
}  // namespace jxl
