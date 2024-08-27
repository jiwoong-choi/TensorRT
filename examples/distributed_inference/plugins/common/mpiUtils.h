/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "common/assert.h"
#include "common/iBuffer.h"
// #include "common/utils/multiDeviceUtils.h"

#ifdef ENABLE_FP8
#include <cuda_fp8.h>
#endif
#ifdef ENABLE_BF16
#include <cuda_bf16.h>
#endif

#include <cstdlib>
#include <memory>
#include <mpi.h>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if ENABLE_MULTI_DEVICE
#include <nccl.h>
#endif // ENABLE_MULTI_DEVICE

#define TLLM_MPI_CHECK(cmd)                                                                        \
  do {                                                                                             \
    auto e = cmd;                                                                                  \
    TLLM_CHECK_WITH_INFO(e == MPI_SUCCESS, "Failed: MPI error %s:%d '%d'", __FILE__, __LINE__, e); \
  } while (0)

#if ENABLE_MULTI_DEVICE
#define TLLM_NCCL_CHECK(cmd)                                                                       \
  do {                                                                                             \
    ncclResult_t r = cmd;                                                                          \
    TLLM_CHECK_WITH_INFO(r == ncclSuccess, "Failed, NCCL error %s:%d '%s'\n", __FILE__, __LINE__,  \
                         ncclGetErrorString(r));                                                   \
  } while (0)
#endif // ENABLE_MULTI_DEVICE

#define MPICHECK(cmd) TLLM_MPI_CHECK(cmd)

namespace tensorrt_llm::runtime {
using SizeType = std::int32_t;
}

// A wrapper module of the MPI library.
namespace tensorrt_llm::mpi {

// A wrapper of MPI data type. MpiType::{data_type}
enum class MpiType {
  kBYTE,
  kHALF,
  kFLOAT,
  kDOUBLE,
  kBOOL,
  kINT8,
  kUINT8,
  kINT32,
  kUINT32,
  kINT64,
  kUINT64,
  kFP8,
  kBF16,
  kCHAR,
};

//! \brief For converting a C++ data type to a TensorRT data type.
template <typename T> struct MpiTypeConverter {};

template <> struct MpiTypeConverter<std::byte> {
  static constexpr auto value = MpiType::kBYTE;
};

template <>

struct MpiTypeConverter<half>

{
  static constexpr auto value = MpiType::kHALF;
};

template <> struct MpiTypeConverter<float> {
  static constexpr auto value = MpiType::kFLOAT;
};

template <> struct MpiTypeConverter<double> {
  static constexpr auto value = MpiType::kDOUBLE;
};

template <> struct MpiTypeConverter<bool> {
  static constexpr auto value = MpiType::kBOOL;
};

template <> struct MpiTypeConverter<std::int8_t> {
  static constexpr auto value = MpiType::kINT8;
};

template <>
struct MpiTypeConverter<std::uint8_t>

{
  static constexpr auto value = MpiType::kUINT8;
};

template <> struct MpiTypeConverter<std::int32_t> {
  static constexpr auto value = MpiType::kINT32;
};

template <> struct MpiTypeConverter<std::uint32_t> {
  static constexpr auto value = MpiType::kUINT32;
};

template <> struct MpiTypeConverter<std::int64_t> {
  static constexpr auto value = MpiType::kINT64;
};

template <> struct MpiTypeConverter<std::uint64_t> {
  static constexpr auto value = MpiType::kUINT64;
};

template <> struct MpiTypeConverter<char> {
  static constexpr auto value = MpiType::kCHAR;
};

#ifdef ENABLE_FP8
template <> struct MpiTypeConverter<__nv_fp8_e4m3> {
  static constexpr auto value = MpiType::kFP8;
};
#endif

#ifdef ENABLE_BF16
template <> struct MpiTypeConverter<__nv_bfloat16> {
  static constexpr auto value = MpiType::kBF16;
};
#endif

// A wrapper of MPI_Op type.
enum class MpiOp {
  NULLOP,
  MAX,
  MIN,
  SUM,
  PROD,
  LAND,
  BAND,
  LOR,
  BOR,
  LXOR,
  BXOR,
  MINLOC,
  MAXLOC,
  REPLACE,
};

// A wrapper of the level of MPI thread support
enum class MpiThreadSupport : int {
  THREAD_SINGLE = MPI_THREAD_SINGLE,
  THREAD_FUNNELED = MPI_THREAD_FUNNELED,
  THREAD_SERIALIZED = MPI_THREAD_SERIALIZED,
  THREAD_MULTIPLE = MPI_THREAD_MULTIPLE,
};

class MpiRequest {
public:
  MpiRequest() = default;

  ~MpiRequest() = default;

  void wait() {
    // TODO: Don't ignore return status
    MPI_Wait(&mRequest, MPI_STATUS_IGNORE);
  }

