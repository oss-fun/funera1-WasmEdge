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

namespace WasmEdge {
  
namespace Runtime {
  class StackManager;
}

namespace Executor {
struct SourceLoc {
  bool operator==(const SourceLoc& rhs) const {
    return (FuncIdx == rhs.FuncIdx && Offset == rhs.Offset);
  }

  uint32_t FuncIdx;
  uint32_t Offset;
};

class Migrator {
public:
  /// TODO: ModuleInstanceがnullだったときの名前。重複しないようにする
  const std::string NULL_MOD_NAME = "null";
  using IterMigratorType = std::map<AST::InstrView::iterator, SourceLoc>;

  struct CtrlInfo {
    // AST::InstrView::iterator Iter;
    uint32_t BeginAddrOfs;
    uint32_t TargetAddrOfs;
    uint32_t ElseAddrOfs;
    uint32_t TspOfs;
    uint32_t SpOfs;
    uint32_t ResultCells;
    uint32_t ResultCount;
  };

  struct IteratorKeys {
    // すべての関数の先頭アドレスを照準に並べたリスト
    std::vector<uintptr_t> AddrVec;
    // 関数の先頭アドレス -> 関数インデックス
    std::map<uintptr_t, uint32_t> AddrToIdx;
  };

  /// ================
  /// Tools
  /// ================

  /// Find module by name.
  const Runtime::Instance::ModuleInstance *findModule(std::string_view Name) const {
    auto Iter = NamedMod.find(Name);
    if (likely(Iter != NamedMod.cend())) {
      return Iter->second;
    }
    return nullptr;
  }

  // void setIterMigrator(const Runtime::Instance::ModuleInstance* ModInst) {
  //   if (IterMigrators.count((std::string)ModInst->getModuleName())) {
  //     return;
  //   }

  //   IterMigratorType IterMigrator;

  //   uint64_t addr_cnt = 0;
  //   for (uint32_t I = 0; I < ModInst->getFuncNum(); ++I) {
  //     Runtime::Instance::FunctionInstance* FuncInst = ModInst->getFunc(I).value();
  //     AST::InstrView Instr = FuncInst->getInstrs();
  //     AST::InstrView::iterator PC = Instr.begin();
  //     AST::InstrView::iterator PCEnd = Instr.end();

  //     std::cerr << "Func[" << I << "]: [" << PC << ", " << PCEnd << "]" << std::endl; 
  //     addr_cnt += PCEnd - PC;
      
  //     uint32_t Offset = 0;
  //     while (PC != PCEnd) {
  //       IterMigrator[PC] = SourceLoc{I, Offset};
  //       Offset++;
  //       PC++;
  //     }
  //   }
  //   std::cerr << "Address Count: " << addr_cnt << std::endl; 

  //   IterMigrators[(std::string)ModInst->getModuleName()] = IterMigrator;
  // }

  // void Prepare(const Runtime::Instance::ModuleInstance* ModInst) {
  void setIterMigrator(const Runtime::Instance::ModuleInstance* ModInst) {
    for (uint32_t I = 0; I < ModInst->getFuncNum(); ++I) {
      Runtime::Instance::FunctionInstance* FuncInst = ModInst->getFunc(I).value();
      AST::InstrView Instr = FuncInst->getInstrs();
      AST::InstrView::iterator PC = Instr.begin();
      ik.AddrVec.push_back(uintptr_t(PC));
      ik.AddrToIdx[uintptr_t(PC)] = I;
    }
    // 門番
    ik.AddrVec.push_back(UINT64_MAX);

    // 昇順ソート
    std::sort(ik.AddrVec.begin(), ik.AddrVec.end());
  }
  
  // IterMigratorType getIterMigrator(const Runtime::Instance::ModuleInstance* ModInst) {
  //   if (IterMigrators.count((std::string)ModInst->getModuleName())) {
  //     return IterMigrators[(std::string)ModInst->getModuleName()];
  //   }

  //   std::string_view ModName = ModInst->getModuleName();
  //   setIterMigrator(ModInst);
  //   return IterMigrators[(std::string)ModName];
  // }

  IterMigratorType getIterMigratorByName(std::string ModName) {
    if (IterMigrators.count(ModName)) {
      return IterMigrators[ModName];
    } else {
      IterMigratorType I;
      return I;
    }
  }

  // SourceLoc getSourceLoc(AST::InstrView::iterator Iter) {
  //   IterMigratorType IterMigrator = getIterMigratorByName(BaseModName);
  //   // assert(IterMigrator);

  //   struct SourceLoc Data = IterMigrator[Iter];
  //   return Data;
  // }
  
