// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

//===-- wasmedge/runtime/stackmgr.h - Stack Manager definition ------------===//
//
// Part of the WasmEdge Project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of Stack Manager.
///
//===----------------------------------------------------------------------===//
#pragma once

#include "ast/instruction.h"
#include "runtime/instance/module.h"
// #include "executor/migrator.h"

#include <vector>

namespace WasmEdge {
namespace Runtime {

class StackManager {
public:
  struct Frame {
    Frame() = delete;
    Frame(const Instance::ModuleInstance *Mod, AST::InstrView::iterator FromIt, const Instance::FunctionInstance *Func,
          uint32_t L, uint32_t A, uint32_t V) noexcept
        : Module(Mod), From(FromIt), EnterFunc(Func), Locals(L), Arity(A), VPos(V) {}
    const Instance::ModuleInstance *Module;
    AST::InstrView::iterator From;
    const Instance::FunctionInstance *EnterFunc;
    uint32_t Locals;
    uint32_t Arity;
    uint32_t VPos;
  };

  using Value = ValVariant;

  /// Stack manager provides the stack control for Wasm execution with VALIDATED
  /// modules. All operations of instructions passed validation, therefore no
  /// unexpect operations will occur.
  StackManager() noexcept {
    ValueStack.reserve(2048U);
    TypeStack.reserve(2048U);
    FrameStack.reserve(16U);
  }
  ~StackManager() = default;

  /// Getter of stack size.
  size_t size() const noexcept { return ValueStack.size(); }

  /// Unsafe Getter of top entry of stack.
  Value &getTop() { return ValueStack.back(); }

  /// Unsafe Getter of top N-th value entry of stack.
  Value &getTopN(uint32_t Offset) noexcept {
    assuming(0 < Offset && Offset <= ValueStack.size());
    return ValueStack[ValueStack.size() - Offset];
  }
  
  uint8_t &getTypeTopN(uint32_t Offset) noexcept {
    assuming(0 < Offset && Offset <= TypeStack.size());
    return TypeStack[TypeStack.size() - Offset];
  }

  uint8_t &getTypeTop() noexcept {
    return TypeStack.back();
  }
  
  /// Unsafe Getter of top N value entries of stack.
  Span<Value> getTopSpan(uint32_t N) {
    return Span<Value>(ValueStack.end() - N, N);
  }

  /// Push a new value entry to stack.
  template <typename T> void push(T &&Val) {
    ValueStack.push_back(std::forward<T>(Val));
    // 32bitなら0, 64bitなら1
    size_t t = sizeof(T);
    TypeStack.push_back(std::forward<uint8_t>(t/4));
    // std::cout << "[DEBUG]push stack: type kind: " << +TypeStack.back() << ", Pos: " << ValueStack.size() << " " << TypeStack.size() << std::endl;
  }

  template <typename T, typename U> void push(T &&Val, U &&Typ) {
    ValueStack.push_back(std::forward<T>(Val));
    TypeStack.push_back(std::forward<U>(Typ));
  }

  /// Unsafe Pop and return the top entry.
  Value pop() {
    Value V = std::move(ValueStack.back());
    ValueStack.pop_back();
    TypeStack.pop_back();
    return V;
  }

  void _pushFrame(const Instance::ModuleInstance *Module,
                 AST::InstrView::iterator From, const Runtime::Instance::FunctionInstance *EnterFunc, 
                 uint32_t LocalNum, uint32_t Arity, uint32_t VPos, bool IsTailCall) noexcept {
    if (likely(!IsTailCall)) {
      FrameStack.emplace_back(Module, From, EnterFunc, LocalNum, Arity, VPos);
    } else {
      assuming(!FrameStack.empty());
      assuming(FrameStack.back().VPos >= FrameStack.back().Locals);
      assuming(FrameStack.back().VPos - FrameStack.back().Locals <=
               ValueStack.size() - LocalNum);
      ValueStack.erase(ValueStack.begin() + FrameStack.back().VPos -
                           FrameStack.back().Locals,
                       ValueStack.end() - LocalNum);
      TypeStack.erase(TypeStack.begin() + FrameStack.back().VPos -
                           FrameStack.back().Locals,
                       TypeStack.end() - LocalNum);

      FrameStack.back().Module = Module;
      FrameStack.back().Locals = LocalNum;
      FrameStack.back().EnterFunc = EnterFunc;
      FrameStack.back().Arity = Arity;
      FrameStack.back().VPos = VPos;
    }
  }
  
