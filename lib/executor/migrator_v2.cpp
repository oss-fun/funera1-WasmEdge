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

namespace fs = std::filesystem;

namespace WasmEdge {
namespace Executor {
    using M = Migrator;


  std::vector<uint8_t> M::getTypeStack_v2(uint32_t FuncIdx, uint32_t Offset) {
    StackTable table = get_stack_table(FuncIdx, Offset);
    std::vector<uint8_t> TypeStack(table.size);
    for (size_t i = 0; i < table.size; i++) {
      StackTableEntry entry = table.data[i];
      TypeStack[i] = entry.ty;
    }
    return TypeStack;
  }

  /// ================
  /// Dump functions
  /// ================
  void M::dumpMemoryV2(const Runtime::Instance::ModuleInstance* ModInst) {
    // Get memory instance
    auto MemInstRes = ModInst->getMemory(0);
    if (unlikely(!MemInstRes)) {
      std::cerr << "Failed to get memory instance: " << MemInstRes.error() << std::endl;
      return;
    }
    Runtime::Instance::MemoryInstance *MemInst = MemInstRes.value();

    // Get page size and memory data
    uint32_t page_size = MemInst->getPageSize();
    auto DataRes = MemInst->getBytes(0, page_size * MemInst->kPageSize);
    if (unlikely(!DataRes)) {
      std::cerr << "Failed to get memory data: " << DataRes.error() << std::endl;
      return;
    }
    Span<Byte> data = DataRes.value();
    
    // Checkpoint memory
    checkpoint_memory(data.data(), page_size);
  }

  void _appendConverted(std::vector<uint32_t>& array, uint8_t type, const ValVariant Val) {
      switch (type) {
        case 1: // S32
          {
            array.push_back(Val.get<uint32_t>());
            break;
          }
        case 2: // S64
          {
            int64_t val64 = Val.get<int64_t>();
            int32_t high = val64 & 0xFFFFFFFF;
            int32_t low = (val64 >> 32) & 0xFFFFFFFF;
            array.push_back(high);
            array.push_back(low);
            break;
          }
        case 4: // S128
          {
            std::cerr << "V128 is not supported" << std::endl;
            exit(1);
          }
        default:
          {
            std::cerr << "Unknown type: " << +type << std::endl;
            exit(1);
          }
      }
  }

  void appendConverted(Runtime::StackManager& StackMgr, TypedArray array) {
    size_t iter = 0;
    for (size_t I = 0; I < array.types.size; I++) {
      uint8_t type = array.types.contents[I];
      switch (type) {
        case 1: // S32
          {
            StackMgr.push(array.values.contents[iter++]);
            break;
          }
        case 2: // S64
          {
            int32_t high = array.values.contents[iter++];
            int32_t low = array.values.contents[iter++];
            int64_t val64 = ((int64_t)high << 32) | low;
            StackMgr.push(val64);
            break;
          }
        case 4: // S128
          {
            std::cerr << "V128 is not supported" << std::endl;
            exit(1);
          }
        default:
          {
            std::cerr << "Unknown type: " << +type << std::endl;
            exit(1);
          }
      }
    }
    return;
  }

