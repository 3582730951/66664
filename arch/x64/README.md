# arch/x64

- 支持 ABI：`sysv_x64`、`msvc_x64`
- 发射域：默认 VM1；构造 `X64Lifter(TargetDomain::vm2)` 时发射 VM2
- 手写 decoder IR：`InstructionIR{ mnemonic, operands, operand_kinds, operand_sizes, condition, immediate_values, memory, relative_target, flags }`
- 已验证指令族：
  - 整数/位运算：`mov/movsx/movzx/movsxd/add/sub/and/or/xor/cmp/test/imul/bsf/bsr/tzcnt/lzcnt/bt/bts/btr/btc`
  - 移位旋转：`rol/ror/rcl/rcr/shl/shr/sal/sar`
  - 控制流：`jmp/jcc/call/ret/push/pop/pushf/popf/leave`
  - 地址/访存：ModR/M、SIB、RIP-relative、`lea`、`mov` 到/从内存、`nop [mem]`
  - 系统：`syscall/rdtsc/rdtscp/cpuid/swapgs/endbr64`
  - SSE/AVX/EVEX 子集：`movaps/movapd/addps/addpd/addss/addsd/movdqa/movdqu/pshufd/pcmpeq*/pshufb/vzeroupper/vzeroall/vmovdqa/vmovdqu/vpxor/vpshufb/vfmadd/vfmsub/vpxorq/vmovdqu64`
- corpus：266 条内联编码样例
- real-binary probe：`/usr/bin/ls` `.text` 前 1024 字节，`unsupported=6/221 (2.71%)`
- 当前已知不支持/降级：
  - 稀有 EVEX mask/layout、AVX-512 更大子集、`MONITOR/MWAIT`、更广的 far control-transfer 仍走 opaque/diagnostic
  - VEX/EVEX FMA 与 EVEX foundation 目前 round-trip 走 decode-only（保留原字节）
- 已知限制：
  - lifter 已扩到整数/位运算/访存/控制流主路径，但不是对全部已解码 SIMD/系统指令都做 lowering
  - 复杂 VEX/EVEX 仍以 decoder correctness 优先，不在本轮做 JIT lowering