  // 命令アドレスの状態表現 (fidx, offset)
  // std::pair<uint32_t, uint32_t> getInstrAddrExpr(const Runtime::Instance::ModuleInstance *ModInst, AST::InstrView::iterator it) {
  //     IterMigratorType iterMigr = getIterMigratorByName(BaseModName);
  //     struct SourceLoc Data = iterMigr[it];
  //     auto Res = ModInst->getFunc(Data.FuncIdx);
  //     Runtime::Instance::FunctionInstance* FuncInst = Res.value();
  //     AST::InstrView::iterator PCStart = FuncInst->getInstrs().begin();
  //     uint32_t Offset = it->getOffset() - PCStart->getOffset();

  //     return std::make_pair(Data.FuncIdx, Offset);
  // }
  uint32_t getFuncIdx(const AST::InstrView::iterator PC) {
    if (PC == nullptr) return -1;
    
    auto Ret = std::upper_bound(ik.AddrVec.begin(), ik.AddrVec.end(), uintptr_t(PC));
    uintptr_t PCStart = *(Ret-1);
    uint32_t FuncIdx = ik.AddrToIdx[PCStart];

    return FuncIdx;
  }

  std::pair<uint32_t, uint32_t> getInstrAddrExpr(const Runtime::Instance::ModuleInstance *ModInst, AST::InstrView::iterator PC) {
      uint32_t FuncIdx = getFuncIdx(PC);
      if (FuncIdx == uint32_t(-1)) return std::make_pair(-1, -1);
      Runtime::Instance::FunctionInstance* FuncInst = ModInst->getFunc(FuncIdx).value();
      AST::InstrView::iterator PCStart = FuncInst->getInstrs().begin();
      uint32_t Offset = PC->getOffset() - PCStart->getOffset();

      return std::make_pair(FuncIdx, Offset);
  }

  // FunctioninstanceからFuncIdxを取得する
  // uint32_t getFuncIdx(const Runtime::Instance::FunctionInstance* Func) {
  //   if (Func == nullptr) return -1;
  //   AST::InstrView::iterator PC = Func->getInstrs().begin();
  //   return getFuncIdx(PC);
  // }


  /// ================
  /// Debug
  /// ================
  void debugFrame(uint32_t FrameIdx, uint32_t EnterFuncIdx, uint32_t Locals, uint32_t Arity, uint32_t VPos) {
      std::string DebugPrefix = "[DEBUG]";
      std::cerr << DebugPrefix << "Frame Idx: " << FrameIdx << std::endl;
      std::cerr << DebugPrefix << "EnterFuncIdx: " << EnterFuncIdx << std::endl;
      std::cerr << DebugPrefix << "Locals: " << Locals << std::endl;
      std::cerr << DebugPrefix << "Arity: "  << Arity << std::endl;
      std::cerr << DebugPrefix << "VPos: "   << VPos << std::endl;
      std::cerr << std::endl;
  }
  
  // void debugFuncOpcode(uint32_t FuncIdx, uint32_t Offset, AST::InstrView::iterator it) {
  //   std::ofstream opcodeLog;
  //   opcodeLog.open("wasmedge_opcode.log", std::ios::app);
  //   opcodeLog << "fidx: " << FuncIdx << "\n";
  //   for (uint32_t i = 0; i < Offset; i++) it--;
  //   for (uint32_t i = 0; i < Offset; i++) {
  //     opcodeLog << i+1 << ") opcode: 0x" << std::hex << static_cast<uint16_t>(it->getOpCode()) << "\n";
  //     opcodeLog << std::dec;
  //     it++;
  //   }
  // }


