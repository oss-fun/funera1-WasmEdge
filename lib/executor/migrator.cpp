#include "executor/migrator.h"

#include <iostream>
#include <fstream>
#include <cassert>
#include <filesystem>
#include <algorithm>
#include <map>

#include <wasmig/migration.h>
#include <wasmig/stack_tables.h>
#include <wasmig/state.h>
#include <wasmig/table_v3.h>
#include <wasmig/registry.h>
#include <wasmig/log.h>

namespace fs = std::filesystem;

namespace WasmEdge {
namespace Executor {
    using M = Migrator;

  /// ================
  /// Tools
  /// ================

  // void Prepare(const Runtime::Instance::ModuleInstance* ModInst) {
  void M::Prepare(const Runtime::Instance::ModuleInstance* ModInst, std::string dirname) {
    if (dirname.size() > 0) {
      // dirnameの終端文字は/
      if (dirname.back() != '/') {
        dirname.push_back('/');
      }

      // ディレクトリがなければ作成する
      if (!std::filesystem::is_directory(dirname)) {
        std::filesystem::create_directory(dirname);
      }
    }
    ImageDir = dirname;

    BaseModName = ModInst->getModuleName();
  }
  
  std::pair<uint32_t, uint32_t> M::getInstrAddrExpr(AST::InstrView::iterator PC) {
      uint64_t instrPtr = PC->getOffset(); // PC自体がポインタ
      uint32_t FuncIdx, Offset;
      AddressMap address_map = wasmig_address_map_load();
      if (!wasmig_address_map_get_key(address_map, instrPtr, &FuncIdx, &Offset)) {
          wasmig_error("address %p not found\n", instrPtr);
          return std::make_pair(-1, -1);
      }

      return std::make_pair(FuncIdx, Offset);
  }

  /// ================
  /// Debug
  /// ================
  void M::debugFrame(uint32_t FrameIdx, uint32_t EnterFuncIdx, uint32_t Locals, uint32_t Arity, uint32_t VPos) {
      std::string DebugPrefix = "[DEBUG]";
      std::cerr << DebugPrefix << "Frame Idx    : " << FrameIdx << std::endl;
      std::cerr << DebugPrefix << "EnterFuncIdx : " << EnterFuncIdx << std::endl;
      std::cerr << DebugPrefix << "Locals       : " << Locals << std::endl;
      std::cerr << DebugPrefix << "Arity        : "  << Arity << std::endl;
      std::cerr << DebugPrefix << "VPos         : "   << VPos << std::endl;
      std::cerr << std::endl;
  }
  
  /// ================
  /// Dump functions
  /// ================
  void M::dumpMemoryV1(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpMemInst(ImageDir);
  }

  void M::dumpGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpGlobInst(ImageDir);
  }

  Expect<void> M::dumpProgramCounter(AST::InstrView::iterator Iter) {
    std::ofstream ofs(ImageDir + "program_counter.img", std::ios::trunc | std::ios::binary);
    if (!ofs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }

    auto [FuncIdx, Offset] = getInstrAddrExpr(Iter);
    ofs.write(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
    ofs.write(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));

    ofs.close();
    return {};
  }

  /// ================
  /// Restore functions
  /// ================
  void M::restoreMemoryV1(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreMemInst(ImageDir);
  }

  void M::restoreGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreGlobInst(ImageDir);
  }

  Expect<AST::InstrView::iterator> M::_restoreIter(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {
    assert(ModInst != nullptr);
    
    auto Res = ModInst->getFunc(FuncIdx);
    if (unlikely(!Res)) {
      return Unexpect(Res);
    }
    Runtime::Instance::FunctionInstance* FuncInst = Res.value();
    assert(FuncInst != nullptr);

    AST::InstrView::iterator Iter = FuncInst->getInstrs().begin();
    assert(Iter != nullptr);

    Iter += Offset;

    return Iter;
  }

  uint64_t get_call_address(uint32_t fidx, uint32_t offset)
  {
      AddressMap address_map = wasmig_address_map_load();

      uint64_t pc_value = 0;
      if (!wasmig_address_map_get_value(address_map, fidx, offset, &pc_value)) {
          wasmig_error("Failed to get key from address map");
          return -1;
      }
      return pc_value;
  }

  // 命令と引数が混在したOffsetの復元
  Expect<AST::InstrView::iterator> M::_restorePC(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {

    uint64_t pc_value = get_call_address(FuncIdx, Offset);
    // wasmig_info("Restoring PC: (FuncIdx, Offset) = (%d, %d) -> Address = %lu", FuncIdx, Offset, pc_value);

    auto Res = ModInst->getFunc(FuncIdx);
    if (unlikely(!Res)) {
      // spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
      return Unexpect(Res);
    }
    Runtime::Instance::FunctionInstance* FuncInst = Res.value();

    AST::InstrView::iterator PCStart = FuncInst->getInstrs().begin();
    AST::InstrView::iterator targetPC = PCStart + pc_value;
    
    assert(targetPC != nullptr);
    assert(targetPC < FuncInst->getInstrs().end());
    
    return targetPC;
  }

  Expect<AST::InstrView::iterator> M::restoreProgramCounter(const Runtime::Instance::ModuleInstance* ModInst) {
    std::ifstream ifs(ImageDir + "program_counter.img", std::ios::binary);

    uint32_t FuncIdx, Offset;
    ifs.read(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
    ifs.read(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));

    ifs.close();

    auto Res = _restorePC(ModInst, FuncIdx, Offset);
    return Res;
  }
} // namespace Runtime
} // namespace WasmEdge