  /// Push a new frame entry to stack.
  void pushFrame(const Instance::ModuleInstance *Module,
                 AST::InstrView::iterator From, uint32_t LocalNum = 0,
                 uint32_t Arity = 0, bool IsTailCall = false) noexcept {

    _pushFrame(Module, From, nullptr, LocalNum, Arity, ValueStack.size(), IsTailCall);
  }

  void pushFrameExt(const Instance::ModuleInstance *Module,
                 AST::InstrView::iterator From, const Runtime::Instance::FunctionInstance *EnterFunc, 
                 uint32_t LocalNum = 0, uint32_t Arity = 0, bool IsTailCall = false) noexcept {
    _pushFrame(Module, From, EnterFunc, LocalNum, Arity, ValueStack.size(), IsTailCall);
  }

  /// Unsafe pop top frame.
  AST::InstrView::iterator popFrame() noexcept {
    assuming(!FrameStack.empty());
    assuming(FrameStack.back().VPos >= FrameStack.back().Locals);
    assuming(FrameStack.back().VPos - FrameStack.back().Locals <=
             ValueStack.size() - FrameStack.back().Arity);
    ValueStack.erase(ValueStack.begin() + FrameStack.back().VPos -
                         FrameStack.back().Locals,
                     ValueStack.end() - FrameStack.back().Arity);
    TypeStack.erase(TypeStack.begin() + FrameStack.back().VPos - 
                        FrameStack.back().Locals,
                     TypeStack.end() - FrameStack.back().Arity);
    auto From = FrameStack.back().From;
    FrameStack.pop_back();
    return From;
  }

  /// Unsafe erase stack.
  void stackErase(uint32_t EraseBegin, uint32_t EraseEnd) noexcept {
    assuming(EraseEnd <= EraseBegin && EraseBegin <= ValueStack.size());
    assuming(EraseEnd <= EraseBegin && EraseBegin <= TypeStack.size());
    ValueStack.erase(ValueStack.end() - EraseBegin,
                     ValueStack.end() - EraseEnd);
    TypeStack.erase(TypeStack.end() - EraseBegin,
                     TypeStack.end() - EraseEnd);
  }

  /// Unsafe leave top label.
  AST::InstrView::iterator maybePopFrame(AST::InstrView::iterator PC) noexcept {
    if (FrameStack.size() > 1 && PC->isLast()) {
      // Noted that there's always a base frame in stack.
      return popFrame();
    }
    return PC;
  }

  /// Unsafe getter of module address.
  const Instance::ModuleInstance *getModule() const noexcept {
    assuming(!FrameStack.empty());
    return FrameStack.back().Module;
  }

  /// Reset stack.
  void reset() noexcept {
    ValueStack.clear();
    FrameStack.clear();
  }

  // TODO: protectedとかつける
  std::vector<Frame> getFrameStack() {
    return FrameStack;
  }
  std::vector<Value> getValueStack() {
    return ValueStack;
  }
  std::vector<uint8_t> getTypeStack() {
    return TypeStack;
  }
  
private:
  /// \name Data of stack manager.
  /// @{
  std::vector<Value> ValueStack;
  std::vector<uint8_t> TypeStack;
  std::vector<Frame> FrameStack;
  /// @}
};

} // namespace Runtime
} // namespace WasmEdge
