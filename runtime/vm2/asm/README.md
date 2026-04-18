# VM2 text DSL

示例：

```asm
.vconst c0, 0x1, 0x2
start:
  ildimm r0, 20
  blnk @fib, 1
  bret
fib:
  ildimm r10, 2
  isub r11, r0, r10
  jp p1, @base
  ...
```

约定：
- 标签：`name:`
- 跳转目标：`@label`
- 向量常量：`.vconst <name>, <lo_u64>, <hi_u64>`
- key context id：`.keyctx 0x001122...`（16 bytes hex）
- 内存寻址：`[sp+0]`、`[r4-16]`