  std::vector<struct CtrlInfo> getCtrlStack(const AST::InstrView::iterator PCNow, Runtime::Instance::FunctionInstance *Func, const std::vector<uint32_t> &WamrCellSums) {
    std::vector<struct CtrlInfo> CtrlStack;
    
    IterMigratorType IterMigrator = getIterMigratorByName(BaseModName);
    // assert(IterMigrator);

    AST::InstrView::iterator PCStart = Func->getInstrs().begin();
    AST::InstrView::iterator PCEnd = Func->getInstrs().end();
    AST::InstrView::iterator PC = PCStart;
    
    uint32_t BaseAddr = PCStart->getOffset();

    auto CtrlPush = [&](AST::InstrView::iterator Begin, AST::InstrView::iterator Target, uint32_t SpOfs, uint32_t TspOfs) {
      uint32_t BeginOfs = Begin->getOffset() - BaseAddr;
      uint32_t TargetOfs = Target->getOffset() - BaseAddr;
      uint32_t ElseOfs = (Begin + Begin->getJumpElse())->getOffset() - BaseAddr;
      CtrlStack.push_back({BeginOfs, TargetOfs, ElseOfs, SpOfs, TspOfs, 0, 0});
    };
    
    auto CtrlPop = [&]() {
      if (CtrlStack.size() == 0) {
        std::cerr << "CtrlStack is empty" << std::endl;
        return;
      }
      CtrlStack.pop_back();
    };
    
    // 関数ブロックを一番最初にpushする
    // ダミーブロックぽさがすこしあるので、適当に入れる（ちゃんとやると、target_addrに関数の一番最後のアドレスを入れる必要があり、無駄が増えるため）
    uint32_t SpOfs, TspOfs;
    CtrlPush(PCStart, PCEnd-1, 0, 0);

    // 命令をなめる
    while (PC < PCNow) {
      switch (PC->getOpCode()) {
        // push
        case OpCode::Block:
        case OpCode::If:
          SpOfs = WamrCellSums[PC->getJump().StackEraseBegin];
          TspOfs = PC->getJump().StackEraseBegin;
          CtrlPush(PC+1, PC+PC->getJumpEnd(), SpOfs, TspOfs);
          break;
        case OpCode::Loop:
          SpOfs = WamrCellSums[PC->getJump().StackEraseBegin];
          TspOfs = PC->getJump().StackEraseBegin;
          CtrlPush(PC+1, PC+1, SpOfs, TspOfs);
          break;

        // pop
        case OpCode::End:
          CtrlPop();
          break;
        default:
          break;
      }

      PC++;
    }
    return CtrlStack;
  }
  
  /// ================
  /// Dump functions
  /// ================
  void dumpMemory(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpMemInst();
  }

  void dumpGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpGlobInst();
  }

  /// TODO: 関数名を中身にあったものにrenameする
  void preDumpIter(const Runtime::Instance::ModuleInstance* ModInst) {
    setIterMigrator(ModInst);
    BaseModName = ModInst->getModuleName();
  }

  Expect<void> dumpProgramCounter(const Runtime::Instance::ModuleInstance* ModInst, AST::InstrView::iterator Iter) {
    // IterMigratorType IterMigrator = getIterMigratorByName(BaseModName);
    // assert(IterMigrator);

    // struct SourceLoc Data = IterMigrator[Iter];
    std::ofstream ofs("program_counter.img", std::ios::trunc | std::ios::binary);
    if (!ofs) {
      return Unexpect(ErrCode::Value::IllegalPath);
    }

    // auto Res = ModInst->getFunc(Data.FuncIdx);
    // if (unlikely(!Res)) {
    //   return Unexpect(Res);
    // }
    // Runtime::Instance::FunctionInstance* FuncInst = Res.value();
    // AST::InstrView::iterator PCStart = FuncInst->getInstrs().begin();


    auto [FuncIdx, Offset] = getInstrAddrExpr(ModInst, Iter);
    // uint32_t Offset = Iter->getOffset() - PCStart->getOffset();
    ofs.write(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
    ofs.write(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));

    ofs.close();
    return {};
  }

