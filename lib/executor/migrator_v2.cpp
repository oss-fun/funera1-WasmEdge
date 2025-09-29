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
#include <wasmig/stack.h>

namespace fs = std::filesystem;

// definition constant values of type size
// 1: S32, 2: S64, 4: S128
static constexpr uint8_t TYPE_S32 = 1;
static constexpr uint8_t TYPE_S64 = 2;
static constexpr uint8_t TYPE_S128 = 4;


namespace WasmEdge {
namespace Executor {
  using M = Migrator;
  static bool checkpointFlag = false;

  bool setCheckpointFlag(bool flag) {
    checkpointFlag = flag;
    return true;
  }
  bool getCheckpointFlag() {
    return checkpointFlag;
  }

  std::vector<uint8_t> M::getTypeStackV2(uint32_t FuncIdx, uint32_t Offset, bool isTopFrame) {
    uint32_t offset = (isTopFrame) ? Offset : Offset + 1;
    StackTable table = get_stack_table(FuncIdx, offset);
    std::vector<uint8_t> TypeStack(table.size);
    for (size_t i = 0; i < table.size; i++) {
      StackTableEntry entry = table.data[i];
      TypeStack[i] = entry.ty;
    }
    return TypeStack;
  }

  bool M::isExistTypeStackTableV2() {
    namespace fs = std::filesystem;
    return fs::exists(ImageDir + "stack-table.msgpack");
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
    wasmig_checkpoint_memory(data.data(), page_size);
  }

  // /// Convert a ValVariant value to uint32_t array representation for serialization
  // void convertValueToUint32Array(std::vector<uint32_t>& targetArray, uint8_t valueType, const ValVariant& value) {
  //     switch (valueType) {
  //       case TYPE_S32:
  //         {
  //           targetArray.push_back(value.get<uint32_t>());
  //           break;
  //         }
  //       case TYPE_S64:
  //         {
  //           // Pack 64-bit value into two 32-bit values: [low_bits, high_bits]
  //           int64_t val64 = value.get<int64_t>();
  //           uint32_t lowBits = static_cast<uint32_t>(val64 & 0xFFFFFFFFLL);
  //           uint32_t highBits = static_cast<uint32_t>((val64 >> 32) & 0xFFFFFFFFLL);
  //           targetArray.push_back(lowBits);
  //           targetArray.push_back(highBits);
  //           break;
  //         }
  //       case TYPE_S128:
  //         {
  //           std::cerr << "Error: V128 type is not supported in migration" << std::endl;
  //           exit(1);
  //         }
  //       default:
  //         {
  //           std::cerr << "Error: Unknown value type in conversion: " << static_cast<int>(valueType) << std::endl;
  //           exit(1);
  //         }
  //     }
  // }

  /// Restore values from uint32_t array representation back to the stack manager
  void restoreValuesFromUint32Array(Runtime::StackManager& stackManager, TypedArray serializedArray) {
    size_t arrayIndex = 0;
    
    for (size_t typeIndex = 0; typeIndex < serializedArray.types.size; typeIndex++) {
      uint8_t valueType = serializedArray.types.contents[typeIndex];
      
      switch (valueType) {
        case TYPE_S32:
          {
            if (arrayIndex >= serializedArray.values.size) {
              std::cerr << "Error: Array index out of bounds during S32 restoration" << std::endl;
              exit(1);
            }
            stackManager.push(serializedArray.values.contents[arrayIndex++]);
            break;
          }
        case TYPE_S64:
          {
            if (arrayIndex + 1 >= serializedArray.values.size) {
              std::cerr << "Error: Array index out of bounds during S64 restoration" << std::endl;
              exit(1);
            }
            // Unpack two 32-bit values back into 64-bit: [low_bits, high_bits]
            uint32_t lowBits = serializedArray.values.contents[arrayIndex++];
            uint32_t highBits = serializedArray.values.contents[arrayIndex++];
            int64_t val64 = (static_cast<int64_t>(highBits) << 32) | static_cast<int64_t>(lowBits);
            stackManager.push(val64);
            break;
          }
        case TYPE_S128:
          {
            std::cerr << "Error: V128 type is not supported in migration" << std::endl;
            exit(1);
          }
        default:
          {
            std::cerr << "Error: Unknown value type in restoration: " << static_cast<int>(valueType) << std::endl;
            exit(1);
          }
      }
    }
  }

