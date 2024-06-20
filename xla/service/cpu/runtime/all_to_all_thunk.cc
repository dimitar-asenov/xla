/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/service/cpu/runtime/all_to_all_thunk.h"

#include <memory>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "xla/service/buffer_assignment.h"
#include "xla/service/collective_ops_utils.h"
#include "xla/service/cpu/collectives_interface.h"
#include "xla/service/cpu/runtime/collective_thunk.h"
#include "xla/service/cpu/runtime/thunk.h"
#include "xla/shape.h"
#include "xla/shape_util.h"
#include "xla/tsl/concurrency/async_value_ref.h"
#include "tsl/platform/errors.h"
#include "tsl/platform/logging.h"
#include "tsl/platform/statusor.h"
#include "tsl/profiler/lib/traceme.h"

namespace xla::cpu {

absl::StatusOr<std::unique_ptr<AllToAllThunk>> AllToAllThunk::Create(
    Info info, OpParams op_params, OpBuffers op_buffers) {
  return absl::WrapUnique(
      new AllToAllThunk(std::move(info), op_params, std::move(op_buffers)));
}

AllToAllThunk::AllToAllThunk(Info info, OpParams op_params,
                             OpBuffers op_buffers)
    : CollectiveThunk(Kind::kAllToAll, info, op_params, std::move(op_buffers)) {
}

tsl::AsyncValueRef<AllToAllThunk::ExecuteEvent> AllToAllThunk::Execute(
    const ExecuteParams& params) {
  tsl::profiler::TraceMe trace([&] { return TraceMeEncode(); });

  TF_ASSIGN_OR_RETURN(OpDeviceMemory data, GetOpDeviceMemory(params));

  VLOG(3) << absl::StreamFormat(
      "AllToAll: #source_buffers=%d, #destination_buffers=%d",
      data.source.size(), data.destination.size());

  for (int i = 0; i < data.source.size(); ++i) {
    VLOG(3) << absl::StreamFormat(
        "  src: %s in slice %s (%p)", source_shape(i).ToString(true),
        source_buffer(i).ToString(), data.source[i].opaque());
  }

  for (int i = 0; i < data.destination.size(); ++i) {
    VLOG(3) << absl::StreamFormat(
        "  dst: %s in slice %s (%p)", destination_shape(i).ToString(true),
        destination_buffer(i).ToString(), data.destination[i].opaque());
  }

  return ExecuteWithCommunicator(
      params.collective_params,
      [&](const RendezvousKey& key, CollectivesCommunicator& comm) {
        const Shape& shape = destination_shape(0);

        absl::InlinedVector<const void*, 4> input_buffers;
        input_buffers.reserve(data.source.size());
        for (int i = 0; i < data.source.size(); ++i) {
          input_buffers.push_back(data.source[i].opaque());
        }

        absl::InlinedVector<void*, 4> output_buffers;
        output_buffers.reserve(data.destination.size());
        for (int i = 0; i < data.destination.size(); ++i) {
          output_buffers.push_back(data.destination[i].opaque());
        }

        TF_RETURN_IF_ERROR(comm.AllToAll(key, ShapeUtil::ByteSizeOf(shape),
                                         input_buffers, output_buffers,
                                         DefaultCollectiveTimeout()));

        return absl::OkStatus();
      });
}

}  // namespace xla::cpu