  void dumpStack(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC) {
    std::vector<Runtime::StackManager::Frame> FrameStack = StackMgr.getFrameStack();
    std::vector<uint8_t> TypeStack = StackMgr.getTypeStack();
    std::ofstream frame_fout("frame.img", std::ios::trunc | std::ios::binary);

    // header file. frame stackのサイズを記録
    uint32_t LenFrame = FrameStack.size()-1;
    frame_fout.write(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    frame_fout.close();
    
    // TypeStackからWAMRのセルの個数累積和みたいにする
    // 累積和 1-indexed
    std::vector<uint32_t> WamrCellSums(TypeStack.size()+1, 0);
    for (uint32_t I = 0; I < TypeStack.size(); I++) {
        WamrCellSums[I+1] = WamrCellSums[I] + (TypeStack[I] == 0 ? 1 : 2);
    }

    // uint32_t PreStackTop = FrameStack[0].VPos - FrameStack[0].Locals;
    // フレームスタックを上から見ていく。上からstack1, stack2...とする
    uint32_t StackIdx = 1;
    uint32_t StackTop = StackMgr.size();
    for (size_t I = FrameStack.size()-1; I > 0; --I, ++StackIdx) {
      std::ofstream ofs("stack" + std::to_string(StackIdx) + ".img", std::ios::trunc | std::ios::binary);
      Runtime::StackManager::Frame f = FrameStack[I];
 
      // ModuleInstance 
      const Runtime::Instance::ModuleInstance* ModInst = f.Module;

      // ModInstがnullの場合、ModNameだけ出力してcontinue
      if (ModInst == nullptr) {
        std::cerr << "ModInst is nullptr" << std::endl;
        exit(1);
      }

      // 関数インデックス
      // uint32_t EnterFuncIdx = getFuncIdx(f.EnterFunc);
      uint32_t EnterFuncIdx = getFuncIdx(PC);
      ofs.write(reinterpret_cast<char *>(&EnterFuncIdx), sizeof(uint32_t));

      // リターンアドレス(uint32 fidx, uint32 offset)
      // NOTE: 共通仕様ではリターンアドレスは次に実行するアドレスなのでf.From+1
      // I==1のときだけ, f.Fromにend()が入ってるので場合分け
      AST::InstrView::iterator From = (I == 1 ? f.From : f.From+1);
      auto [FuncIdx, Offset] = getInstrAddrExpr(ModInst, From);
      ofs.write(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
      ofs.write(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));

      // 型スタック
      uint32_t StackBottom = f.VPos - f.Locals;
      uint32_t TspOfs = StackTop - StackBottom;
      ofs.write(reinterpret_cast<char *>(&TspOfs), sizeof(uint32_t));
      for (uint32_t I = StackBottom; I < StackTop; I++) {
          ofs.write(reinterpret_cast<char *>(&TypeStack[I]), sizeof(uint8_t));
      }

      // 値スタック
      std::vector<ValVariant> ValueStack = StackMgr.getValueStack();
      for (uint32_t I = StackBottom; I < StackTop; I++) {
        ofs.write(reinterpret_cast<char *>(&ValueStack[I].get<uint128_t>()), sizeof(uint32_t) * TypeStack[I]);
      }

      // ラベルスタック
      auto Res = ModInst->getFunc(EnterFuncIdx);
      if (!Res) {
        std::cerr << "FuncIdx isn't correct" << std::endl; 
        exit(1);
      }
      Runtime::Instance::FunctionInstance* FuncInst = Res.value();
      std::vector<struct CtrlInfo> CtrlStack = getCtrlStack(PC, FuncInst, WamrCellSums);
      uint32_t LenCs = CtrlStack.size();
      ofs.write(reinterpret_cast<char *>(&LenCs), sizeof(uint32_t));
      for (uint32_t I = 0; I < LenCs; I++) {
        struct CtrlInfo ci = CtrlStack[I];
        ofs.write(reinterpret_cast<char *>(&ci.BeginAddrOfs), sizeof(uint32_t));
        ofs.write(reinterpret_cast<char *>(&ci.TargetAddrOfs), sizeof(uint32_t));
        ofs.write(reinterpret_cast<char *>(&ci.SpOfs), sizeof(uint32_t));
        ofs.write(reinterpret_cast<char *>(&ci.TspOfs), sizeof(uint32_t));
        ofs.write(reinterpret_cast<char *>(&ci.ResultCells), sizeof(uint32_t));
        ofs.write(reinterpret_cast<char *>(&ci.ResultCount), sizeof(uint32_t));
      }

      ofs.close();

      // 各値を更新
      PC = f.From;
      StackTop = StackBottom;
      // std::cerr << "[DEBUG]StackTop is " << StackTop << std::endl;

      // debug
      // debugFrame(I, EnterFuncIdx, f.Locals, f.Arity, f.VPos);
    }
  }
  
  /// ================
  /// Restore functions
  /// ================
  void restoreMemory(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreMemInst();
  }

  void restoreGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreGlobInst();
  }

