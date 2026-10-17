# USB Boot Log 分析报告

**分析时间**: 2026-08-16  
**检查日志**: 5 个最近 boot log (20260815_174557 ~ 213100)

## USB 相关 Log 行

### 日志 20260815_204801 (8KB, 最完整)
```
[    0.262693] usb_phy_generic usb_phy_generic.2.auto: dummy supplies not allowed for exclusive requests (id=vbus)
[    0.284463] phy-mtk-mt6572-usb2-handoff 11110800.phy: HS slew-rate calibration timed out; using 4
```

### 其他日志
- **20260815_174557** (101KB): 无任何 USB/gadget/musb/phy/udc 内核消息
- **20260815_175914** (1.6KB): 仅元数据含 `bridge_usb_line_coding`
- **20260815_200224** (9.2KB): 无 USB 内核消息
- **20260815_213100** (416B): 空 capture (timeout)

## 成功/失败点

### ✅ 部分成功
- **USB PHY probe**: `phy-mtk-mt6572-usb2-handoff 11110800.phy` 成功探测
- **PHY 降级运行**: HS slew-rate 校准超时后使用默认值 4 (未致命)

### ❌ 失败点
1. **VBUS supply 错误**: `dummy supplies not allowed for exclusive requests (id=vbus)`
   - USB PHY generic 驱动尝试请求 VBUS regulator 失败
   - 可能原因: dts 未配置 vbus-supply 或 regulator 未注册

2. **MUSB probe 缺失**: 所有日志均无 `musb-hdrc` 或 `musb mediatek` probe 消息
   - 可能原因: 
     - MUSB 驱动未编译进内核 (CONFIG_USB_MUSB_MEDIATEK 未开)
     - dts `&usb` 节点 status="disabled" 或 compatible 不匹配
     - MUSB probe 依赖 PHY 就绪，但 PHY 因 VBUS 错误未完成初始化

3. **UDC 未注册**: 无 `udc` 或 `gadget` 相关消息
   - MUSB 未 probe → UDC 无法注册

4. **Gadget 未创建**: 无 `gadget` 或 `configfs` 消息
   - UDC 未注册 → gadget 无法绑定

5. **/dev/ttyGS0 不存在**: 无 ACM gadget 创建消息
   - 完整链路失败: PHY → MUSB → UDC → gadget → ttyGS0

## 根因分析

**主因**: MUSB 驱动未 probe，导致 USB gadget 全链路失败

**根因链**:
1. **VBUS supply 缺失** → PHY 初始化警告 (非致命但影响后续)
2. **MUSB probe 完全缺失** → 核心问题
   - 最可能: `CONFIG_USB_MUSB_MEDIATEK=y` 未编进内核或 dts `&usb` 节点配置错误
   - 次可能: MUSB 依赖 PHY 就绪，PHY 因 VBUS 错误延迟初始化，MUSB probe 超时

**排查建议**:
1. 检查 `.config` 确认 `CONFIG_USB_MUSB_MEDIATEK=y` 且 `CONFIG_USB_MUSB_HDRC=y`
2. 检查 dts `&usb` 节点:
   - `status = "okay"` (非 disabled)
   - `dr_mode = "peripheral"` (Z1 只做 device)
   - `vbus-supply = <&mt6323_vbus>` (若缺此项导致 PHY 报错)
3. 检查 dts `&usb_phy` 节点:
   - `status = "okay"`
   - 包含 `vbus-supply` 属性
4. 若配置正确仍不 probe: 开 `CONFIG_USB_MUSB_DEBUG=y` 重编抓详细 log

**下一步**: 修复 VBUS supply + 确认 MUSB 配置后重编 boot.img 上板验证
