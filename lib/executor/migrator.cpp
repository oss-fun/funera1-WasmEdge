#include "executor/migrator.h"
#include <map>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <wasmig/migration.h>

namespace WasmEdge {
  
namespace Runtime {
  class StackManager;
}

namespace Executor {
    using M = Migrator;

    // void Prepare(const Runtime::Instance::ModuleInstance* ModInst) {
    void M::Prepare(const Runtime::Instance::ModuleInstance* ModInst, std::string dirname) {
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
  
  uint32_t M::getFuncIdx(const AST::InstrView::iterator PC) {
    if (PC == nullptr) return -1;
    
    auto Ret = std::upper_bound(ik.AddrVec.begin(), ik.AddrVec.end(), uintptr_t(PC));
    return ik.AddrToIdx[*(Ret-1)];
  }

  std::pair<uint32_t, uint32_t> M::getInstrAddrExpr(const Runtime::Instance::ModuleInstance *ModInst, AST::InstrView::iterator PC) {
      uint32_t FuncIdx = getFuncIdx(PC);
      if (FuncIdx == uint32_t(-1)) return std::make_pair(-1, -1);
      Runtime::Instance::FunctionInstance* FuncInst = ModInst->getFunc(FuncIdx).value();
      AST::InstrView::iterator PCStart = FuncInst->getInstrs().begin();
      uint32_t Offset = PC->getOffset() - PCStart->getOffset();

      return std::make_pair(FuncIdx, Offset);
  }

  // TODO: リファクタしたほうが良さそう
  std::vector<uint8_t> M::getTypeStack(uint32_t FuncIdx, uint32_t Offset, bool IsRetAddr) {
    uint8_t Val;
    std::ifstream type_table(TYPE_TABLE, std::ios::binary);
    std::ifstream tablemap_func(TYPE_TABLEMAP_FUNC, std::ios::binary);
    std::ifstream tablemap_offset(TYPE_TABLEMAP_OFFSET, std::ios::binary);

    /// tablemap_func
    uint32_t _FuncIdx;
    uint64_t TablemapOffsetAddr;
    tablemap_func.seekg(3*sizeof(uint32_t)*FuncIdx, std::ios_base::beg);
    tablemap_func.read(reinterpret_cast<char *>(&_FuncIdx), sizeof(uint32_t));
    tablemap_func.read(reinterpret_cast<char *>(&TablemapOffsetAddr), sizeof(uint64_t));

    tablemap_func.close();

    /// tablemap_offset
    std::vector<uint8_t> TypeStack(0);
    uint32_t LocalsSize;
    tablemap_offset.seekg(TablemapOffsetAddr, std::ios_base::beg);
    // 関数FuncIdxのローカルを取得
    tablemap_offset.read(reinterpret_cast<char *>(&LocalsSize), sizeof(uint32_t));
    for (uint32_t I = 0; I < LocalsSize; ++I) {
      tablemap_offset.read(reinterpret_cast<char *>(&Val), sizeof(uint8_t));
      TypeStack.push_back(Val);
    }
    // Offsetの位置まで移動
    uint32_t _Offset;
    uint64_t TypeTableAddr;
    // uint64_t PreTypeTableAddr;
    while(1) {
      tablemap_offset.read(reinterpret_cast<char *>(&_Offset), sizeof(uint32_t));
      // eofならbreak
      if (tablemap_offset.eof()) break;

      tablemap_offset.read(reinterpret_cast<char *>(&TypeTableAddr), sizeof(uint64_t));
      if (Offset == _Offset) break;
    }

    tablemap_offset.close();

    /// type_table
    type_table.seekg(TypeTableAddr, std::ios_base::beg);
    uint32_t StackSize;
    type_table.read(reinterpret_cast<char *>(&StackSize), sizeof(uint32_t));
    // リターンアドレスの場合、つまり関数呼び出し途中のときの場合、それ用のスタックを取得する
    if (IsRetAddr) {
      type_table.seekg(StackSize, std::ios_base::cur);
      type_table.read(reinterpret_cast<char *>(&StackSize), sizeof(uint32_t));
    }
    for (uint32_t I = 0; I < StackSize; ++I) {
      type_table.read(reinterpret_cast<char *>(&Val), sizeof(uint8_t));
      TypeStack.push_back(Val);
    }

    type_table.close();

    // debug
    // std::cerr << "[DEBUG]LocalsSize: " << LocalsSize << std::endl;
    // std::cerr << "[DEBUG]StackSize: " << StackSize << std::endl;

    return TypeStack;
  }

  bool M::isExistTypeStackTable() {
    namespace fs = std::filesystem;
    return fs::exists(TYPE_TABLE) &&
           fs::exists(TYPE_TABLEMAP_FUNC) &&
           fs::exists(TYPE_TABLEMAP_OFFSET);
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
  
//   std::vector<struct CtrlInfo> M::getCtrlStack(const AST::InstrView::iterator PCNow, Runtime::Instance::FunctionInstance *Func, const std::vector<uint32_t> &WamrCellSums) {
  std::vector<M::CtrlInfo> M::getCtrlStack(const AST::InstrView::iterator PCNow,
                                     Runtime::Instance::FunctionInstance *Func,
                                     const std::vector<uint32_t> &WamrCellSums) {
    std::vector<M::CtrlInfo> CtrlStack;

    AST::InstrView::iterator PCStart = Func->getInstrs().begin();
    AST::InstrView::iterator PCEnd = Func->getInstrs().end();
    AST::InstrView::iterator PC = PCStart;
    
    uint32_t BaseAddr = PCStart->getOffset();

    auto CtrlPush = [&](AST::InstrView::iterator Begin, AST::InstrView::iterator Target, uint32_t SpOfs) {
      uint32_t BeginOfs = Begin->getOffset() - BaseAddr;
      uint32_t TargetOfs = Target->getOffset() - BaseAddr;
      uint32_t ElseOfs = (Begin + Begin->getJumpElse())->getOffset() - BaseAddr;
      CtrlStack.push_back({BeginOfs, TargetOfs, ElseOfs, SpOfs, 0});
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
    uint32_t SpOfs;
    CtrlPush(PCStart, PCEnd-1, 0);

    // 命令をなめる
    while (PC < PCNow) {
      switch (PC->getOpCode()) {
        // push
        case OpCode::Block:
        case OpCode::If:
          SpOfs = WamrCellSums[PC->getJump().StackEraseBegin];
          CtrlPush(PC+1, PC+PC->getJumpEnd(), SpOfs);
          break;
        case OpCode::Loop:
          SpOfs = WamrCellSums[PC->getJump().StackEraseBegin];
          CtrlPush(PC+1, PC+1, SpOfs);
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
  void M::dumpMemory(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpMemInst(ImageDir);
  }

  void M::dumpGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->dumpGlobInst(ImageDir);
  }


  Expect<void> M::dumpProgramCounter(const Runtime::Instance::ModuleInstance* ModInst, AST::InstrView::iterator Iter) {
    auto [FuncIdx, Offset] = getInstrAddrExpr(ModInst, Iter);
    checkpoint_pc(FuncIdx, Offset);
    return {};
  }

  void M::dumpStack(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC) {
    std::vector<Runtime::StackManager::Frame> FrameStack = StackMgr.getFrameStack();
    std::vector<ValVariant> ValueStack = StackMgr.getValueStack();
    std::vector<std::vector<uint8_t>> TypeStacks(FrameStack.size());
    std::ofstream frame_fout(ImageDir + "frame.img", std::ios::trunc | std::ios::binary);

    // header file. frame stackのサイズを記録
    uint32_t LenFrame = FrameStack.size()-1;
    frame_fout.write(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    frame_fout.close();
    
    // 先にフレームごとの型スタックを取得し、TypeStacksにつめる
    AST::InstrView::iterator PCCopy = PC;
    uint32_t StackIdx = 1;
    for (size_t I = FrameStack.size()-1; I > 0; --I, ++StackIdx) {
      auto f = FrameStack[I];
      const Runtime::Instance::ModuleInstance* ModInst = f.Module;
      // NOTE: リターンアドレスは、実行しているアドレスの1つまえのアドレスを持っているので+1する
      if (I != FrameStack.size() - 1) PCCopy++;
      auto [FuncIdx, Offset] = getInstrAddrExpr(ModInst, PCCopy);
      TypeStacks[StackIdx] = getTypeStack(FuncIdx, Offset, I != FrameStack.size()-1);
      PCCopy = f.From;
    }

    // TypeStackからWAMRのセルの個数累積和みたいにする
    // 累積和 1-indexed
    uint32_t Cur = 0;
    std::vector<uint32_t> WamrCellSums(StackMgr.size()+1, 0);
    for (uint32_t StackIdx = TypeStacks.size()-1; StackIdx > 0; --StackIdx) {
      std::vector<uint8_t> TypeStack = TypeStacks[StackIdx];
      for (uint32_t I = 0; I < TypeStack.size(); I++) {
          WamrCellSums[Cur+1] = WamrCellSums[Cur] + TypeStack[I];
          Cur++;
      }
    }

    // フレームスタックを上から見ていく。上からstack1, stack2...とする
    StackIdx = 1;
    for (size_t I = FrameStack.size()-1; I > 0; --I, ++StackIdx) {
      std::ofstream ofs(ImageDir + "stack" + std::to_string(StackIdx) + ".img", std::ios::trunc | std::ios::binary);
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
      std::vector<uint8_t> TypeStack = TypeStacks[StackIdx];
      uint32_t TypeStackLen = TypeStack.size();
      ofs.write(reinterpret_cast<char *>(&TypeStackLen), sizeof(uint32_t));
      ofs.write(reinterpret_cast<char *>(TypeStack.data()), sizeof(uint8_t) * TypeStackLen);

      // 値スタック
      for (uint32_t I = 0; I < TypeStackLen; I++) {
        ofs.write(reinterpret_cast<char *>(&ValueStack[StackBottom+I].get<uint128_t>()), sizeof(uint32_t) * TypeStack[I]);
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
        ofs.write(reinterpret_cast<char *>(&ci.ResultCells), sizeof(uint32_t));
      }

      ofs.close();

      // 各値を更新
      PC = f.From;

      // debug
      // debugFrame(I, EnterFuncIdx, f.Locals, f.Arity, f.VPos);
    }
  }
  
  /// ================
  /// Restore functions
  /// ================
  void M::restoreMemory(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreMemInst(ImageDir);
  }

  void M::restoreGlobal(const Runtime::Instance::ModuleInstance* ModInst) {
    ModInst->restoreGlobInst(ImageDir);
  }

  Expect<AST::InstrView::iterator> M::_restoreIter(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {
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
  Expect<AST::InstrView::iterator> M::_restorePC(const Runtime::Instance::ModuleInstance* ModInst, uint32_t FuncIdx, uint32_t Offset) {
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

  Expect<AST::InstrView::iterator> M::restoreProgramCounter(const Runtime::Instance::ModuleInstance* ModInst) {
    CodePos pc = restore_pc();
    auto Res = _restorePC(ModInst, pc.fidx, pc.offset);
    return Res;
  }

  Expect<void> M::restoreStack(Runtime::StackManager& StackMgr) {
    const Runtime::Instance::ModuleInstance *Module = StackMgr.getModule();

    uint32_t LenFrame;
    std::ifstream ifs(ImageDir + "frame.img", std::ios::binary);
    ifs.read(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    ifs.close();

    // LenFrame-1から始まるのは、Stack{LenFrame}.imgがダミーフレームだから
    AST::InstrView::iterator PC = StackMgr.popFrame();
    for (size_t I = LenFrame; I > 0; --I) {
      ifs.open(ImageDir + "stack" + std::to_string(I) + ".img", std::ios::binary);

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
        StackMgr.push(Value);
      }

      ifs.close();

      // debug
      // debugFrame(I, EnterFuncIdx, Locals, RetsN, VPos);
    }
    return {};
  }

} // namespace Executor
} // namespace WasmEdge