  Expect<AST::InstrView::iterator> _restoreIter(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {
    assert(ModInst != nullptr);
    
    auto Res = ModInst->getFunc(FuncIdx);
    if (unlikely(!Res)) {
      // spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
      std::cout << "\x1b[31m";
      std::cout << "ERROR: _restoreIter" << std::endl;
      std::cout << "\x1b[1m";
      return Unexpect(Res);
    }
    Runtime::Instance::FunctionInstance* FuncInst = Res.value();
    assert(FuncInst != nullptr);

    AST::InstrView::iterator Iter = FuncInst->getInstrs().begin();
    assert(Iter != nullptr);

    Iter += Offset;

    return Iter;
  }

  // 命令と引数が混在したOffsetの復元
  Expect<AST::InstrView::iterator> _restorePC(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {
    assert(ModInst != nullptr);
    
    auto Res = ModInst->getFunc(FuncIdx);
    if (unlikely(!Res)) {
      // spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
      return Unexpect(Res);
    }
    Runtime::Instance::FunctionInstance* FuncInst = Res.value();
    assert(FuncInst != nullptr);

    AST::InstrView::iterator PC = FuncInst->getInstrs().begin();
    assert(PC != nullptr);

    // 与えられたOffsetは関数の先頭からの相対オフセットなので、先頭アドレス分を足す
    Offset += PC->getOffset();

    uint32_t PCOfs = 0;
    while (PCOfs < Offset) {
      PCOfs = PC->getOffset();
      if (PCOfs == Offset) {
        return PC;
      }
      PC++;
    }

    // ここまで来たらエラー
    std::cerr << "[WARN] The offset of program_counter.img is incorrect" << std::endl;
    return Unexpect(ErrCode::Value::Terminated);
  }

  Expect<AST::InstrView::iterator> restoreProgramCounter(const Runtime::Instance::ModuleInstance* ModInst) {
    std::ifstream ifs("program_counter.img", std::ios::binary);

    uint32_t FuncIdx, Offset;
    ifs.read(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
    ifs.read(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));

    ifs.close();

    auto Res = _restorePC(ModInst, FuncIdx, Offset);
    return Res;
  }

  Expect<void> restoreStack(Runtime::StackManager& StackMgr) {
    const Runtime::Instance::ModuleInstance *Module = StackMgr.getModule();

    uint32_t LenFrame;
    std::ifstream ifs("frame.img", std::ios::binary);
    ifs.read(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    ifs.close();

    // LenFrame-1から始まるのは、Stack{LenFrame}.imgがダミーフレームだから
    AST::InstrView::iterator PC = StackMgr.popFrame();
    for (size_t I = LenFrame; I > 0; --I) {
      ifs.open("stack" + std::to_string(I) + ".img", std::ios::binary);

      // 関数インデックスのロード
      uint32_t EnterFuncIdx;
      ifs.read(reinterpret_cast<char *>(&EnterFuncIdx), sizeof(uint32_t));
      
      // リターンアドレスのロード
      uint32_t FuncIdx, Offset;
      ifs.read(reinterpret_cast<char *>(&FuncIdx), sizeof(uint32_t));
      ifs.read(reinterpret_cast<char *>(&Offset), sizeof(uint32_t));
      // リターンアドレスの復元
      auto ResFrom = _restorePC(Module, FuncIdx, Offset);
      if (!ResFrom) {
        return Unexpect(ResFrom);
      }
      AST::InstrView::iterator From = ResFrom.value()-1;
      if (I == LenFrame) From = PC; // 一番bottomのフレームのリターンアドレスはWasmEdge特有なので、それを使う

      // ローカルと返り値の数
      auto ResFunc = Module->getFunc(EnterFuncIdx);
      if (!ResFunc) {
        return Unexpect(ResFunc);
      }
      const Runtime::Instance::FunctionInstance* Func = ResFunc.value();
      const auto &FuncType = Func->getFuncType();
      const uint32_t ArgsN = static_cast<uint32_t>(FuncType.getParamTypes().size());
      const uint32_t RetsN =
          static_cast<uint32_t>(FuncType.getReturnTypes().size());

      // TODO: Localsに対応する値をenterFunctionと対応してるか確認する
      uint32_t Locals = ArgsN + Func->getLocalNum();
      uint32_t VPos = StackMgr.size() + Locals;

      StackMgr._pushFrame(Module, From, Locals, RetsN, VPos, false);

      // 型スタック
      uint32_t TspOfs;
      std::vector<uint8_t> TypeStack;
      ifs.read(reinterpret_cast<char *>(&TspOfs), sizeof(uint32_t));
      for (uint32_t I = 0; I < TspOfs; I++) {
        uint8_t type;
        ifs.read(reinterpret_cast<char *>(&type), sizeof(uint8_t));
        TypeStack.push_back(type);
      }

      // 値スタック
      for (uint32_t I = 0; I < TspOfs; I++) {
        ValVariant Value;
        ifs.read(reinterpret_cast<char *>(&Value), sizeof(uint32_t) * TypeStack[I]);
        StackMgr.push(Value, TypeStack[I]);
      }

      ifs.close();

      // debug
      // debugFrame(I, EnterFuncIdx, Locals, RetsN, VPos);
    }
    return {};
  }

  bool DumpFlag; 
  bool RestoreFlag;
private:
  friend class Executor;

  std::map<std::string, IterMigratorType> IterMigrators;
  std::string BaseModName;
  /// \name Module name mapping.
  std::map<std::string, const Runtime::Instance::ModuleInstance *, std::less<>> NamedMod;
  IteratorKeys ik;
};

} // namespace Runtime
} // namespace WasmEdge
