// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

#include "executor/executor.h"

#include <cstdint>

namespace WasmEdge {
namespace Executor {

Expect<void> Executor::runLocalGetOp(Runtime::StackManager &StackMgr,
                                     uint32_t StackOffset) const noexcept {
   StackMgr.push(StackMgr.getTopN(StackOffset), StackMgr.getTypeTopN(StackOffset));
  return {};
}

Expect<void> Executor::runLocalSetOp(Runtime::StackManager &StackMgr,
                                     uint32_t StackOffset) const noexcept {
  StackMgr.getTopN(StackOffset - 1) = StackMgr.pop();
  return {};
}

Expect<void> Executor::runLocalTeeOp(Runtime::StackManager &StackMgr,
                                     uint32_t StackOffset) const noexcept {
  const ValVariant &Val = StackMgr.getTop();
  StackMgr.getTopN(StackOffset) = Val;
  return {};
}

Expect<void> Executor::runGlobalGetOp(Runtime::StackManager &StackMgr,
                                      uint32_t Idx) const noexcept {
  auto *GlobInst = getGlobInstByIdx(StackMgr, Idx);
  assuming(GlobInst);
  ValType T = GlobInst->getGlobalType().getValType();
  if (T == ValType::I32 || T == ValType::F32) {
    StackMgr.push(GlobInst->getValue());
    StackMgr.getTypeTop() = 1;
  }
  else if (T == ValType::I64 || T == ValType::F64) {
    StackMgr.push(GlobInst->getValue());
    StackMgr.getTypeTop() = 2;
  }
  else {
    StackMgr.push(GlobInst->getValue());
    StackMgr.getTypeTop() = 4;
  }
  // std::cout << "[DEBUG]push stack: type kind: " << +StackMgr.getTypeTop() << ", Pos: " << StackMgr.getValueStack().size() << " " << StackMgr.getTypeStack().size() << std::endl;
  return {};
}

Expect<void> Executor::runGlobalSetOp(Runtime::StackManager &StackMgr,
                                      uint32_t Idx) const noexcept {
  auto *GlobInst = getGlobInstByIdx(StackMgr, Idx);
  assuming(GlobInst);
  GlobInst->getValue() = StackMgr.pop();
  return {};
}

} // namespace Executor
} // namespace WasmEdge
