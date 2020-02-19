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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <vector>

#include "jxl/aux_out.h"
#include "jxl/base/data_parallel.h"
#include "jxl/base/file_io.h"
#include "jxl/base/span.h"
#include "jxl/base/thread_pool_internal.h"
#include "jxl/codec_in_out.h"
#include "jxl/enc_cache.h"
#include "jxl/enc_file.h"
#include "jxl/enc_params.h"
#include "jxl/external_image.h"
#include "jxl/modular/encoding/context_predict.h"

const size_t kMaxWidth = 50000;
const size_t kMaxHeight = 50000;
const size_t kMaxPixels = 20 * (1 << 20);  // 20 MP
const size_t kMaxBitDepth = 24;  // The maximum reasonable bit depth supported.

std::mutex stderr_mutex;

typedef std::function<uint8_t()> PixelGenerator;

struct ImageSpec {
  bool Validate() const {
    if (width > kMaxWidth || height > kMaxHeight ||
        width * height > kMaxPixels) {
      return false;
    }
    if (bit_depth > kMaxBitDepth || bit_depth == 0) return false;
    if (num_frames == 0) return false;
    return true;
  }

  friend std::ostream& operator<<(std::ostream& o, const ImageSpec& spec) {
    o << "ImageSpec<"
      << "size=" << spec.width << "x" << spec.height
      << " * chan=" << spec.num_channels << " depth=" << spec.bit_depth
      << " alpha=" << spec.alpha_bit_depth
      << " (premult=" << spec.alpha_is_premultiplied
      << ") x frames=" << spec.num_frames << " seed=" << spec.seed << ">";
    return o;
  }

  void SpecHash(uint8_t hash[16]) const {
    memset(hash, 0, 16);
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(this);
    uint64_t state = 0;
    for (size_t i = 0; i < sizeof(*this); ++i) {
      state = state * 113 + buf[i];
      hash[i % 16] ^= state;
    }
  }

  size_t width, height;
  // Number of channels *not* including alpha.
  size_t num_channels;
  size_t bit_depth;
  // Bit depth for the alpha channel. A value of 0 means no alpha channel.
  size_t alpha_bit_depth;
  bool alpha_is_premultiplied = false;

  // Number of frames, all the frames will have the same size.
  size_t num_frames;

  // The seed for the PRNG.
  uint32_t seed = 7777;

  // Flags used for compression.
  jxl::CompressParams params;
};

bool GenerateFile(const char* output_dir, const ImageSpec& spec) {
  {
    std::unique_lock<std::mutex> lock(stderr_mutex);
    std::cerr << "Generating " << spec << std::endl;
  }

  jxl::CodecInOut io;
  io.metadata.bits_per_sample = spec.bit_depth;
  io.metadata.alpha_bits = spec.alpha_bit_depth;
  io.dec_pixels = spec.width * spec.height;
  io.frames.clear();
  io.frames.reserve(spec.num_frames);

  jxl::ColorEncoding c;
  if (spec.num_channels == 1) {
    c = jxl::ColorEncoding::LinearSRGB(true);
  } else if (spec.num_channels == 3) {
    c = jxl::ColorEncoding::SRGB();
  }

  std::mt19937 mt(spec.seed);
  std::uniform_int_distribution<> dis(1, 6);
  PixelGenerator gen = [&]() -> uint8_t { return dis(mt); };

  for (uint32_t frame = 0; frame < spec.num_frames; frame++) {
    jxl::ImageBundle ib(&io.metadata);
    const jxl::PackedImage desc(
        spec.width, spec.height, io.metadata.color_encoding,
        /*has_alpha=*/spec.alpha_bit_depth != 0,
        /*alpha_is_premultiplied=*/spec.alpha_is_premultiplied,
        io.metadata.alpha_bits, io.metadata.bits_per_sample,
        false /* big_endian */, false /* flipped_y */);

    size_t bytes_per_pixel = desc.row_size / desc.xsize;
    std::vector<uint8_t> img_data(desc.row_size * desc.ysize, 0);
    for (size_t y = 0; y < spec.height; y++) {
      size_t pos = desc.row_size * y;
      for (size_t x = 0; x < spec.width; x++) {
        for (size_t b = 0; b < bytes_per_pixel; b++) {
          img_data[pos++] = gen();
        }
      }
    }

    const jxl::Span<const uint8_t> span(img_data.data(), img_data.size());
    if (!CopyTo(desc, span, nullptr, &ib)) {
      return false;
    }
    io.frames.push_back(std::move(ib));
  }

  // Compress the image.
  jxl::PaddedBytes compressed;
  jxl::AuxOut aux_out;
  jxl::PassesEncoderState passes_encoder_state;
  bool ok = jxl::EncodeFile(spec.params, &io, &passes_encoder_state,
                            &compressed, &aux_out, nullptr);
  if (!ok) return false;

  // Compute a checksum of the ImageSpec to name the file. This is just to keep
  // the output of this program repeatable.
  uint8_t checksum[16];
  spec.SpecHash(checksum);
  std::string hash_str(sizeof(checksum) * 2, ' ');
  static const char* hex_chars = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(checksum); i++) {
    hash_str[2 * i] = hex_chars[checksum[i] >> 4];
    hash_str[2 * i + 1] = hex_chars[checksum[i] % 0x0f];
  }
  std::string output_fn = std::string(output_dir) + "/" + hash_str + ".jxl";

  if (!jxl::WriteFile(compressed, output_fn)) return 1;
  {
    std::unique_lock<std::mutex> lock(stderr_mutex);
    std::cerr << "Stored " << output_fn << " size: " << compressed.size()
              << std::endl;
  }

  return true;
}

