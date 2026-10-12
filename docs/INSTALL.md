# 安装、验证与恢复

本教程假定你已经有一个能正常启动系统的 x64 UEFI rEFInd，且能够回到 Windows 或恢复介质。**先完成测试，再改默认入口。** 下面的命令都是供你在自己的机器上核对后执行的示例；本仓库没有自动安装或自动改启动项的脚本。

## 1. 核对适用条件

- 当前硬件配置与 README 的实测配置一致，或你已独立核对并适配设备路径。
- 显示器开机前已经接好，使用的是笔记本 HDMI 接口。
- 了解程序需要已有的 NVIDIA/平台 UEFI 图形驱动，不会安装新的显卡驱动。
- 能访问 EFI 系统分区，知道如何从固件菜单选择原 Windows Boot Manager。
- 当前实现未签名。Secure Boot 开启时不要假定它可以加载；本教程不自动关闭安全设置。

设备路径或输出接口不确定时，先阅读 [排查记录](DEBUGGING.md)。即使机型相同，BIOS 和显卡模式变化也可能影响匹配。

## 2. 备份并识别 EFI 分区

在管理员 PowerShell 中先记录启动配置：

```powershell
# 使用自己的备份位置；目录必须位于普通数据分区。
$backup = Join-Path $env:USERPROFILE ('Desktop\efi-backup-' + (Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $backup | Out-Null
bcdedit /enum firmware /v | Out-File (Join-Path $backup 'firmware-before.txt')
bcdedit /export (Join-Path $backup 'system-BCD')
```

`bcdedit /export` 不是 EFI 分区全部内容的备份；菜单、程序和独立 BCD 仍要分别备份。

```powershell
if (Test-Path S:\) { throw 'S: 已被使用，请另选未使用的盘符并同步替换后文。' }
mountvol S: /S
mountvol S: /L
Get-Partition | Where-Object GptType -eq '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}' |
    Select-Object DiskNumber,PartitionNumber,Guid,Size
Get-ChildItem S:\EFI
```

多磁盘机器可能有多个 ESP。核对实际挂载卷与磁盘分区，不能凭盘符或“第一个分区”猜测。rEFInd 示例要求的是 **ESP 的 GPT 分区 GUID**；不要把固件启动条目 GUID、文件系统卷标识和 GPT 分区 GUID 混用。

复制现有 `S:\EFI\refind` 到备份目录，并备份计划修改的其他目录和配置。不要删除或覆盖 `EFI\Microsoft`、厂商目录和现有系统启动文件。

## 3. 构建并准备独立菜单副本