  bool materialize_stack_values(
    Stack addr_stack, 
    Stack type_stack, 
    std::vector<ValVariant>& raw_stack,
    uint8_t* type_buf, uint32_t* value_buf, 
    uint32_t stack_count, uint32_t stack_size) 
  {
    uint32_t stack_ptr = 0;
    StackIterator addr_it = wasmig_stack_iterator_create(addr_stack);
    StackIterator type_it = wasmig_stack_iterator_create(type_stack);
    if (!addr_it || !type_it) {
        wasmig_error("failed to create stack iterators\n");
        if (addr_it) wasmig_stack_iterator_destroy(addr_it);
        if (type_it) wasmig_stack_iterator_destroy(type_it);
        return false;
    }

    stack_ptr = stack_size;
    uint32_t index = 0;
    while (wasmig_stack_iterator_has_next(addr_it) && wasmig_stack_iterator_has_next(type_it)) {
        index++;
        uint64_t address = wasmig_stack_iterator_next(addr_it);
        uint32_t type = (uint32_t)wasmig_stack_iterator_next(type_it);

        type_buf[stack_count - index] = type;
        stack_ptr -= type;

        // that is a case if value is in register
        switch (type) {
            case 1: // i32
            {
                wasmig_debug("reconstruct stack[%u]: i32 %u\n", stack_ptr, (uint32_t)address);
                value_buf[stack_ptr] = raw_stack[(size_t)address].get<uint32_t>();
                break;
            }
            case 2: // i64
            {
                wasmig_debug("reconstruct stack[%u]: i64 %u\n", stack_ptr, (uint64_t)address);
                uint64_t val64 = raw_stack[(size_t)address].get<uint64_t>();
                uint32_t lowBits = static_cast<uint32_t>(val64 & 0xFFFFFFFFLL);
                uint32_t highBits = static_cast<uint32_t>((val64 >> 32) & 0xFFFFFFFFLL);
                value_buf[stack_ptr]   = lowBits;
                value_buf[stack_ptr+1] = highBits;
                break;
            }
            default:
                wasmig_error("unknown type: %d\n", type);
                break;
        }
    }
    wasmig_stack_iterator_destroy(addr_it);
    wasmig_stack_iterator_destroy(type_it);
    return true;
  }

  // calculate stack and locals size. stack size has all entries including local size.
  bool calc_stack_and_local_entries(Stack type_stack, uint32_t locals_count,
                          uint32_t* out_stack_count,
                          uint32_t* out_stack_size,
                          uint32_t* out_locals_size) {
      uint32_t stack_count = 0;
      uint32_t stack_size  = 0;
      uint32_t locals_size = 0;

      // リングバッファで末尾 locals_count 個を保持
      uint32_t *ring = (uint32_t *)((locals_count>0) ? malloc(sizeof(uint32_t) * locals_count) : NULL);
      uint32_t idx = 0;

      StackIterator it = wasmig_stack_iterator_create(type_stack);
      if (!it) {
          free(ring);
          wasmig_error("failed to create type iterator\n");
          return false;
      }

      while (wasmig_stack_iterator_has_next(it)) {
          uint32_t val = (uint32_t)wasmig_stack_iterator_next(it);
          stack_count++;
          stack_size += val;

          if (locals_count) {
              ring[idx % locals_count] = val;
              idx++;
          }
      }
      wasmig_stack_iterator_destroy(it);

      // locals の和を計算
      if (locals_count) {
          uint32_t take = (stack_count < locals_count) ? stack_count : locals_count;
          for (uint32_t i = 0; i < take; i++) {
              locals_size += ring[(idx - take + i) % locals_count];
          }
      }

      free(ring);

      if (out_stack_count) *out_stack_count = stack_count;
      if (out_stack_size)  *out_stack_size  = stack_size;
      if (out_locals_size) *out_locals_size = locals_size;
      
      return true;
  }

  bool load_metadata_stacks(uint32_t fidx, uint32_t offset, Stack* addr_stack, Stack* type_stack) {
      StackStateMap m = wasmig_stack_state_map_registry_load(fidx);
      if (!m) {
          wasmig_error("no stack state map for function %d\n", fidx);
          return false;
      }
      if (!wasmig_stack_state_load_pair(m, offset, addr_stack, type_stack)) {
          wasmig_error("failed to load metadata stack\n");
          return false;
      }
      wasmig_stack_print(*addr_stack);
      wasmig_stack_print(*type_stack);
      return true;
  }
  