  MPI_Request mRequest{};
};

MPI_Datatype getMpiDtype(MpiType dtype);

class MpiComm {
public:
  explicit MpiComm(MPI_Comm g, bool freeComm);
  ~MpiComm() noexcept;

  // no copy
  MpiComm(MpiComm const &) = delete;
  MpiComm &operator=(MpiComm const &) = delete;

  // move
  MpiComm(MpiComm &&) noexcept;
  MpiComm &operator=(MpiComm &&) noexcept;

  [[nodiscard]] int getRank() const;
  [[nodiscard]] int getSize() const;

  operator MPI_Comm() const // NOLINT(*-explicit-constructor)
  {
    return mComm;
  }

  //! \brief Returns the MPI world communicator.
  static MpiComm const &world();

  //! \brief Corresponds to `world()` by default, but can be overridden per process.
  static MpiComm &session();

  [[nodiscard]] MpiComm split(int color, int key) const;

  std::shared_ptr<MpiRequest> bcastAsync(void *buffer, size_t size, MpiType dtype, int root) const;

  std::shared_ptr<MpiRequest> bcastAsync(runtime::IBuffer &buf, int root) const {
    TLLM_CHECK(buf.getMemoryType() != runtime::MemoryType::kGPU);
    return bcastAsync(buf.data(), buf.getSizeInBytes(), MpiType::kBYTE, root);
  }

  void bcast(void *buffer, size_t size, MpiType dtype, int root) const;

  void bcast(runtime::IBuffer &buf, int root) const {
    bcast(buf.data(), buf.getSizeInBytes(), MpiType::kBYTE, root);
  }

  template <typename T> void bcastValue(T &value, int root) const {
    if constexpr (std::is_fundamental_v<std::remove_cv_t<T>>) {
      bcast(&value, 1, MpiTypeConverter<std::remove_cv_t<T>>::value, root);
    } else {
      bcast(&value, sizeof(T), MpiType::kBYTE, root);
    }
  }

  template <typename T> void bcast(std::vector<T> &vec, int root) const {
    auto const rank = getRank();
    auto vecSize = (rank == root) ? static_cast<int64_t>(vec.size()) : int64_t(0);
    bcast(&vecSize, 1, MpiType::kINT64, root);
    vec.resize(vecSize);

    if constexpr (std::is_fundamental_v<std::remove_cv_t<T>>) {
      auto const mpiType = MpiTypeConverter<std::remove_cv_t<T>>::value;
      bcast(vec.data(), vec.size(), mpiType, root);
    } else {
      bcast(vec.data(), vec.size() * sizeof(T), MpiType::kBYTE, root);
    }
  }

  void send(void const *buffer, std::size_t size, MpiType dtype, int dest, int tag) const;

  void send(runtime::IBuffer const &buf, int dest, int tag) const {
    TLLM_CHECK(buf.getMemoryType() != runtime::MemoryType::kGPU);
    send(buf.data(), buf.getSizeInBytes(), MpiType::kBYTE, dest, tag);
  }

  template <typename T> void send(T const &value, int dest, int tag) const {
    if constexpr (std::is_fundamental_v<std::remove_cv_t<T>>) {
      send(&value, 1, MpiTypeConverter<std::remove_cv_t<T>>::value, dest, tag);
    } else {
      send(&value, sizeof(T), MpiType::kBYTE, dest, tag);
    }
  }

  MPI_Status recv(void *buffer, size_t size, MpiType dtype, int source, int tag) const;

  MPI_Status recv(runtime::IBuffer &buf, int source, int tag) const {
    TLLM_CHECK(buf.getMemoryType() != runtime::MemoryType::kGPU);
    return recv(buf.data(), buf.getSizeInBytes(), MpiType::kBYTE, source, tag);
  }

  template <typename T> MPI_Status recv(T &value, int source, int tag) const {
    if constexpr (std::is_fundamental_v<std::remove_cv_t<T>>) {
      return recv(&value, 1, MpiTypeConverter<std::remove_cv_t<T>>::value, source, tag);
    } else {
      return recv(&value, sizeof(T), MpiType::kBYTE, source, tag);
    }
  }

  void allreduce(void const *sendbuf, void *recvbuf, int count, MpiType dtype, MpiOp op) const;
  void allgather(void const *sendbuf, void *recvbuf, int count, MpiType dtype) const;
  void barrier() const;

  void mprobe(int source, int tag, MPI_Message *msg, MPI_Status *status) const;

  bool operator==(MpiComm const &rhs) const { return mComm == rhs.mComm; }

  bool operator!=(MpiComm const &rhs) const { return !(rhs == *this); }

private:
  MPI_Comm mComm;
  bool mFreeComm;
};

void initialize(MpiThreadSupport threadMode = MpiThreadSupport::THREAD_FUNNELED);

} // namespace tensorrt_llm::mpi

#define COMM_SESSION tensorrt_llm::mpi::MpiComm::session()
