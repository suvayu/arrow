// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/compute/kernels/common.h"

#include "arrow/util/bit_util.h"
#include "arrow/util/bitmap_ops.h"

namespace arrow {

using internal::CopyBitmap;
using internal::InvertBitmap;

namespace compute {
namespace {

struct IsValidOperator {
  static void Call(KernelContext* ctx, const Scalar& in, Scalar* out) {
    checked_cast<BooleanScalar*>(out)->value = in.is_valid;
  }

  static void Call(KernelContext* ctx, const ArrayData& arr, ArrayData* out) {
    DCHECK_EQ(out->offset, 0);
    DCHECK_LE(out->length, arr.length);
    if (arr.buffers[0] != nullptr) {
      out->buffers[1] = arr.offset == 0
                            ? arr.buffers[0]
                            : SliceBuffer(arr.buffers[0], arr.offset / 8, arr.length / 8);
      out->offset = arr.offset % 8;
      return;
    }

    KERNEL_RETURN_IF_ERROR(ctx, ctx->AllocateBitmap(out->length).Value(&out->buffers[1]));

    if (arr.null_count == 0 || arr.buffers[0] == nullptr) {
      BitUtil::SetBitsTo(out->buffers[1]->mutable_data(), out->offset, out->length, true);
      return;
    }

    CopyBitmap(arr.buffers[0]->data(), arr.offset, arr.length,
               out->buffers[1]->mutable_data(), out->offset);
  }
};

struct IsNullOperator {
  static void Call(KernelContext* ctx, const Scalar& in, Scalar* out) {
    checked_cast<BooleanScalar*>(out)->value = !in.is_valid;
  }

  static void Call(KernelContext* ctx, const ArrayData& arr, ArrayData* out) {
    if (arr.null_count == 0 || arr.buffers[0] == nullptr) {
      BitUtil::SetBitsTo(out->buffers[1]->mutable_data(), out->offset, out->length,
                         false);
      return;
    }

    InvertBitmap(arr.buffers[0]->data(), arr.offset, arr.length,
                 out->buffers[1]->mutable_data(), out->offset);
  }
};

void MakeFunction(std::string name, std::vector<InputType> in_types, OutputType out_type,
                  ArrayKernelExec exec, FunctionRegistry* registry,
                  MemAllocation::type mem_allocation, bool can_write_into_slices) {
  Arity arity{static_cast<int>(in_types.size())};
  auto func = std::make_shared<ScalarFunction>(name, arity);

  ScalarKernel kernel(std::move(in_types), out_type, exec);
  kernel.null_handling = NullHandling::OUTPUT_NOT_NULL;
  kernel.can_write_into_slices = can_write_into_slices;
  kernel.mem_allocation = mem_allocation;

  DCHECK_OK(func->AddKernel(std::move(kernel)));
  DCHECK_OK(registry->AddFunction(std::move(func)));
}

}  // namespace

namespace internal {

void RegisterScalarValidity(FunctionRegistry* registry) {
  MakeFunction("is_valid", {ValueDescr::ANY}, boolean(),
               applicator::SimpleUnary<IsValidOperator>, registry,
               MemAllocation::NO_PREALLOCATE, /*can_write_into_slices=*/false);

  MakeFunction("is_null", {ValueDescr::ANY}, boolean(),
               applicator::SimpleUnary<IsNullOperator>, registry,
               MemAllocation::PREALLOCATE,
               /*can_write_into_slices=*/true);
}

}  // namespace internal
}  // namespace compute
}  // namespace arrow
