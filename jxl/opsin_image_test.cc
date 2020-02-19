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

#include <stdio.h>

#include "gtest/gtest.h"
#include "jxl/base/compiler_specific.h"
#include "jxl/color_management.h"
#include "jxl/dec_xyb.h"
#include "jxl/enc_xyb.h"
#include "jxl/image.h"
#include "jxl/linalg.h"
#include "jxl/opsin_params.h"

namespace jxl {
namespace {

TEST(OpsinImageTest, MaxCubeRootError) { TestCubeRoot(); }

// Convert a single linear sRGB color to xyb, using the exact image conversion
// procedure that jpeg xl uses.
void LinearSrgbToOpsin(float rgb_r, float rgb_g, float rgb_b,
                       float* JXL_RESTRICT xyb_x, float* JXL_RESTRICT xyb_y,
                       float* JXL_RESTRICT xyb_b) {
  Image3F linear(1, 1);
  linear.PlaneRow(0, 0)[0] = rgb_r;
  linear.PlaneRow(1, 0)[0] = rgb_g;
  linear.PlaneRow(2, 0)[0] = rgb_b;

  ImageMetadata metadata;
  metadata.bits_per_sample = 32;
  metadata.color_encoding = ColorEncoding::LinearSRGB();
  ImageBundle ib(&metadata);
  ib.SetFromImage(std::move(linear), metadata.color_encoding);
  Image3F opsin(1, 1);
  ImageBundle unused_linear;
  (void)ToXYB(ib, 1.0f, /*pool=*/nullptr, &opsin, &unused_linear);

  *xyb_x = opsin.PlaneRow(0, 0)[0];
  *xyb_y = opsin.PlaneRow(1, 0)[0];
  *xyb_b = opsin.PlaneRow(2, 0)[0];
}

// Convert a single XYB color to linear sRGB, using the exact image conversion
// procedure that jpeg xl uses.
void OpsinToLinearSrgb(float xyb_x, float xyb_y, float xyb_b,
                       float* JXL_RESTRICT rgb_r, float* JXL_RESTRICT rgb_g,
                       float* JXL_RESTRICT rgb_b) {
  Image3F opsin(1, 1);
  opsin.PlaneRow(0, 0)[0] = xyb_x;
  opsin.PlaneRow(1, 0)[0] = xyb_y;
  opsin.PlaneRow(2, 0)[0] = xyb_b;
  Image3F linear(1, 1);
  OpsinParams opsin_params;
  opsin_params.Init();
  OpsinToLinear(opsin, Rect(opsin), nullptr, &linear, opsin_params);
  *rgb_r = linear.PlaneRow(0, 0)[0];
  *rgb_g = linear.PlaneRow(1, 0)[0];
  *rgb_b = linear.PlaneRow(2, 0)[0];
}

void OpsinRoundtripTestRGB(float r, float g, float b) {
  float xyb_x, xyb_y, xyb_b;
  LinearSrgbToOpsin(r, g, b, &xyb_x, &xyb_y, &xyb_b);
  float r2, g2, b2;
  OpsinToLinearSrgb(xyb_x, xyb_y, xyb_b, &r2, &g2, &b2);
  EXPECT_NEAR(r, r2, 1e-3);
  EXPECT_NEAR(g, g2, 1e-3);
  EXPECT_NEAR(b, b2, 1e-3);
}

TEST(OpsinImageTest, VerifyOpsinAbsorbanceInverseMatrix) {
  float matrix[9];  // writable copy
  for (int i = 0; i < 9; i++) {
    matrix[i] = GetOpsinAbsorbanceInverseMatrix()[i];
  }
  Inv3x3Matrix(matrix);
  for (int i = 0; i < 9; i++) {
    EXPECT_NEAR(matrix[i], kOpsinAbsorbanceMatrix[i], 1e-6);
  }
}

TEST(OpsinImageTest, OpsinRoundtrip) {
  OpsinRoundtripTestRGB(0, 0, 0);
  OpsinRoundtripTestRGB(1, 1, 1);
  OpsinRoundtripTestRGB(128, 128, 128);
  OpsinRoundtripTestRGB(255, 255, 255);

  OpsinRoundtripTestRGB(0, 0, 1);
  OpsinRoundtripTestRGB(0, 0, 128);
  OpsinRoundtripTestRGB(0, 0, 255);

  OpsinRoundtripTestRGB(0, 1, 0);
  OpsinRoundtripTestRGB(0, 128, 0);
  OpsinRoundtripTestRGB(0, 255, 0);

  OpsinRoundtripTestRGB(1, 0, 0);
  OpsinRoundtripTestRGB(128, 0, 0);
  OpsinRoundtripTestRGB(255, 0, 0);
}

TEST(OpsinImageTest, VerifyZero) {
  // Test that black color (zero energy) is 0,0,0 in xyb.
  float x, y, b;
  LinearSrgbToOpsin(0, 0, 0, &x, &y, &b);
  EXPECT_NEAR(0, x, 1e-9);
  EXPECT_NEAR(0, y, 1e-7);
  EXPECT_NEAR(0, b, 1e-7);
}

TEST(OpsinImageTest, VerifyGray) {
  // Test that grayscale colors have a fixed y/b ratio and x==0.
  for (size_t i = 1; i < 255; i++) {
    float x, y, b;
    LinearSrgbToOpsin(i, i, i, &x, &y, &b);
    EXPECT_NEAR(0, x, 1e-6);
    EXPECT_NEAR(kYToBRatio, b / y, 2e-6);
  }
}

}  // namespace
}  // namespace jxl
