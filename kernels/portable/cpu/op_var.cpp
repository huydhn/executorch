/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cmath>

#include <executorch/kernels/portable/cpu/util/reduce_util.h>
#include <executorch/runtime/kernel/kernel_includes.h>
#include <executorch/runtime/platform/assert.h>

namespace torch {
namespace executor {
namespace native {

using Tensor = exec_aten::Tensor;
using ScalarType = exec_aten::ScalarType;

namespace {

void check_preconditions(
    const Tensor& in,
    const optional<ArrayRef<int64_t>>& dim_list,
    bool keepdim,
    Tensor& out) {
  check_dim_list_is_valid(in, dim_list);
  ET_CHECK_MSG(
      out.dim() == compute_reduced_out_dim(in, dim_list, keepdim),
      "Number of dims of out tensor is not compatible with inputs and params");
  ET_CHECK_DEFAULT_OR_CHANNELSLAST_DIMORDER(in);
  ET_CHECK_DEFAULT_OR_CHANNELSLAST_DIMORDER(out);
}

} // namespace

Tensor& var_out(
    RuntimeContext& ctx,
    const Tensor& in,
    optional<ArrayRef<int64_t>> dim_list,
    bool unbiased,
    bool keepdim,
    Tensor& out) {
  (void)ctx;

  check_preconditions(in, dim_list, keepdim, out);

  Error e = resize_reduction_out(in, dim_list, keepdim, out);
  ET_CHECK_MSG(e == Error::Ok, "Failed to resize out tensor in var_out");

  ET_SWITCH_FLOAT_TYPES(in.scalar_type(), ctx, "var.out", CTYPE_IN, [&] {
    ET_SWITCH_FLOAT_TYPES(out.scalar_type(), ctx, "var.out", CTYPE_OUT, [&] {
      CTYPE_OUT* out_data = out.mutable_data_ptr<CTYPE_OUT>();
      const size_t num = get_reduced_dim_product(in, dim_list);
      const size_t denominator = unbiased ? num - 1 : num;
      if (num == 0 || denominator == 0) {
        for (size_t out_ix = 0; out_ix < out.numel(); ++out_ix) {
          out_data[out_ix] = NAN;
        }
      } else {
        for (size_t out_ix = 0; out_ix < out.numel(); ++out_ix) {
          CTYPE_OUT sum = map_reduce_over_dim_list<CTYPE_IN, CTYPE_OUT>(
              [](CTYPE_IN v) { return static_cast<CTYPE_OUT>(v); },
              [](CTYPE_OUT outv, CTYPE_OUT acc) { return acc + outv; },
              in,
              dim_list,
              out_ix);
          CTYPE_OUT mean = sum / num;
          CTYPE_OUT sum2 = map_reduce_over_dim_list<CTYPE_IN, CTYPE_OUT>(
              [mean](CTYPE_IN v) {
                return (
                    (static_cast<CTYPE_OUT>(v) - mean) *
                    (static_cast<CTYPE_OUT>(v) - mean));
              },
              [](CTYPE_OUT outv, CTYPE_OUT acc) { return acc + outv; },
              in,
              dim_list,
              out_ix);
          out_data[out_ix] = sum2 / denominator;
        }
      }
    });
  });

  return out;
}

} // namespace native
} // namespace executor
} // namespace torch