  static bool
  _setup_value_stacks(
    Runtime::StackManager::Frame frame,
    std::vector<ValVariant> raw_stack, 
    CodePos pc, 
    bool isFrameTop,
    TypedArray *out_locals, 
    TypedArray *out_value_stack)
  {
      // wasmig_info("fidx: %d, offset: %d\n", call_pos.fidx, call_pos.offset);
      if (!isFrameTop)
          pc.offset += 1;

    Stack addr_stack, type_stack;
    wasmig_info("Loading type stack for fidx=%d, offset=%d (isTopFrame=%d)\n", pc.fidx, pc.offset, isFrameTop);
    if (!load_metadata_stacks(pc.fidx, pc.offset, &addr_stack, &type_stack))
        return false;

    uint32_t local_count = frame.Locals;
    uint32_t stack_size, stack_count, local_size;
    if (!calc_stack_and_local_entries(type_stack, local_count, &stack_count, &stack_size, &local_size)) {
        wasmig_error("failed count_stack_entries");
        return false;
    }
    wasmig_info("stack_count=%d, stack_size=%d\n", stack_count, stack_size);
    wasmig_info("local_count=%d, local_size=%d\n", local_count, local_size);

    uint8_t* type_buf = (uint8_t *)malloc(stack_size * sizeof(uint8_t));
    uint32_t* value_buf = (uint32_t *)malloc(stack_size * sizeof(uint32_t));
    if (!materialize_stack_values(addr_stack, type_stack, raw_stack, type_buf, value_buf, stack_count, stack_size))
        return false;

      // // Array8 locals_types = get_local_types(pc.fidx);
      // // StackTable stack_table = get_stack_table(pc.fidx, offset);
      // // Array8 stack_types = convert_type_stack_from_stack_table(&stack_table);
      
      // // Allocate buffer for locals (malloc required to avoid errors)
      // // TODO: Avoid memcpy - currently doing double value copying which is wasteful
      // uint32_t* localsBuffer = (uint32_t *)malloc(localsVec.size() * sizeof(uint32_t));
      // memcpy(localsBuffer, localsVec.data(), localsVec.size() * sizeof(uint32_t));
      // out_locals->types = locals_types;
      // out_locals->values = {
      //     .size = (uint32_t)localsVec.size(),
      //     .contents = localsBuffer,
      // };

      // // Process value stack
      // std::vector<uint32_t> stackVec;
      // for (size_t i = 0; i < typeStack.size(); i++) {
      //     convertValueToUint32Array(stackVec, typeStack[i], valueStackPtr[i]);
      // }
      
      // // Allocate buffer for stack
      // uint32_t* stackBuffer = (uint32_t *)malloc(stackVec.size() * sizeof(uint32_t));
      // memcpy(stackBuffer, stackVec.data(), stackVec.size() * sizeof(uint32_t));
      // out_value_stack->types = stack_types;
      // out_value_stack->values = {
      //     .size = (uint32_t)stackVec.size(),
      //     .contents = stackBuffer,
      // };
      // spdlog::info("Set value stack with {} elements", stackVec.size());

      // pack the stack
      out_locals->types = (Array8){local_count, type_buf};
      out_locals->values = (Array32){local_size, value_buf};
      out_value_stack->types = (Array8){stack_count - local_count, type_buf + local_count};
      out_value_stack->values = (Array32){stack_size - local_size, value_buf + local_size};
      
      // print log
      wasmig_info("locals: {count=%d, size=%d}\n", local_count, local_size);
      wasmig_info("value_stack: {count=%d, size=%d}\n", stack_count - local_count, stack_size - local_size);

      return true;
  }

  void _dumpStack(
      Runtime::StackManager::Frame frame,
      CodePos pc,
      std::vector<ValVariant>& raw_stack,
      CallStackEntry& entry,
      bool isFrameTop
  ) {
    // Set program counter
    entry.pc = pc;

    // Setup a value stack
    TypedArray locals, value_stack;
    _setup_value_stacks(frame, raw_stack, pc, isFrameTop, &locals, &value_stack);
  
    // store to entry
    entry.pc = pc;
    entry.locals = locals;
    entry.value_stack = value_stack;
    entry.label_stack = (LabelStack){0, NULL, NULL, NULL, NULL};
}

