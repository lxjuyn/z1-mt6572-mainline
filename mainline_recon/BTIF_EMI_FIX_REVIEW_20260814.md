# BTIF ranges + CONNSYS EMI 迁移修复 — 只读复核报告 (2026-08-14)

> 只读源码审计,未触碰任何硬件/文件。结论基于:
> - `fork_linux/arch/arm/boot/dts/mediatek/mt6572-z1.dts`
> - `/tmp/connsys_build/conn_soc/mt6572/include/mtk_wcn_consys_hw.h`
> - `/tmp/connsys_build/conn_soc/mt6572/mtk_wcn_consys_hw.c`
> - `/tmp/connsys_build/conn_soc/mt6572/wmt_plat_alps.c`
> - `/tmp/connsys_build/btif/btif_dma_plat.c`, `btif_plat.c`
> - `fork_linux/drivers/of/address.c` (mainline 7.0-rc7 树内代码)

---

## 1. 修复①: btif 节点加空 `ranges;` — 源码层成立 ✅

### 1.1 DTS 现状 (mt6572-z1.dts, btif@1100e000 节点)

```
btif: btif@1100e000 {
    compatible = "mediatek,btif";
    #address-cells = <1>;
    #size-cells = <1>;
    ranges;                       <- 已加,空属性 = 1:1 翻译
    reg = <0x1100e000 0x1000>,
          <0x11000380 0x80>,
          <0x11000400 0x80>;
    ...
    btif_tx: btif_tx@11000380 { reg = <0x11000380 0x80>; ... };
    btif_rx: btif_rx@11000400 { reg = <0x11000400 0x80>; ... };
};
```

- `ranges;` 为空属性 (无 cell 参数)。按 DT 规范,空 ranges = 父/子地址 1:1 映射。
- btif 节点挂在根 `/` 下 (与 connsys_pwr/connectivity/wifi/reserved-memory 同级)。
- 根节点 `#address-cells=<1> #size-cells=<1>` (mt6572.dtsi 17-18 行),btif 及子节点继承 1/1。

### 1.2 内核翻译路径逐级核对 (drivers/of/address.c)

驱动侧调用链 (btif_dma_plat.c `hal_dma_set_default_setting`):
`of_find_compatible_node(NULL,NULL,"mediatek,btif_rx"/"mediatek,btif_tx")`
→ `of_iomap(node,0)` → `of_address_to_resource` → `of_translate_address`
→ `__of_translate_address(btif_tx, of_get_parent, addr=[0x11000380], "ranges")`。

`__of_translate_address` 循环:
- iter1: dev=btif_tx, parent=btif@1100e000, na=ns=1 (继承 btif 的 1/1)。
- dev 换成 btif, parent 换成根 → 调 `of_translate_one(dev=btif, ..., rprop="ranges")`:
  - `of_get_property(btif, "ranges", &rlen)` → **非 NULL**, rlen=0 (空属性存在)。
  - 第一个失败判断 `if (ranges == NULL && !quirk && strcmp(rprop,"dma-ranges"))` → ranges 非 NULL,条件为假,**不会 return 1**。
  - 第二个判断 `if (ranges == NULL || rlen == 0)` → rlen==0 为真 → **走 1:1 分支**: `offset = of_read_number(addr, na)` (=0x11000380),高 cell 清零,`goto finish` → `pbus->translate()` 返回成功 (0)。
- iter2: dev=根, parent=NULL → `return of_read_number(addr, na)` = **0x11000380**。

结论: 空 ranges 下 btif_tx/btif_rx 的 reg 正确翻译成 CPU 物理地址 0x11000380 / 0x11000400,`of_iomap` 返回有效映射。

**反证 (修复前)**: 若没有 `ranges;`,`of_get_property` 返回 NULL → 第一个判断真 → `return 1` → `__of_translate_address` 返回 `OF_BAD_ADDR` → `of_iomap` 返回 NULL → `hal_btif_dma_hw_init` 拿 NULL base 解引用 Oops。**且 `of_empty_ranges_quirk` 只在 powerpc (Mac) 上生效,ARM 上缺 ranges 就是硬失败** —— 所以空 `ranges;` 是本修复的正确且必要机制,不能依赖 quirk。

