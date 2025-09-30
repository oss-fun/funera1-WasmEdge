// include/migrator/migrator.h
#pragma once

#include "ast/instruction.h"
#include "runtime/instance/module.h"
#include "runtime/instance/function.h"
#include "runtime/stackmgr.h"
#include "runtime/storemgr.h"
#include "executor/executor.h"

#include <map>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace WasmEdge {


namespace Runtime {
  class StackManager;
}
namespace Executor {

class Migrator {
public:
  struct CtrlInfo {
    uint32_t BeginAddrOfs;
    uint32_t TargetAddrOfs;
    uint32_t ElseAddrOfs;
    uint32_t SpOfs;
    uint32_t ResultCells;
  };

  struct IteratorKeys {
    std::vector<uintptr_t> AddrVec;
    std::map<uintptr_t, uint32_t> AddrToIdx;
  };

  // constructor
  Migrator() {};
  ~Migrator() {};

  // (Removed) checkpoint flag control functions were incorrectly declared as
  // member functions. They are provided as free functions instead; see below.

  void Prepare(const Runtime::Instance::ModuleInstance* ModInst, std::string dirname);
  std::pair<uint32_t, uint32_t> getInstrAddrExpr(AST::InstrView::iterator PC);

  void debugFrame(uint32_t FrameIdx, uint32_t EnterFuncIdx, uint32_t Locals, uint32_t Arity, uint32_t VPos);
  void dumpMemoryV1(const Runtime::Instance::ModuleInstance* ModInst);
  void dumpMemoryV2(const Runtime::Instance::ModuleInstance* ModInst);
  void dumpGlobal(const Runtime::Instance::ModuleInstance* ModInst);
  Expect<void> dumpProgramCounter(AST::InstrView::iterator Iter);
  void dumpStackV2(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC);

  void restoreMemoryV1(const Runtime::Instance::ModuleInstance* ModInst);
  void restoreMemoryV2(const Runtime::Instance::ModuleInstance* ModInst);
  void restoreGlobal(const Runtime::Instance::ModuleInstance* ModInst);
  Expect<AST::InstrView::iterator> restoreProgramCounter(const Runtime::Instance::ModuleInstance* ModInst);
  Expect<void> restoreStackV2(Runtime::StackManager& StackMgr);

private:
  const std::string NULL_MOD_NAME = "null";
  const std::string TYPE_TABLE = "type_table";
  const std::string TYPE_TABLEMAP_FUNC = "tablemap_func";
  const std::string TYPE_TABLEMAP_OFFSET = "tablemap_offset";

  IteratorKeys ik;
  std::string ImageDir;
  std::string BaseModName;
  
  Expect<AST::InstrView::iterator> _restoreIter(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset);
  Expect<AST::InstrView::iterator> _restorePC(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset);
};

// Global checkpoint flag helpers (free functions implemented in migrator_v2.cpp)
bool setCheckpointFlag(bool flag);
bool getCheckpointFlag();

} // namespace Executor
} // namespace WasmEdge