  void M::dumpStackV2(Runtime::StackManager& StackMgr, AST::InstrView::iterator PC) {
      std::vector<Runtime::StackManager::Frame> frameStack = StackMgr.getFrameStack();
      std::vector<ValVariant> valueStack = StackMgr.getValueStack();
      std::vector<std::vector<uint8_t>> typeStacks(frameStack.size());
      size_t frameCount = frameStack.size() - 1;
      
      // Prepare entries array
      CallStackEntry entries[frameCount];
      auto currentPC = PC;
      
      // Process each frame for dumping
      for (size_t frameIndex = frameStack.size() - 1; frameIndex > 0; --frameIndex) {
          Runtime::StackManager::Frame frame = frameStack[frameIndex];
          const Runtime::Instance::ModuleInstance* modInst = frame.Module;
          bool isTopFrame = (frameIndex == frameStack.size() - 1);
          // if (!isTopFrame) currentPC += 1;

          if (modInst == nullptr) {
              std::cerr << "Error: ModuleInstance is null for frame " << frameIndex << std::endl;
              exit(1);
          }

          // Get current PC address
          auto [currentFuncIdx, currentOffset] = getInstrAddrExpr(modInst, currentPC);
          CodePos pc = {
              .fidx = currentFuncIdx,
              .offset = currentOffset,
          };
          // spdlog::info("Processing frame {}: PC = ({}, {}), OpCode: {}", frameIndex, pc.fidx, pc.offset, (currentPC)->getOpCode());

          // Calculate local and stack pointers
          uint32_t stackBottom = frame.VPos - frame.Locals;
          std::vector<ValVariant> raw_stack(valueStack.begin() + stackBottom, valueStack.end());
          // ValVariant* raw_stack = valueStack.data() + stackBottom;
          // ValVariant* localsPtr = valueStack.data() + stackBottom;
          // ValVariant* valueStackPtr = valueStack.data() + stackBottom + frame.Locals;

          // Dump this frame using the pre-computed type stack
          size_t entryIndex = frameIndex - 1;  // Convert frame index to entry array index
          _dumpStack(frame, pc, raw_stack, entries[entryIndex], isTopFrame);
          // spdlog::info("Successfully dumped frame {}", frameIndex);

          // Update PC for next iteration
          currentPC = frame.From;
      }
      
      // Checkpoint the complete stack
      printf("Checkpointing stack with %zu frames\n", frameCount);
      CallStack cs = {.size = (uint32_t)frameCount, .entries = entries};
      print_call_stack(&cs);
      wasmig_checkpoint_stack_v4(frameCount, entries);
      // spdlog::info("Stack dump completed successfully");
  }
  
  /// ================
  /// Restore functions
  /// ================
  void M::restoreMemoryV2(const Runtime::Instance::ModuleInstance* ModInst) {
    // ModInst->restoreMemInst(ImageDir);
    Array8 data = wasmig_restore_memory();
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
    
    StackMgr.reset();
    
    // restore stack
    CallStack cs = wasmig_restore_stack();
    wasmig_debug("Restored stack with %d frames\n", cs.size);
    // print_call_stack(&cs);
    

    uint32_t LenFrame = cs.size;
    // std::ifstream ifs(ImageDir + "frame.img", std::ios::binary);
    // ifs.read(reinterpret_cast<char *>(&LenFrame), sizeof(uint32_t));
    // ifs.close();


    AST::InstrView::iterator PC, From;
    // LenFrame-1から始まるのは、Stack{LenFrame}.imgがダミーフレームだから
    // From = StackMgr.popFrame();
    // for (size_t I = LenFrame; I > 0; --I) {
    for (size_t I = 0; I < LenFrame; ++I) {
      CallStackEntry entry = cs.entries[I];
      // spdlog::info("{}th pc = ({}, {})", I, entry.pc.fidx, entry.pc.offset);
      // ifs.open(ImageDir + "stack" + std::to_string(I) + ".img", std::ios::binary);

      // 関数インデックスのロード
      // uint32_t EnterFuncIdx;
      // ifs.read(reinterpret_cast<char *>(&EnterFuncIdx), sizeof(uint32_t));
      
      auto ResPC = _restorePC(Module, entry.pc.fidx, entry.pc.offset);
      if (!ResPC) {
        return Unexpect(ResPC);
      }
      PC = ResPC.value();
      // WasmEdgeのリターンアドレスは1つ前のアドレスを持っているので-1する
      // if (I < LenFrame) PC -= 1;

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
      // std::cerr << "restore locals" << std::endl; 
      restoreValuesFromUint32Array(StackMgr, entry.locals);
      // std::cerr << "restore stack" << std::endl;  
      restoreValuesFromUint32Array(StackMgr, entry.value_stack);

      wasmig_debug("Restored frame %zu: PC = (%u, %u), Locals = %u, Rets = %u, VPos = %u\n", 
                  I, entry.pc.fidx, entry.pc.offset, Locals, RetsN, VPos);

      From = PC;

      // debug
      // debugFrame(I, EnterFuncIdx, Locals, RetsN, VPos);
    }
    return {};
  }

} // namespace Runtime
} // namespace WasmEdge