### 1.3 dtb 编译验证 ✅

- dtc 1.7.0 编译 `/tmp/mt6572-z1_review.dtb`,**exit 0**。
- 反编译 dtb 二进制确认 `btif@1100e000` 含 `ranges;` + `#address-cells=<0x01>` + `#size-cells=<0x01>`,子节点 btif_tx/btif_rx reg 均在。
- 仅 2 条**既有无关** warning:
  1. `simple_bus_reg` pinctrl unit-address 格式 (mt6572.dtsi 既有,与本次无关)。
  2. `unique_unit_address`: `/connsys@18070000` 与 `/connectivity@18070000` 重复 unit-address (in-tree connsys_pwr 与 out-of-tree connectivity 都在 18070000,见 §3.2)。

---

## 2. 修复②: CONSYS_EMI_AP_PHY_BASE 0x80080000 → 0xbe080000 — 源码层成立 ✅

### 2.1 头文件现状 (mtk_wcn_consys_hw.h)

```
165: #define CONSYS_EMI_AP_PHY_OFFSET  (0x80000)
166: #define CONSYS_EMI_AP_PHY_BASE    (0xbe080000)   <- 已改
167: #define CONSYS_EMI_FW_PHY_BASE    (0xf0080000)   <- 未动 (见 §3.1 隐患)
168: #define CONSYS_EMI_MEM_SIZE       (343*KBYTE)    /* 343K */
```

- 全树 grep `/tmp/connsys_build/`,**无任何残留 0x80080000** 引用,所有引用点都走宏。

### 2.2 使用点 (mtk_wcn_consys_hw.c)

- `gConEmiPhyBase = CONSYS_EMI_AP_PHY_BASE - CONSYS_EMI_AP_PHY_OFFSET` = 0xbe080000 - 0x80000 = **0xbe000000** (carveout 起始)。
- `ioremap(gConEmiPhyBase + CONSYS_EMI_AP_PHY_OFFSET, CONSYS_EMI_MEM_SIZE)` = ioremap(**0xbe080000**, 343K)。`#if 0` 分支 `ioremap(CONSYS_EMI_AP_PHY_BASE,...)` 等价,无歧义。
- init 与 restore (swsusp) 两处一致。

### 2.3 EMI 区间与 carveout 边界核对 ✅

| 项 | 值 |
|---|---|
| EMI AP 物理区间 | 0xbe080000 .. 0xbe0d5bff (excl 0xbe0d5c00),长 343K |
| reserved-memory carveout | connsys@be000000, reg=<0xbe000000 0x100000> = **0xbe000000..0xbe0fffff** (1MB, no-map) |
| 越界判断 | 0xbe080000 ≥ 0xbe000000 ✅,0xbe0d5c00 ≤ 0xbe100000 ✅ **不越界** |
| 起始到 carveout 底 | 512KB (MCU 代码区预留给 [0xbe000000,0xbe080000),与原布局 0x80000000→EMI 0x80080000 的相对间隔一致) |
| 尾部余量 | 169KB (0x2a400) |
| dts connsys_pwr reg[1] | <0xbe080000 0x55c00>,0x55c00 = 351232 = 343K,与头文件一致 ✅ |

- carveout 是 `no-map` → 区域不进入内核 RAM (memblock),`ioremap` 不会被 "attempt to ioremap RAM" 拦掉;AP 侧 coredump/握手区在此可正常 ioremap+读写。
- dts `connsys_pwr reg[1]` 与头文件宏、carveout 三者数值完全一致。

### 2.4 dtb 二进制确认

反编译 dtb: `connsys@be000000` reg=<0xbe000000 0x100000> + no-map ✅;`connsys@18070000` reg=<0x18070000 0x1000 0xbe080000 0x55c00> ✅。

---

