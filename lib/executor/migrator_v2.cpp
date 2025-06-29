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
            int32_t low_bits = array.values.contents[iter++];   // Contains original LOW 32 bits
            int32_t high_bits = array.values.contents[iter++];  // Contains original HIGH 32 bits
            int64_t val64 = ((int64_t)high_bits << 32) | (low_bits & 0xFFFFFFFF);
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
    Migrator &migrator,
    Runtime::StackManager::Frame frame,
    AST::InstrView::iterator PC, 
    ValVariant* localsPtr,
    ValVariant* valueStackPtr,
    std::vector<struct Migrator::CtrlInfo> &labelStack,
    BaseCallStackEntry& entry
) {
    const Runtime::Instance::ModuleInstance* modInst = frame.Module;
    if (localsPtr == nullptr || valueStackPtr == nullptr) {
        std::cerr << "Error: LocalsPtr or ValueStackPtr is null" << std::endl;
        exit(1);
    }
    
    // Set program counter
    auto [funcIdx, offset] = migrator.getInstrAddrExpr(modInst, PC);
    entry.pc = CodePos{
        .fidx = funcIdx,
        .offset = offset,
    };
    spdlog::info("Setting PC to ({}, {})", funcIdx, offset);
    
    // Process locals
    // TODO: Convert from 128bit slot-size stack to 32bit stack
    Array8 localTypes = get_local_types(funcIdx);
    std::vector<uint32_t> localsVec;
    for (size_t i = 0; i < localTypes.size; i++) {
        uint8_t type = localTypes.contents[i];
        _appendConverted(localsVec, type, localsPtr[i]);
    }
    
    // Allocate buffer for locals (malloc required to avoid errors)
    // TODO: Avoid memcpy - currently doing double value copying which is wasteful
    uint32_t* localsBuffer = (uint32_t *)malloc(localsVec.size() * sizeof(uint32_t));
    memcpy(localsBuffer, localsVec.data(), localsVec.size() * sizeof(uint32_t));
    entry.locals = {
        .size = (uint32_t)localsVec.size(),
        .contents = localsBuffer,
    };
    spdlog::info("Set locals with {} elements", localsVec.size());

    // Process value stack
    StackTable stackTable = get_stack_table(funcIdx, offset);
    std::vector<uint32_t> stackVec;
    for (size_t i = 0; i < stackTable.size; i++) {
        StackTableEntry stackEntry = stackTable.data[i];
        _appendConverted(stackVec, stackEntry.ty, valueStackPtr[i]);
    }
    
    // Allocate buffer for stack
    uint32_t* stackBuffer = (uint32_t *)malloc(stackVec.size() * sizeof(uint32_t));
    memcpy(stackBuffer, stackVec.data(), stackVec.size() * sizeof(uint32_t));
    entry.value_stack = {
        .size = (uint32_t)stackVec.size(),
        .contents = stackBuffer,
    };
    spdlog::info("Set value stack with {} elements", stackVec.size());

    // Process label stack
    uint32_t labelStackSize = labelStack.size();
    uint32_t* begins = (uint32_t *)malloc(labelStackSize * sizeof(uint32_t));
    uint32_t* targets = (uint32_t *)malloc(labelStackSize * sizeof(uint32_t));
    uint32_t* stackPointers = (uint32_t *)malloc(labelStackSize * sizeof(uint32_t));
    uint32_t* cellNums = (uint32_t *)malloc(labelStackSize * sizeof(uint32_t));
    
    for (size_t i = 0; i < labelStack.size(); i++) {
        Migrator::CtrlInfo ctrlInfo = labelStack[i];
        begins[i] = ctrlInfo.BeginAddrOfs;
        targets[i] = ctrlInfo.TargetAddrOfs;
        stackPointers[i] = ctrlInfo.SpOfs;
        cellNums[i] = ctrlInfo.ResultCells;
    }
    
    entry.label_stack = {
        .size = labelStackSize,
        .begins = begins,
        .targets = targets,
        .stack_pointers = stackPointers,
        .cell_nums = cellNums,
    };
    spdlog::info("Set label stack with {} elements", labelStackSize);
}