  void _dumpStack(
    Migrator &M,
    Runtime::StackManager::Frame f,
    AST::InstrView::iterator PC, 
    ValVariant* LocalsPtr,
    ValVariant* ValueStackPtr,
    std::vector<struct Migrator::CtrlInfo> &LabelStack,
    BaseCallStackEntry& entry
  ) {
    const Runtime::Instance::ModuleInstance* ModInst = f.Module;
    if (LocalsPtr == nullptr || ValueStackPtr == nullptr) {
      std::cerr << "LocalsPtr or ValueStackPtr is nullptr" << std::endl;
      exit(1);
    }
    
    // pc
    auto [FuncIdx, Offset] = M.getInstrAddrExpr(ModInst, PC);
    entry.pc = CodePos{
      .fidx = FuncIdx,
      .offset = Offset,
    };
    spdlog::info("_dumpStack: Set pc to ({}, {})", FuncIdx, Offset);
    
    // locals
    // TODO: slot-sizeが128bit単位のstackから32bit単位のstackに変換する
    ValVariant* lp = LocalsPtr;
    Array8 local_types = get_local_types(FuncIdx);
    std::vector<uint32_t> locals_vec;
    for (size_t i = 0; i < local_types.size; i++) {
      uint8_t type = local_types.contents[i];
      // print what calls _appendConverted
      std::cerr << "At _dumpStack, process that convert locals" << std::endl;
      _appendConverted(locals_vec, type, lp[i]);
    }
    // mallocしないとエラーでる.
    // TODO: memcpyを回避する. 現在二重で値のコピーが発生していて無駄
    uint32_t* locals_buf = (uint32_t *)malloc(locals_vec.size() * sizeof(uint32_t));
    memcpy(locals_buf, locals_vec.data(), locals_vec.size() * sizeof(uint32_t));
    entry.locals = {
      .size = (uint32_t)locals_vec.size(),
      .contents = locals_buf,
    };
    spdlog::info("_dumpStack: Set locals");

    // stack
    ValVariant* sp = ValueStackPtr;
    StackTable stack_table = get_stack_table(FuncIdx, Offset);
    std::vector<uint32_t> stack_vec;
    for (size_t i = 0; i < stack_table.size; i++) {
      StackTableEntry entry = stack_table.data[i];
      // print what calls _appendConverted
      std::cerr << "At _dumpStack, process that convert stack" << std::endl;
      _appendConverted(stack_vec, entry.ty, sp[i]);
    }
    uint32_t* stack_buf = (uint32_t *)malloc(stack_vec.size() * sizeof(uint32_t));
    memcpy(stack_buf, stack_vec.data(), stack_vec.size() * sizeof(uint32_t));
    entry.value_stack = {
      .size = (uint32_t)stack_vec.size(),
      .contents = stack_buf,
    };
    spdlog::info("_dumpStack: Set stack");

    /// label stack
    uint32_t label_stack_size = LabelStack.size();
    uint32_t* begins = (uint32_t *)malloc(label_stack_size * sizeof(uint32_t));
    uint32_t* targets = (uint32_t *)malloc(label_stack_size* sizeof(uint32_t));
    uint32_t* stack_pointers = (uint32_t *)malloc(label_stack_size * sizeof(uint32_t));
    uint32_t* cell_nums = (uint32_t *)malloc(label_stack_size * sizeof(uint32_t));
    for (size_t i = 0; i < LabelStack.size(); i++) {
        Migrator::CtrlInfo ci = LabelStack[i];
        begins[i] = ci.BeginAddrOfs;
        targets[i] = ci.TargetAddrOfs;
        stack_pointers[i] = ci.SpOfs;
        cell_nums[i] = ci.ResultCells;
    }
    entry.label_stack = {
      .size = label_stack_size,
      .begins = begins,
      .targets = targets,
      .stack_pointers = stack_pointers,
      .cell_nums = cell_nums,
    };
    spdlog::info("_dumpStack: Set label stack");
    
    return;
  }