## 3. 隐患 / 风险 (源码层无法完全排除,需上板验证)

### 3.1 【最大隐患】 CONSYS_EMI_FW_PHY_BASE=0xf0080000 未随迁

- wmt_plat_alps.c:81 `.emi_phy_addr = CONSYS_EMI_FW_PHY_BASE` (0xf0080000) 是 **CONN MCU 侧** 的共享 EMI 视图地址,写入 CONN MCU 告诉固件共享区在哪。
- 0xf0080000 是 vendor 原始值,offset 模式与旧 AP 基址 0x80080000 同源 (0xf0000000+0x80000 / 0x80000000+0x80000)。**AP 侧迁到 0xbe080000,但 FW 侧地址没动**。
- 若 MT6572 的 0xf0000000 别名窗口固定落在旧 0x80080000,则固件读写共享 EMI 仍打在旧位置 —— 现在那里是**普通 RAM** (carveout 已移走),轻则握手失败,重则静默踩内存。若别名窗口实际按当前 EMI 映射落在 0xbe080000,则一致。**源码无法证明哪边成立**。
- dts 注释也承认这一点: "vendor CONN firmware may be built against a fixed shared-EMI base of 0x80080000; if the CONN MCU misbehaves, revert both carveout AND connsys_pwr reg[1] together"。
- **建议**: 上板后确认 CONN MCU chip ID + EMI 握手是否正常;若 EMI 区 0xbe080000 无固件数据流,考虑连 `CONSYS_EMI_FW_PHY_BASE` 一起迁 (但它 32 位表达不了 0xbe080000+0xf0000000,若别名窗口固定则无解,需确认硬件行为)。

### 3.2 既有 DT 卫生问题 (非本次修复引入)

- dtc 报 `/connsys@18070000` 与 `/connectivity@18070000` 重复 unit-address。现状无害: in-tree connsys_pwr (`"mediatek,mt6572-connsys"`, disabled) 与 out-of-tree connectivity (`"mediatek,mt6572-consys"`) compatible 不同,`of_find_compatible_node("mediatek,mt6572-consys")` 只会命中 connectivity。但将来若 enable connsys_pwr,两者都会映射 0x18070000 + 操作同一组 regulator,需留意不双上电。

### 3.3 btif 驱动既有 vendor 小怪癖 (非本次修复)

- btif_dma_plat.c `hal_dma_set_default_setting` 里 `of_property_read_u32_index(node, "reg", 1, &phy_base)` 读的是子节点 reg 的**第 2 个 cell (=size 0x80)**,却当 "phy base" 用。目前只进 BTIF_INFO_FUNC 打印,不影响流程;若后续代码真拿它当地址用会错 (值 0x80)。标注即可。

### 3.4 结论汇总

| 检查项 | 结果 |
|---|---|
| btif `ranges;` 已加且格式正确 (空=1:1) | ✅ |
| of_translate_one 空 ranges 走 1:1,不 return 1 | ✅ (非 powerpc 也可用,因为属性存在) |
| dtc 编译 dtb 合法 | ✅ exit 0,仅 2 条既有无关 warning |
| CONSYS_EMI_AP_PHY_BASE=0xbe080000 | ✅ 无残留旧基址 |
| EMI [0xbe080000,0xbe0d5c00) 在 carveout [0xbe000000,0xbe100000) 内 | ✅ 尾余 169KB |
| dts reg[1]/carveout/头文件三者一致 | ✅ 均为 0xbe080000 / 0x55c00 |
| 唯一存疑 | CONSYS_EMI_FW_PHY_BASE=0xf0080000 未随迁,AP/FW 侧共享 EMI 视图是否一致需上板验证 |

**总评**: 两个修复在源码层均成立且互为一致 (dts carveout / dts reg[1] / 头文件宏三处数值闭环),dtb 合法,EMI 不越界。真正需要上板回答的只剩一个:**CONN 固件 (0xf0080000 别名窗口) 是否真的看得到迁到 0xbe080000 的共享 EMI**。
