# 三系统配置示例

本项目的重点是 UEFI 显示选择。下面记录与它配合的三系统结构，不是完整的 Windows/Linux 镜像制作工具，也不会替你创建或格式化分区。

## 三个入口

| 系统 | rEFInd 进入的文件 | 下一阶段 |
|---|---|---|
| 当前 Windows | `\EFI\Microsoft\Boot\bootmgfw.efi` | 现有 Windows BCD |
| Windows 虚拟磁盘 | `\EFI\Win10VHD\bootmgfw.efi` | 独立 BCD 指向虚拟磁盘 |
| Ubuntu 虚拟磁盘 | `\EFI\refind\vdiskchain` | `vdisk=/ubuntu.vhd.vtoy` |

脱敏配置见 [refind-main.conf](../examples/refind-main.conf)。先把每个入口配置到能够启动，再加入 HDMI 程序；这样显示与系统加载问题可以分开定位。

## Windows 虚拟磁盘

原案例已有一个能使用的 Windows 10 虚拟磁盘，采用独立目录和单独 BCD，把其中 Windows 加载项的 `device` 与 `osdevice` 指向目标文件，同时清理从原配置复制过来的不适用关联项。当前系统的 BCD 不作为随意试改的对象。

路径表示形式示例（仅用于理解，不能整段直接当命令执行）：

```text
device   vhd=[D:]\VMs\windows.vhdx
osdevice vhd=[D:]\VMs\windows.vhdx
```

创建新的 Windows 原生启动虚拟磁盘时，按目标 Windows 版本的官方要求操作。Microsoft 当前文档对 Windows 10 及更新版本规定使用 VHDX；不要把本案例旧文件的 `.vhd` 名称当作所有版本都受官方支持的保证。[Microsoft Native Boot 文档](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/boot-to-vhd--native-boot--add-a-virtual-hard-disk-to-the-boot-menu?view=windows-11)

独立启动目录不只是一个 `bootmgfw.efi` 文件，它还需要正确的 BCD 和相关支持文件。本仓库不分发这些 Microsoft 文件。不要把 `bcdboot` 对默认系统存储的操作与建立自定义独立 BCD 混为一谈。

## Linux 虚拟磁盘

本案例的 Ubuntu 虚拟磁盘已经具备 vtoy 启动支持，在其启动环境中可以确认相关脚本。rEFInd 调用外部 `vdiskchain`，再传入虚拟磁盘路径：

```text
loader \EFI\refind\vdiskchain
options "vdisk=/ubuntu.vhd.vtoy"
```

这里 `loader` 位于 EFI 分区；`vdisk=` 指向被查找的虚拟磁盘文件，不使用 Windows 的 `D:` 或 `E:` 盘符。vdiskchain 的适用文件系统与用法见 [上游说明](https://github.com/ventoy/vdiskchain)。`.vtoy` 后缀本身不会把一个未经准备的 Linux 镜像变成可启动镜像。

## 文件移动与更新

- Windows 虚拟磁盘改名或移动后，需要检查对应独立 BCD；只改菜单标题不够。
- Linux 虚拟磁盘改名或移入子目录后，需要检查 `vdisk=`，并同步主菜单和备用菜单。
- Linux 内核或 initramfs 更新后，应确认虚拟磁盘启动支持仍然存在。
- 源码、EFI 配置和日志备份不包含虚拟磁盘内的数据；系统镜像要另行备份。
- 某个 VHD 文件缺失通常只影响那个菜单项，不代表当前 Windows 或 HDMI 显示选择一定损坏。