先在仓库目录编译：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Diagnostics
```

**只有确认 S: 是目标 ESP 后**，按以下布局放置文件。路径已经存在时先检查用途，不要直接覆盖其他项目的同名文件。

```text
build/hdmi-auto.efi      → S:\EFI\gopdiag\hdmi-auto.efi
build/hdmi-menu.efi      → S:\EFI\gopdiag\hdmi-menu.efi
build/target-output.efi  → S:\EFI\gopdiag\target-output.efi
```

日志目录 `S:\EFI\gopdiag` 必须存在。诊断/手动版本要求能打开日志；自动版本允许日志打开失败后继续启动。

从已经正常工作的原 rEFInd 中复制：

```text
S:\EFI\refind\refind_x64.efi  → S:\EFI\refind-hdmi\refind_x64.efi
S:\EFI\refind\icons\          → S:\EFI\refind-hdmi\icons\
```

为 `refind-hdmi` 放置正常的系统菜单配置，可参考 [配置模板](../examples/refind-main.conf)。所有占位符都必须替换；删除你没有配置的系统选项。测试期间可把 `timeout` 设为 60，`resolution` 保持 `1920 1080`。

先建立这个只包含正常系统入口的副本，**再**往原菜单追加测试项，避免把手动测试入口再次复制进子菜单形成不必要的嵌套。不要复制不明驱动目录进新菜单。

## 4. 第一次实机测试：三色输出

在原 `EFI\refind\refind.conf` 后追加 [refind-tests.conf](../examples/refind-tests.conf) 中的两个条目，替换 ESP 分区 GUID。原来的默认系统暂不改变。

卸载临时盘符后重启：

```powershell
mountvol S: /D
```

选择 **HDMI color test**，依次观察：

1. 1920×1080 蓝色，15 秒。
2. 2560×1440 绿色，15 秒。
3. 640×480 红色，15 秒。

不支持的模式会跳过。程序结束后尝试恢复内屏；内屏可能在测试时暂时黑屏。先等待整个测试结束，不要把第一秒黑屏立即判断为失败。

如果没有看见画面，先收集 `EFI\gopdiag\target-output.txt`。单独一个 `EFI_SUCCESS` 不能证明物理屏幕真的显示了图像。不要在这一阶段设置自动启动。

## 5. 第二次实机测试：真正的 rEFInd 菜单

选择 **HDMI menu test**。程序会激活外屏 1080p，再启动 `EFI\refind-hdmi\refind_x64.efi`。

检查外屏能否显示菜单、键盘能否选择，以及系统能否正常加载。主菜单底部 Exit 工具可以返回并尝试恢复内屏。

这一阶段的结构是：原 rEFInd → 手动程序 → 新 rEFInd。它是隔离验证步骤，不是最终正常启动结构。

## 6. 建立独立的自动启动条目

确认前两步成功后，在管理员 PowerShell 中重新查看：

```powershell
bcdedit /enum firmware /v
```

从输出中找出**已验证可用的原 rEFInd 固件条目**，记录它的 ID、设备和路径。下面示例的占位符必须换成你自己的值，不是 Windows `{bootmgr}` 的 ID：

```powershell
$refindId = '<ORIGINAL_REFIND_FIRMWARE_ENTRY_GUID>'
if ($refindId -notmatch '^\{[0-9a-fA-F-]{36}\}$') { throw '先填写实际原 rEFInd 条目 ID。' }
$created = bcdedit /copy $refindId /d 'rEFInd HDMI Auto' | Out-String
if ($LASTEXITCODE -ne 0) { throw '创建启动条目失败。' }
$autoId = [regex]::Match($created,'\{[0-9a-fA-F-]{36}\}').Value
if (-not $autoId) { throw '未能提取新条目 ID；请查看 firmware 列表，不要继续。' }
bcdedit /set $autoId path '\EFI\gopdiag\hdmi-auto.efi'
if ($LASTEXITCODE -ne 0) { throw '自动程序路径设置失败。' }
bcdedit /enum $autoId /v
$autoId | Set-Content (Join-Path $backup 'hdmi-auto-entry-id.txt')
$refindId | Set-Content (Join-Path $backup 'original-refind-entry-id.txt')
```

复制的是原 rEFInd 的条目，因此设备位置应保持指向相同 ESP；仍需用枚举结果核对。路径中的程序必须已经存在。这里新建条目是为了保留原入口；原案例最终调整了之前创建的专用条目，读者不必照搬那个机器的 ID。

先做一次性启动验证：

```powershell
bcdedit /set '{fwbootmgr}' bootsequence $autoId
```

该设置改变下一次固件启动选择。再次核对命令返回结果，然后重启，验证固件直接进入程序后的外屏菜单。再用一次性选择或固件启动菜单，验证**关机拔掉 HDMI 后**进入同一自动入口能否使用内屏。

两种情况均成功后，才设为优先：

```powershell
bcdedit /set '{fwbootmgr}' displayorder $autoId /addfirst
```

最终路径为 UEFI → 自动程序 → 一层 rEFInd → 系统。日常可把主菜单 `timeout` 恢复为 10。

## 7. 恢复

保留原 rEFInd 和 Windows Boot Manager 入口。需要回到原菜单时，用记录的 ID：

```powershell
bcdedit /set '{fwbootmgr}' displayorder $refindId /addfirst
```

若之前设置的一次性选择仍存在，先查看 `{fwbootmgr}`，必要时清除 `bootsequence`：

```powershell
bcdedit /deletevalue '{fwbootmgr}' bootsequence
```

需要 Windows Boot Manager 优先时：

```powershell
bcdedit /set '{fwbootmgr}' displayorder '{bootmgr}' /addfirst
```

Windows 无法进入时，可从固件启动菜单选择保留的入口或使用自己的恢复介质。不要因为恢复而直接删掉整个 EFI 目录。

## 8. 验证成功后清理

可以删除原菜单中的两个测试条目，并移除 `target-output.efi`、`hdmi-menu.efi` 及对应测试日志。保留自动程序和它的日志目录。

不能删除：`EFI\refind-hdmi` 主菜单、`EFI\refind` 备用菜单，以及被系统菜单引用的 vdiskchain、Windows 启动器和 BCD。清理后核对运行中二进制哈希、配置路径和固件入口，避免把测试清理变成启动链重构。

## 已知边界

- 当前程序固定寻找内置设备路径和 1080p 模式；不是多接口自动探测框架。
- 如果固件报告切换成功而显示器仍然黑屏，程序不能感知实际视觉结果。
- 只验证了开机前插线/拔线，没有验证菜单期间热插拔。
- 固件调用挂死、ESP 损坏等问题不属于模拟测试能够保证恢复的情况。
- 改 BIOS、显卡模式或接口后，应重新检查设备路径和显示行为。