void M::dumpStackV2(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC) {
    std::vector<Runtime::StackManager::Frame> frameStack = StackMgr.getFrameStack();
    std::vector<ValVariant> valueStack = StackMgr.getValueStack();
    std::vector<std::vector<uint8_t>> typeStacks(frameStack.size());
    size_t frameCount = frameStack.size() - 1;
    
    spdlog::info("Starting stack dump with {} frames", frameCount);
    
    // Build type stacks for each frame
    AST::InstrView::iterator pcCopy = PC;
    uint32_t stackIdx = 1;
    for (size_t i = frameStack.size() - 1; i > 0; --i, ++stackIdx) {
        auto frame = frameStack[i];
        const Runtime::Instance::ModuleInstance* modInst = frame.Module;
        
        // NOTE: Return address points to previous instruction, so +1
        if (i != frameStack.size() - 1) {
            pcCopy++;
        }
        
        auto [funcIdx, offset] = getInstrAddrExpr(modInst, pcCopy);
        typeStacks[stackIdx] = getTypeStack_v2(funcIdx, offset);
        pcCopy = frame.From;
    }

    // Build WAMR cell cumulative sums
    uint32_t currentSum = 0;
    std::vector<uint32_t> wamrCellSums(StackMgr.size() + 1, 0);
    for (uint32_t stackIdx = typeStacks.size() - 1; stackIdx > 0; --stackIdx) {
        std::vector<uint8_t> typeStack = typeStacks[stackIdx];
        for (uint32_t i = 0; i < typeStack.size(); i++) {
            wamrCellSums[currentSum + 1] = wamrCellSums[currentSum] + typeStack[i];
            currentSum++;
        }
    }

    // Prepare entries array
    BaseCallStackEntry entries[frameCount];
    auto currentPC = PC;
    
    // Process each frame
    for (size_t i = frameStack.size() - 1; i > 0; --i) {
        Runtime::StackManager::Frame frame = frameStack[i];
        const Runtime::Instance::ModuleInstance* modInst = frame.Module;

        if (modInst == nullptr) {
            std::cerr << "Error: ModuleInstance is null for frame " << i << std::endl;
            exit(1);
        }

        // Get current PC address
        auto [currentFuncIdx, currentOffset] = getInstrAddrExpr(modInst, currentPC);
        CodePos pc = {
            .fidx = currentFuncIdx,
            .offset = currentOffset,
        };
        spdlog::info("Processing frame {}: PC = ({}, {})", i, pc.fidx, pc.offset);

        // Calculate local and stack pointers
        uint32_t stackBottom = frame.VPos - frame.Locals;
        ValVariant* localsPtr = valueStack.data() + stackBottom;
        ValVariant* valueStackPtr = valueStack.data() + stackBottom + frame.Locals;

        // Get control stack (label stack)
        auto funcResult = modInst->getFunc(pc.fidx);
        if (!funcResult) {
            std::cerr << "Error: Invalid function index " << pc.fidx << std::endl;
            exit(1);
        }
        Runtime::Instance::FunctionInstance* funcInst = funcResult.value();
        std::vector<struct CtrlInfo> ctrlStack = getCtrlStack(currentPC, funcInst, wamrCellSums);
        
        // Dump this frame
        _dumpStack(*this, frame, currentPC, localsPtr, valueStackPtr, ctrlStack, entries[i - 1]);
        spdlog::info("Successfully dumped frame {}", i);

        // Update PC for next iteration
        currentPC = frame.From;
    }
    
    // Checkpoint the complete stack
    checkpoint_stack_v3(frameCount, entries);
    spdlog::info("Stack dump completed successfully");
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