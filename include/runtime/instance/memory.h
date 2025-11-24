// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

//===-- wasmedge/runtime/instance/memory.h - Memory Instance definition ---===//
//
// Part of the WasmEdge Project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the memory instance definition in store manager.
///
//===----------------------------------------------------------------------===//
#pragma once

#include "ast/type.h"
#include "common/errcode.h"
#include "common/errinfo.h"
#include "common/log.h"
#include "system/allocator.h"

#include <cstdio>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <utility>
#include <iostream>
#include <vector>

namespace WasmEdge {
namespace Runtime {
namespace Instance {

class MemoryInstance {

public:
  static inline constexpr const uint64_t kPageSize = UINT64_C(65536);
  static inline constexpr const uint64_t k4G = UINT64_C(0x100000000);
  MemoryInstance() = delete;
  MemoryInstance(MemoryInstance &&Inst) noexcept
      : MemType(Inst.MemType), DataPtr(Inst.DataPtr),
        PageLimit(Inst.PageLimit) {
    Inst.DataPtr = nullptr;
  }
  MemoryInstance(const AST::MemoryType &MType,
                 uint32_t PageLim = UINT32_C(65536)) noexcept
      : MemType(MType), PageLimit(PageLim) {
    if (MemType.getLimit().getMin() > PageLimit) {
      spdlog::error(
          "Create memory instance failed -- exceeded limit page size: {}",
          PageLimit);
      return;
    }
    DataPtr = Allocator::allocate(MemType.getLimit().getMin());
    if (DataPtr == nullptr) {
      spdlog::error("Unable to find usable memory address");
      return;
    }
  }
  ~MemoryInstance() noexcept {
    Allocator::release(DataPtr, MemType.getLimit().getMin());
  }

  bool isShared() const noexcept { return MemType.getLimit().isShared(); }

  /// Get page size of memory.data
  uint32_t getPageSize() const noexcept {
    // The memory page size is binded with the limit in memory type.
    return MemType.getLimit().getMin();
  }

  /// Getter of memory type.
  const AST::MemoryType &getMemoryType() const noexcept { return MemType; }

  /// Check access size is valid.
  bool checkAccessBound(uint32_t Offset, uint32_t Length) const noexcept {
    const uint64_t AccessLen =
        static_cast<uint64_t>(Offset) + static_cast<uint64_t>(Length);
    return AccessLen <= MemType.getLimit().getMin() * kPageSize;
  }

  /// Get boundary index.
  uint32_t getBoundIdx() const noexcept {
    return MemType.getLimit().getMin() > 0
               ? MemType.getLimit().getMin() * kPageSize - 1
               : 0;
  }

  /// Grow page
  bool growPage(const uint32_t Count) {
    if (Count == 0) {
      return true;
    }
    // Maximum pages count, 65536
    uint32_t MaxPageCaped = k4G / kPageSize;
    uint32_t Min = MemType.getLimit().getMin();
    uint32_t Max = MemType.getLimit().getMax();
    if (MemType.getLimit().hasMax()) {
      MaxPageCaped = std::min(Max, MaxPageCaped);
    }
    if (Count + Min > MaxPageCaped) {
      return false;
    }
    if (Count + Min > PageLimit) {
      spdlog::error("Memory grow page failed -- exceeded limit page size: {}",
                    PageLimit);
      return false;
    }
    if (auto NewPtr = Allocator::resize(DataPtr, Min, Min + Count);
        NewPtr == nullptr) {
      return false;
    } else {
      DataPtr = NewPtr;
    }
    MemType.getLimit().setMin(Min + Count);
    return true;
  }

  /// Get slice of Data[Offset : Offset + Length - 1]
  Expect<Span<Byte>> getBytes(uint32_t Offset, uint32_t Length) const noexcept {
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }
    return Span<Byte>(&DataPtr[Offset], Length);
  }

  /// Replace the bytes of Data[Offset :] by Slice[Start : Start + Length - 1]
  Expect<void> setBytes(Span<const Byte> Slice, uint32_t Offset, uint32_t Start,
                        uint32_t Length) noexcept {
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }

    // Check the input data validation.
    if (unlikely(Start + Length > Slice.size())) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }

    // Copy the data.
    if (likely(Length > 0)) {
      std::copy(Slice.begin() + Start, Slice.begin() + Start + Length,
                DataPtr + Offset);
    }
    return {};
  }

  /// Fill the bytes of Data[Offset : Offset + Length - 1] by Val.
  Expect<void> fillBytes(uint8_t Val, uint32_t Offset,
                         uint32_t Length) noexcept {
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }

    // Copy the data.
    if (likely(Length > 0)) {
      std::fill(DataPtr + Offset, DataPtr + Offset + Length, Val);
    }
    return {};
  }

  /// Get an uint8 array from Data[Offset : Offset + Length - 1]
  Expect<void> getArray(uint8_t *Arr, uint32_t Offset, uint32_t Length,
                        bool IsReverse = false) const noexcept {
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }
    if (likely(Length > 0)) {
      // Copy the data.
      if (IsReverse) {
        std::reverse_copy(DataPtr + Offset, DataPtr + Offset + Length, Arr);
      } else {
        std::copy(DataPtr + Offset, DataPtr + Offset + Length, Arr);
      }
    }
    return {};
  }

  /// Replace Data[Offset : Offset + Length - 1] to an uint8 array
  Expect<void> setArray(const uint8_t *Arr, uint32_t Offset, uint32_t Length,
                        bool IsReverse = false) noexcept {
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }
    if (likely(Length > 0)) {
      // Copy the data.
      if (IsReverse) {
        std::reverse_copy(Arr, Arr + Length, DataPtr + Offset);
      } else {
        std::copy(Arr, Arr + Length, DataPtr + Offset);
      }
    }
    return {};
  }

  /// Get pointer to specific offset of memory or null.
  template <typename T>
  typename std::enable_if_t<std::is_pointer_v<T>, T>
  getPointerOrNull(uint32_t Offset) const noexcept {
    if (Offset == 0 ||
        unlikely(!checkAccessBound(Offset, sizeof(std::remove_pointer_t<T>)))) {
      return nullptr;
    }
    return reinterpret_cast<T>(&DataPtr[Offset]);
  }

  /// Get pointer to specific offset of memory.
  template <typename T>
  typename std::enable_if_t<std::is_pointer_v<T>, T>
  getPointer(uint32_t Offset) const noexcept {
    using Type = std::remove_pointer_t<T>;
    uint32_t ByteSize = static_cast<uint32_t>(sizeof(Type));
    if (unlikely(!checkAccessBound(Offset, ByteSize))) {
      return nullptr;
    }
    return reinterpret_cast<T>(&DataPtr[Offset]);
  }

  /// Get array of object at specific offset of memory.
  template <typename T>
  Span<T> getSpan(uint32_t Offset, uint32_t Size) const noexcept {
    uint32_t ByteSize = static_cast<uint32_t>(sizeof(T) * Size);
    if (unlikely(!checkAccessBound(Offset, ByteSize))) {
      return Span<T>();
    }
    return Span<T>(reinterpret_cast<T *>(&DataPtr[Offset]), Size);
  }

  /// Get array of object at specific offset of memory.
  std::string_view getStringView(uint32_t Offset,
                                 uint32_t Size) const noexcept {
    if (unlikely(!checkAccessBound(Offset, Size))) {
      return {};
    }
    return {reinterpret_cast<const char *>(&DataPtr[Offset]), Size};
  }

  /// Template of loading bytes and convert to a value.
  ///
  /// Load the length of vector and construct into a value.
  /// Only output value of int32, uint32, int64, uint64, float, and double are
  /// allowed.
  ///
  /// \param Value the constructed output value.
  /// \param Offset the start offset in data array.
  ///
  /// \returns void when success, ErrCode when failed.
  template <typename T, uint32_t Length = sizeof(T)>
  typename std::enable_if_t<IsWasmNumV<T>, Expect<void>>
  loadValue(T &Value, uint32_t Offset) const noexcept {
    // Check the data boundary.
    static_assert(Length <= sizeof(T));
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }
    // Load the data to the value.
    if (likely(Length > 0)) {
      if constexpr (std::is_floating_point_v<T>) {
        // Floating case. Do the memory copy.
        std::memcpy(&Value, &DataPtr[Offset], sizeof(T));
      } else {
        if constexpr (sizeof(T) > 8) {
          assuming(sizeof(T) == 16);
          Value = 0;
          std::memcpy(&Value, &DataPtr[Offset], Length);
        } else {
          uint64_t LoadVal = 0;
          // Integer case. Extends to the result type.
          std::memcpy(&LoadVal, &DataPtr[Offset], Length);
          if (std::is_signed_v<T> && (LoadVal >> (Length * 8 - 1))) {
            // Signed extension.
            for (unsigned int I = Length; I < 8; I++) {
              LoadVal |= 0xFFULL << (I * 8);
            }
          }
          Value = static_cast<T>(LoadVal);
        }
      }
    }
    return {};
  }

  /// Template of loading bytes and convert to a value.
  ///
  /// Destruct and Store the value to length of vector.
  /// Only input value of uint32, uint64, float, and double are allowed.
  ///
  /// \param Value the value want to store into data array.
  /// \param Offset the start offset in data array.
  ///
  /// \returns void when success, ErrCode when failed.
  template <typename T, uint32_t Length = sizeof(T)>
  typename std::enable_if_t<IsWasmNativeNumV<T>, Expect<void>>
  storeValue(const T &Value, uint32_t Offset) noexcept {
    // Check the data boundary.
    static_assert(Length <= sizeof(T));
    // Check the memory boundary.
    if (unlikely(!checkAccessBound(Offset, Length))) {
      spdlog::error(ErrCode::Value::MemoryOutOfBounds);
      spdlog::error(ErrInfo::InfoBoundary(Offset, Length, getBoundIdx()));
      return Unexpect(ErrCode::Value::MemoryOutOfBounds);
    }
    // Copy the stored data to the value.
    if (likely(Length > 0)) {
      std::memcpy(&DataPtr[Offset], &Value, Length);
    }
    return {};
  }

  uint8_t *getDataPtr() const noexcept { return DataPtr; }

  // Migration functions

  // filename = meminst_{i}
  Expect<void> dump(std::string filename) {
    if (auto Res = dumpMemType(filename); !Res) {
      return Unexpect(Res);
    }
    
    if (auto Res = dumpDataPtr(filename); !Res) {
      return Unexpect(Res);
    }

    return {};
  }
  
  Expect<void> dumpMemType(std::string filename) {
    // Open file
    filename = filename + "mem_page_count.img";
    // filename = filename + "_memtype.img";
    uint32_t CurPageCount = MemType.getLimit().getMin();

    std::ofstream fout;
    fout.open(filename, std::ios::trunc | std::ios::binary);
    fout.write(reinterpret_cast<char *>(&CurPageCount), sizeof(CurPageCount));

    fout.close();
    return {};
  }

  bool IsDirtyPage(uint64_t PageMapEntry) {
    return (PageMapEntry >> 62 & 1) | (PageMapEntry >> 63 & 1);
  }

  bool IsSoftDirtyPage(uint64_t PageMapEntry) {
    return (PageMapEntry >> 55 & 1);
  }

  Expect<int> dumpDirtyMemory(Span<Byte> &Data, std::ofstream &ofs) {
    const uint32_t PAGEMAP_LENGTH = 8;
    const uint32_t PAGE_SIZE = 4096;

    // プロセスのpagemapを開く
    FILE* PageMap = fopen("/proc/self/pagemap", "rb");
    if (PageMap == NULL) {
        perror("Error opening pagemap");
        return -1;
    }

    auto PageMapGuard = std::unique_ptr<FILE, decltype(&fclose)>(PageMap, &fclose);

    const auto IsZeroPage = [](const uint8_t *Page, uint32_t Size) noexcept {
      const uint64_t *Page64 = reinterpret_cast<const uint64_t *>(Page);
      const uint32_t Count64 = Size / sizeof(uint64_t);
      for (uint32_t I = 0; I < Count64; ++I) {
        if (Page64[I] != 0) {
          return false;
        }
      }
      for (uint32_t I = Count64 * sizeof(uint64_t); I < Size; ++I) {
        if (Page[I] != 0) {
          return false;
        }
      }
      return true;
    };

    uint8_t* BegAddr = Data.data();
    uint8_t* EndAddr = BegAddr + Data.size();
    uint64_t PageMapEntry;

    uint32_t ZeroRunStart = 0;
    uint32_t ZeroRunCount = 0;

    const auto FlushZeroRun = [&](std::ofstream &Stream) -> bool {
      if (ZeroRunCount == 0) {
        return true;
      }
      constexpr uint8_t ZeroRunType = 2;
      Stream.write(reinterpret_cast<const char *>(&ZeroRunType), sizeof(uint8_t));
      Stream.write(reinterpret_cast<const char *>(&ZeroRunStart), sizeof(uint32_t));
      Stream.write(reinterpret_cast<const char *>(&ZeroRunCount), sizeof(uint32_t));
      if (!Stream) {
        return false;
      }
      ZeroRunStart = 0;
      ZeroRunCount = 0;
      return true;
    };

    for (uint8_t* Addr = BegAddr; Addr < EndAddr; Addr += PAGE_SIZE) {
      uint64_t pfn = (uint64_t)Addr / PAGE_SIZE;
      off64_t Offset = sizeof(uint64_t) * pfn;

      if (fseek(PageMap, Offset, SEEK_SET) == -1) {
          perror("[ERROR]Failed seeking to pagemap entry");
          return -1;
      }

      if (fread(&PageMapEntry, PAGEMAP_LENGTH, 1, PageMap) == 0) {
          perror("[ERROR]Failed reading pagemap entry");
          return -1;
      }

      if (!IsDirtyPage(PageMapEntry)) {
        if (!FlushZeroRun(ofs)) {
          return -1;
        }
        continue;
      }

      if (IsZeroPage(Addr, PAGE_SIZE)) {
        if (ZeroRunCount == 0) {
          ZeroRunStart = static_cast<uint32_t>(Addr - BegAddr);
        }
        ++ZeroRunCount;
        continue;
      }

      if (!FlushZeroRun(ofs)) {
        return -1;
      }

      constexpr uint8_t DirtyType = 1;
      ofs.write(reinterpret_cast<const char *>(&DirtyType), sizeof(uint8_t));
      if (!ofs) {
        return -1;
      }
      uint32_t MemInstAddr = Addr - BegAddr;
      ofs.write(reinterpret_cast<char *>(&MemInstAddr), sizeof(uint32_t));
      if (!ofs) {
        return -1;
      }
      ofs.write(reinterpret_cast<char *>(Addr), PAGE_SIZE);
      if (!ofs) {
        return -1;
      }
    }
    if (!FlushZeroRun(ofs)) {
      return -1;
    }
    return 0;
  }
  
  Expect<void> dumpDataPtr(std::string filename) {
    // Open file
    // filename = filename + "_dataptr.img";
    filename = filename + "memory.img";
    std::ofstream ofs(filename, std::ios::trunc | std::ios::binary);
    if (!ofs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }

    // DataPtrをfileにdump
    auto Res = getBytes(0, MemType.getLimit().getMin() * kPageSize);
    if (unlikely(!Res)) {
      return Unexpect(Res);
    }
    Span<Byte> Data = Res.value();
    dumpDirtyMemory(Data, ofs);
    ofs.close();

    // デバッグのために全部吐き出すやつもやる
    // std::ofstream ofs2("all_memory.img", std::ios::trunc | std::ios::binary);
    // if (!ofs2) {
    //   return Unexpect(ErrCode::Value::IllegalPath);
    // }
    // ofs2.write(reinterpret_cast<char*>(Data.data()), Data.size());
    // ofs2.close();
    return {};
  }
  
  
  Expect<void> restore(std::string filename) noexcept {
    // Restore MemType
    uint32_t oldPageSize = getPageSize();
    uint32_t newPageSize;

    if (auto Res = restoreMemType(filename)) {
      newPageSize = Res.value();
      // 新しいページサイズが前のページサイズを下回ることはないはず
      // static_assert(newPageSize >= oldPageSize);

      if (growPage(newPageSize - oldPageSize)) {
        MemType.getLimit().setMin(newPageSize);
      }
      else {
        return Unexpect(ErrCode::Value::Terminated);
      }
    }
    else {
      return Unexpect(Res);
    }
    
    // Restore DataPtr
    if (auto Res = restoreDirtyMemory(filename); !Res) {
      return Unexpect(Res);
    }

    return {};
  }
  
  Expect<uint32_t> restoreMemType(std::string filename) {
    // Restore MemType
    // filename = filename + "_memtype.img";
    filename = filename + "mem_page_count.img";
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }
    
    uint32_t mem_page_count;
    ifs.read(reinterpret_cast<char*>(&mem_page_count), sizeof(uint32_t));
    ifs.close();

    return mem_page_count;
  }

  Expect<void> restoreDirtyMemory(std::string filename) {
    filename = filename + "memory.img";
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }

    const uint32_t PAGE_SIZE = 4096;
    std::vector<uint8_t> memory(PAGE_SIZE);
    while (true) {
      uint8_t RecordType = 0;
      if (!ifs.read(reinterpret_cast<char *>(&RecordType), sizeof(uint8_t))) {
        if (ifs.eof()) {
          break;
        }
        return Unexpect(ErrCode::Value::IllegalPath);
      }

      if (RecordType == 1) {
        uint32_t MemInstAddr = 0;
        if (!ifs.read(reinterpret_cast<char *>(&MemInstAddr), sizeof(uint32_t))) {
          return Unexpect(ErrCode::Value::IllegalPath);
        }
        if (!ifs.read(reinterpret_cast<char *>(memory.data()), PAGE_SIZE)) {
          return Unexpect(ErrCode::Value::IllegalPath);
        }
        if (auto Res = setBytes(Span<Byte>(memory), MemInstAddr, 0, PAGE_SIZE); !Res) {
          return Unexpect(Res);
        }
      } else if (RecordType == 2) {
        uint32_t ZeroRunStart = 0;
        uint32_t ZeroRunCount = 0;
        if (!ifs.read(reinterpret_cast<char *>(&ZeroRunStart), sizeof(uint32_t))) {
          return Unexpect(ErrCode::Value::IllegalPath);
        }
        if (!ifs.read(reinterpret_cast<char *>(&ZeroRunCount), sizeof(uint32_t))) {
          return Unexpect(ErrCode::Value::IllegalPath);
        }
        for (uint32_t I = 0; I < ZeroRunCount; ++I) {
          uint32_t PageOffset = ZeroRunStart + I * PAGE_SIZE;
          if (auto Res = fillBytes(0, PageOffset, PAGE_SIZE); !Res) {
            return Unexpect(Res);
          }
        }
      } else {
        return Unexpect(ErrCode::Value::IllegalPath);
      }
    }

    ifs.close();
    return {};
  }
  
  Expect<std::vector<uint8_t>> restoreDataPtr(std::string filename) {
    // filename = filename + "_dataptr.img";
    filename = filename + "all_memory.img";
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }
    // ファイルのサイズを取得
    ifs.seekg(0, std::ios::end);
    int length = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> vec(length);
    ifs.read(reinterpret_cast<char*>(vec.data()), length);
    if (!ifs) {
      // static_assert(ifs, "dataptr.imgから読み込みが成功しなかった");      
    }
    ifs.close();
    return vec;
  }

private:
  /// \name Data of memory instance.
  /// @{
  AST::MemoryType MemType;
  uint8_t *DataPtr = nullptr;
  const uint32_t PageLimit;
  /// @}
};

} // namespace Instance
} // namespace Runtime
} // namespace WasmEdge
