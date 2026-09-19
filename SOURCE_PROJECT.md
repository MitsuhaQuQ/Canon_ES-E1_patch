# EOS-1V compatibility source project

This source directory rebuilds every project-owned executable component in the clean release. It does not require or contain Canon binaries.

## Prerequisites

- Windows 10 or Windows 11, x64;
- Visual Studio or Visual Studio Build Tools with **Desktop development with C++**;
- a Windows SDK providing SetupAPI, WinUSB, BCrypt, and User32 import libraries.

The build scripts locate the newest installed Visual Studio C++ toolchain through `vswhere.exe`.

## One-command build

Run `build_all.cmd` from a normal Command Prompt. It produces:

- `EOSHOOKX.dll` — PE32/x86 bridge loaded by the original 32-bit Canon application;
- `EOS1V_Patcher.exe` — PE32+/x64 local patch installer with the static C runtime;
- `smoke_test.exe` and `bridge_device_test.exe` — x86 bridge diagnostics;
- `eos_probe.exe` — x64 standalone WinUSB transport probe.

## Individual builds

`build.cmd` builds the x86 bridge DLL and its tests from `eosbridge.c` plus `eosbridge.def`.

`smoke_test.exe` validates the bridge's required legacy exports and ordinary non-COM file forwarding without requiring any Canon binary. `bridge_device_test.exe` is the hardware-facing bridge diagnostic.

`build_patcher.cmd` builds the x64 native local patcher from `eos1v_patcher.c`. The patcher uses Windows CNG (`bcrypt.dll`) for SHA-256, supports a folder passed by drag-and-drop, and otherwise operates on its own directory.

`build_patcher.cmd` requires the already built `EOSHOOKX.dll`, calculates its SHA-256, regenerates `bridge_hash.h`, compiles `eos1v_patcher.rc`, and embeds the DLL as an `RCDATA` resource in `EOS1V_Patcher.exe`. Thus a source-built patcher carries and accepts the bridge DLL produced in the same build even when PE linker metadata changes the binary hash.

`build_probe.cmd` builds the standalone WinUSB transport probe.

## Clean-distribution rule

Do not add Canon `Remote.exe`, `Eos1v.drv`, `Memory.exe`, locally patched copies, or binary deltas to this project. `EOS1V_Patcher.exe` accepts only the three exact original hashes recorded in its source and creates the equal-length import-name modifications on the user's machine.
