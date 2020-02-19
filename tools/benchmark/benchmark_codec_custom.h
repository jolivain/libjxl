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

#ifndef TOOLS_BENCHMARK_BENCHMARK_CODEC_CUSTOM_H_
#define TOOLS_BENCHMARK_BENCHMARK_CODEC_CUSTOM_H_

// This is a benchmark codec that can be used with any command-line
// encoder/decoder that satisfies the following conditions:
//
// - the encoder can read from a PNG file `$input.png` and write the encoded
//   image to `$encoded.$ext` if it is called as:
//
//       $encoder [OPTIONS] $input.png $encoded.$ext
//
// - the decoder can read from an encoded file `$encoded.$ext` and write to a
//   PNG file `$decoded.png` if it is called as:
//
//       $decoder $encoded.$ext $decoded.png
//
// On the benchmark command line, the codec must be specified as:
//
//     custom:$ext:$encoder:$decoder:$options
//
// Where the options are also separated by colons.
//
// An example with JPEG XL itself would be:
//
//     custom:jxl:cjpegxl:djpegxl:--distance:3
//
// Optionally, to have encoding and decoding speed reported, the codec may write
// the number of seconds (as a floating point number) elapsed during actual
// encoding/decoding to $encoded.time and $decoded.time, respectively (replacing
// the .$ext and .png extensions).

#include "tools/benchmark/benchmark_args.h"
#include "tools/benchmark/benchmark_codec.h"

namespace jxl {

ImageCodec* CreateNewCustomCodec(const BenchmarkArgs& args);

}  // namespace jxl

#endif  // TOOLS_BENCHMARK_BENCHMARK_CODEC_CUSTOM_H_
