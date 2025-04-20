// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2024 Second State INC

//===-- wasmedge/runtime/instance/global.h - Global Instance definition ---===//
//
// Part of the WasmEdge Project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the global instance definition in store manager.
///
//===----------------------------------------------------------------------===//
#pragma once

#include "ast/type.h"

#include <fstream>
#include <cstring>
#include <iostream>

namespace WasmEdge {
namespace Runtime {
namespace Instance {

class GlobalInstance {
public:
  GlobalInstance() = delete;
  GlobalInstance(const AST::GlobalType &GType,
                 ValVariant Val = uint128_t(0)) noexcept
      : GlobType(GType), Value(Val) {
    assuming(GType.getValType().isNumType() ||
             GType.getValType().isNullableRefType() ||
             !Val.get<RefVariant>().isNull());
  }

  /// Getter of global type.
  const AST::GlobalType &getGlobalType() const noexcept { return GlobType; }

  /// Getter of value.
  const ValVariant &getValue() const noexcept { return Value; }
  ValVariant &getValue() noexcept { return Value; }

  /// Setter of value.
  void setValue(const ValVariant &Val) noexcept { Value = Val; }

  void dump(std::ofstream &ofs) const noexcept {
    // TODO: uint32_t型以外の場合どうなるのか(int32_tとかは同じ値が出力された)
    uint32_t v32;
    uint64_t v64;
    uint128_t v128;

    switch (GlobType.getValType().getCode()) {
      case TypeCode::I32:
      case TypeCode::F32:
        v32 = Value.get<uint32_t>();
        ofs.write(reinterpret_cast<char*>(&v32), sizeof(uint32_t));
        break;
      case TypeCode::I64:
      case TypeCode::F64:
        v64 = Value.get<uint64_t>();
        ofs.write(reinterpret_cast<char*>(&v64), sizeof(uint64_t));
        break;
      default:
        v128 = Value.get<uint128_t>();
        ofs.write(reinterpret_cast<char*>(&v128), sizeof(uint128_t));
        break;
    }
  }

  void restore(std::ifstream &ifs)  noexcept {
    switch (GlobType.getValType().getCode()) {
      case TypeCode::I32:
      case TypeCode::F32:
        ifs.read(reinterpret_cast<char*>(&Value), sizeof(uint32_t));
        break;
      case TypeCode::I64:
      case TypeCode::F64:
        ifs.read(reinterpret_cast<char*>(&Value), sizeof(uint64_t));
        break;
      default:
        ifs.read(reinterpret_cast<char*>(&Value), sizeof(uint128_t));
        break;
    }
  }

private:
  /// \name Data of global instance.
  /// @{
  AST::GlobalType GlobType;
  alignas(16) ValVariant Value;
  /// @}
};

} // namespace Instance
} // namespace Runtime
} // namespace WasmEdge