  void M::dumpStackV2(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC) {
    std::vector<Runtime::StackManager::Frame> FrameStack = StackMgr.getFrameStack();
    std::vector<ValVariant> ValueStack = StackMgr.getValueStack();
    std::vector<std::vector<uint8_t>> TypeStacks(FrameStack.size());
    size_t LenFrame = FrameStack.size()-1;
    
    // 先にフレームごとの型スタックを取得し、TypeStacksにつめる
    AST::InstrView::iterator PCCopy = PC;
    uint32_t StackIdx = 1;
    for (size_t I = FrameStack.size()-1; I > 0; --I, ++StackIdx) {
      auto f = FrameStack[I];
      const Runtime::Instance::ModuleInstance* ModInst = f.Module;
      // NOTE: リターンアドレスは、実行しているアドレスの1つまえのアドレスを持っているので+1する
      if (I != FrameStack.size() - 1) PCCopy++;
      auto [FuncIdx, Offset] = getInstrAddrExpr(ModInst, PCCopy);
      TypeStacks[StackIdx] = getTypeStack_v2(FuncIdx, Offset);
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

    BaseCallStackEntry entries[LenFrame];
    auto _PC = PC;
    for (size_t I = FrameStack.size()-1; I > 0; --I) {
    // for (size_t I = 1; I < LenFrame; I++) {
      Runtime::StackManager::Frame f = FrameStack[I];
 
      // ModuleInstance 
      const Runtime::Instance::ModuleInstance* ModInst = f.Module;

      // ModInstがnullの場合、ModNameだけ出力してcontinue
      if (ModInst == nullptr) {
        std::cerr << "ModInst is nullptr" << std::endl;
        exit(1);
      }

      // PCのアドレスを取得
      // _PC = (I == FrameStack.size() - 1) ? _PC : _PC-1;
      auto [CurFidx, CurOffset] = getInstrAddrExpr(ModInst, _PC);
      CodePos pc = {
        .fidx = CurFidx,
        .offset = CurOffset,
      };
      spdlog::info("{}th frame: PC = ({}, {})", I, pc.fidx, pc.offset);

      // ローカル/スタック
      uint32_t StackBottom = f.VPos - f.Locals;
      ValVariant*LocalsPtr = ValueStack.data() + StackBottom;
      ValVariant* ValueStackPtr = ValueStack.data() + StackBottom + f.Locals;

      // ラベルスタック
      auto Res = ModInst->getFunc(pc.fidx);
      if (!Res) {
        std::cerr << "FuncIdx isn't correct" << std::endl; 
        exit(1);
      }
      Runtime::Instance::FunctionInstance* FuncInst = Res.value();
      std::vector<struct CtrlInfo> CtrlStack = getCtrlStack(_PC, FuncInst, WamrCellSums);
      _dumpStack(*this, f, _PC, LocalsPtr, ValueStackPtr, CtrlStack, entries[I-1]);
      spdlog::info("OK _dumpStack");

      // 各値を更新
      _PC = f.From;

      // debug
      // debugFrame(I, pc.fidx, f.Locals, f.Arity, f.VPos);
    }
    
    checkpoint_stack_v3(LenFrame, entries);
  }
  
  /// ================
  /// Restore functions
  /// ================
  void M::restoreMemoryV2(const Runtime::Instance::ModuleInstance* ModInst) {
    // ModInst->restoreMemInst(ImageDir);
    Array8 data = restore_memory();
    if (data.size == 0) {
      std::cerr << "ERROR: restore_memory" << std::endl;
      exit(1);
    }

    auto Res = ModInst->getMemory(0);
    Runtime::Instance::MemoryInstance *MemInst = Res.value();
    uint32_t old_page_size = MemInst->getPageSize();
    uint32_t new_page_size = data.size / 65536;

    // grow the page for restore memory
    bool succeeded_grown = MemInst->growPage(new_page_size- old_page_size);
    if (!succeeded_grown) {
      std::cerr << "ERROR: Failed to grow page" << std::endl;
      // compare between new page size and old page size
      std::cout << "DEBUG: page size (new, old): " << "(" << new_page_size << ", " << old_page_size << ")" << std::endl;
      exit(1);
    }

    Span<const Byte> SpanData(data.contents, data.size);
    auto Res2 = MemInst->setBytes(SpanData, 0, 0, data.size);
    if (!Res2) {
      std::cerr << "ERROR: restore_memory" << std::endl;
      exit(1);
    }
  }

  Expect<void> M::restoreStackV2(Runtime::StackManager& StackMgr) {
    const Runtime::Instance::ModuleInstance *Module = StackMgr.getModule();
    
    // restore stack
    CallStack cs = restore_stack();
    print_call_stack(&cs);
    

    uint32_t LenFrame = cs.size;
    // std::ifstream ifs(ImageDir + "frame.img", std::ios::binary);
    // ifs.read(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    // ifs.close();


    AST::InstrView::iterator PC, From;
    // LenFrame-1から始まるのは、Stack{LenFrame}.imgがダミーフレームだから
    From = StackMgr.popFrame();
    // for (size_t I = LenFrame; I > 0; --I) {
    for (size_t I = 0; I < LenFrame; ++I) {
      CallStackEntry entry = cs.entries[I];
      spdlog::info("{}th pc = ({}, {})", I, entry.pc.fidx, entry.pc.offset);
      // ifs.open(ImageDir + "stack" + std::to_string(I) + ".img", std::ios::binary);

      // 関数インデックスのロード
      // uint32_t EnterFuncIdx;
      // ifs.read(reinterpret_cast<char *>(&EnterFuncIdx), sizeof(uint32_t));
      
        auto ResPC = _restorePC(Module, entry.pc.fidx, entry.pc.offset);
        if (!ResPC) {
          return Unexpect(ResPC);
        }
        PC = ResPC.value();
        // リターンアドレスは1つ前のアドレスを持っているので-1する
        // if (I < LenFrame) PC--;

        // ローカルと返り値の数
        auto ResFunc = Module->getFunc(entry.pc.fidx);
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

      // if (I == 0) From = PC; // 一番bottomのフレームのリターンアドレスはWasmEdge特有なので、それを使う
      // 先頭フレームはフレームスタックに入っていないのでpushしない
      StackMgr._pushFrame(Module, From, Locals, RetsN, VPos, false);
      // if (I < LenFrame-1) {
      // }

      // 値スタック
      std::cerr << "restore locals" << std::endl; appendConverted(StackMgr, entry.locals);
      std::cerr << "restore stack" << std::endl;  appendConverted(StackMgr, entry.value_stack);

      From = PC;

      // debug
      // debugFrame(I, EnterFuncIdx, Locals, RetsN, VPos);
    }
    return {};
  }

} // namespace Runtime
} // namespace WasmEdge