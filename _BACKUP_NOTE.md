# Z1 调研文档公开快照说明

- 来源：本机 `mainline_recon/`（未被 git 跟踪，此前只有一份本地副本）中的全部 `*.md` 调研结论，加 `out/PACKAGE_NOTE*.md` 与工作区根的部分文档。
- **不含**：串口原始日志 `*.log`、二进制取证料、镜像。`CLAUDE.md`（按记录含 GitHub PAT）与 `AGENTS.md`（内含本机 sudo 口令）一律排除。
- 已剔除 9 份审计快照目录里内容完全相同的 14212B 模板 README：
  - mainline_recon/fork_files/README.md
  - mainline_recon/fork_linux_connsys_btif_resource_audit_src_20260718/README.md
  - mainline_recon/fork_linux_connsys_chipid_audit_src_20260717/README.md
  - mainline_recon/fork_linux_i2c_controller_audit_src_20260717/README.md
  - mainline_recon/fork_linux_layout2m_mfd_only_strict_src_20260718/README.md
  - mainline_recon/fork_linux_mt6323_vcn_register_audit_src_20260718/README.md
  - mainline_recon/fork_linux_pmic_audit_src_20260717/README.md
  - mainline_recon/fork_linux_touch_i2c1_controller_only_current_src_20260718/README.md
  - mainline_recon/fork_linux_vcn_chipid_rebase_audit_src_20260720/README.md

## 公开快照前已做的脱敏

邮箱、MAC、11 位手机号、15 位数字（IMEI 候选）、`serial=<SERIAL> 赋值、本机用户名路径 `/home/<user>`、主机名字符串、口令字段，均按值掩码。