std::vector<jxl::CompressParams> CompressParamsList() {
  std::vector<jxl::CompressParams> ret;
  jxl::CompressParams default_params;
  default_params.speed_tier = jxl::SpeedTier::kTortoise;

  {
    jxl::CompressParams params = default_params;
    params.butteraugli_distance = 1.5;
    ret.push_back(params);
  }

  {
    // Lossless
    jxl::CompressParams params = default_params;
    params.modular_group_mode = true;
    params.color_transform = jxl::ColorTransform::kNone;
    params.quality_pair = {100, 100};
    params.options.predictor = {int(jxl::Predictor::Weighted)};
    ret.push_back(params);
  }

  return ret;
}

int main(int argc, const char** argv) {
  const char* dest_dir = "corpus";
  if (argc > 1) {
    dest_dir = argv[1];
  }

  struct stat st;
  if (stat(dest_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
    fprintf(stderr, "Output path \"%s\" is not a directory.\n", dest_dir);
    return 1;
  }

  // Create the corpus directory if doesn't already exist.
  std::mt19937 mt(77777);
  // std::uniform_int_distribution<> dis(1, 6);
  // auto gen = [&mt, &dis]() { return dis(mt); };
  // std::vector<

  std::vector<std::pair<uint32_t, uint32_t>> image_sizes = {
      {8, 8},
      {32, 32},
      {128, 128},
      // Degenerated cases.
      {10000, 1},
      {10000, 2},
      {1, 10000},
      {2, 10000},
      // Large case.
      {777, 256},
      {333, 1025},
  };
  const std::vector<jxl::CompressParams> params_list = CompressParamsList();

  std::vector<ImageSpec> specs;

  ImageSpec spec;
  for (auto img_size : image_sizes) {
    spec.width = img_size.first;
    spec.height = img_size.second;
    for (uint32_t bit_depth : {1, 2, 8, 16}) {
      spec.bit_depth = bit_depth;
      for (uint32_t num_channels : {1, 3}) {
        spec.num_channels = num_channels;
        for (uint32_t alpha_bit_depth : {0, 8, 16}) {
          spec.alpha_bit_depth = alpha_bit_depth;
          for (uint32_t num_frames : {1, 3}) {
            spec.num_frames = num_frames;

            for (const auto& params : params_list) {
              spec.params = params;

              if (alpha_bit_depth) {
                spec.alpha_is_premultiplied = mt() % 2;
              }
              if (spec.width * spec.height > 1000) {
                // Increase the encoder speed for larger images.
                spec.params.speed_tier = jxl::SpeedTier::kWombat;
              }
              spec.seed = mt() % 777777;
              if (!spec.Validate()) {
                std::cerr << "Skipping " << spec << std::endl;
              } else {
                specs.push_back(spec);
              }
            }
          }
        }
      }
    }
  }

  jxl::ThreadPoolInternal pool;
  pool.Run(0, specs.size(), jxl::ThreadPool::SkipInit(),
           [&specs, dest_dir](const int task, const int /* thread */) {
             const ImageSpec& spec = specs[task];
             GenerateFile(dest_dir, spec);
           });

  return 0;
}
