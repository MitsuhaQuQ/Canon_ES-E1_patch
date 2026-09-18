# Canon_ES-E1_patch
THIS PATCH IS AI GENERATED but HUMAN TESTED. 

## About
 The CANON EOS 1v's customization needs a wire called ES-E1, and it needs a PC software.

 But the software only provides 32bit drivers that only runs on windows XP.
 
 This project allows the original 32-bit Canon **EOS LINK ES-E1** software to communicate with an EOS-1V through the ES-E1 USB interface on 64-bit Windows 11.

It provides a native WinUSB compatibility bridge and a self-contained patcher. The release does **not** contain Canon executables, drivers, modified Canon files, or binary patches capable of recreating them. You must supply your own lawfully obtained EOS LINK ES-E1 installation.
## Requirements

- 64-bit Windows 10 or Windows 11;
- Canon EOS-1V;
- Canon ES-E1 USB interface;
- a complete original EOS LINK ES-E1 program directory;
- the ES-E1 USB device bound to Microsoft's WinUSB driver.(Use ['zadig'](https://zadig.akeo.ie/) )

The device used for development reports:

```text
USB\VID_04A9&PID_3040
```

## How to Use
 **0. You need an origional ES-E1 hardware.**
 1.Plug the ES-E1 to your computer.
 2.Use ['Zadig'](https://zadig.akeo.ie/) to install **WinUSB** driver for the ES-E1(**USB ID 04A9_3040**).
 3.Get canon's origional software from anywhere.
 4.Download and Copy the patcher into the program folder where the Remote.exe and Memory.exe lives.
 5.Run the patcher.
 **If the Canon directory is under `Program Files`, run the patcher with administrator.**
 6.Use the new patched Remote.exe as origional one. Everything should work as intended.


## What works

The following operations have been tested with a real EOS-1V and ES-E1 on Windows 11:

- detecting and connecting to the camera;
- reading and changing C.Fn and P.Fn settings;
- synchronizing the camera date and time;
- downloading film shooting records;
- converting transferred temporary data into sequential `.EFD` files;
- automatically opening `Memory.exe` after a transfer as intended;
- adding downloaded rolls to the Memory database;
- starting the Canon application from a shortcut or a different working directory;
- performing the complete camera exit sequence when Remote is closed normally.

Two downloaded 13,312-byte film records were compared byte for byte with records produced by the working Windows XP installation and were identical.

## How it works

```mermaid
flowchart LR
    A[Original Remote.exe] --> B[Original Eos1v.drv]
    B --> C[EOSHOOKX.dll compatibility bridge]
    C --> D[Microsoft WinUSB]
    D --> E[Canon ES-E1 / EOS-1V]
```

The original software expects a serial-style Windows driver interface. `EOSHOOKX.dll` presents the required synchronous COM-like API inside the original 32-bit process, then translates those calls into WinUSB transfers and the ES-E1 KLSI 64-byte framing.

The bridge also fixes several assumptions made by the legacy application:

- driver discovery when the process starts in another directory;
- the incorrectly resolved `\DATA\` path;
- launching the local `Memory.exe` after a transfer;
- finalizing current `FI*.tmp` downloads as sequential `.EFD` files;
- keeping the camera in PC mode between operations;
- sending the observed two-stage exit exchange when Remote closes normally.






## Camera mode warning

The real camera returns to metering mode after the exit command. Before every new probe or Canon-software connection, check the camera itself and put it back into **PC mode**. Waiting or reconnecting the application does not replace this check.

During a normal session, the bridge suppresses the original application's premature one-byte exit command so that subsequent operations continue to work. When Remote closes normally, the bridge sends the complete observed exit exchange:

```text
F2 -> receive F4 -> echo F4 -> F2 -> receive F2
```

A forced process termination or crash cannot guarantee that this final exchange is sent.

## Patcher safeguards

The native x64 patcher uses Windows CNG for SHA-256.

It performs these checks and operations locally:

- accepts only the two tested original file hashes;
- creates and verifies `Remote.original.exe` and `Eos1v.original.drv`;
- changes one equal-length import name in each user-supplied file;
- verifies the exact expected patched hashes;
- extracts its embedded bridge through a temporary file;
- verifies the bridge before and after installation;
- recognizes a previously patched installation;
- removes old `_hooked` files and its own temporary files;
- refuses unknown Canon binary versions without heuristic patching.

Supported hashes:

| File | State | SHA-256 |
| --- | --- | --- |
| `Remote.exe` | original | `0B3DC2A9CC1EE3690E08B4D0DE52EAACC46F1182922E9B93C94E43CF7BB10C89` |
| `Eos1v.drv` | original | `B224AE5492BA644A5B5F228DBD7D1E1AF1AAEC7E880FB9DC56D0E0E47FEC66C5` |

The two local import-name changes are:

```text
Remote.exe:  ADVAPI32.dll -> EOSHOOKX.dll
Eos1v.drv:   KERNEL32.dll -> EOSHOOKX.dll
```

Each change modifies eight bytes because the replacement names have equal lengths.

## Diagnostic probe

`eos_probe.exe` is a standalone x64 WinUSB transport probe. It performs device enumeration, USB initialization, the observed `FF/F4` handshake, checksum-verified `F6` and `F1` queries, and an orderly shutdown. It does not change C.Fn/P.Fn settings or download or erase film records.

Run it from a terminal only after confirming that the camera is in PC mode:

```bat
eos_probe.exe
```

A successful run ends with:

```text
Probe completed successfully.
```

Because the probe sends the exit command, return the camera to PC mode before starting Remote afterward.

## Building from source

Install Visual Studio or Visual Studio Build Tools with **Desktop development with C++** and a Windows SDK. Then run:

```bat
cd source
build_all.cmd
```

The build produces:

- `EOSHOOKX.dll` — PE32/x86 bridge loaded by the original application;
- `EOS1V_Patcher.exe` — PE32+/x64 patcher with the newly built DLL embedded;
- `eos_probe.exe` — PE32+/x64 WinUSB diagnostic probe;
- `smoke_test.exe` and `bridge_device_test.exe` — PE32/x86 bridge diagnostics.

The final patcher build calculates the newly built DLL's SHA-256, generates `bridge_hash.h`, compiles `eos1v_patcher.rc`, and embeds the DLL as an `RCDATA` resource. The resulting EXE is therefore paired automatically with the bridge from the same build.

See [`SOURCE_PROJECT.md`](SOURCE_PROJECT.md) for the source layout and individual build commands.

## Troubleshooting

### `Unable to connect`

- Confirm that the camera is currently in PC mode.
- Confirm that VMware or another program does not own the USB device.
- Check for hardware ID `USB\VID_04A9&PID_3040` in Device Manager.
- Confirm that the device is using WinUSB on the Windows host.
- Run `eos_probe.exe` and preserve its complete console output.

### `Unsupported SHA-256`

The Canon file is not the exact version tested by this project, or it has already been modified. Restore the original installation. The patcher deliberately does not search for similar byte patterns in unknown versions.

### `Failed load shooting data`

Confirm that the complete Canon application directory is present, including `Memory.exe` and `DATA`. Check `EOSBRIDGE.LOG` in the application directory for the failing path or transfer.

### Memory does not open after transfer

Confirm that `Memory.exe` is beside `Remote.exe` and that security software did not block the legacy executable. The bridge resolves the launch relative to its installed directory.

## Restoring the original files

Close both Canon applications, then replace:

```text
Remote.exe  <- Remote.original.exe
Eos1v.drv  <- Eos1v.original.drv
```

After verifying the restored files, `EOSHOOKX.dll` and `EOSBRIDGE.LOG` may be removed from that application directory.

## Distribution and legal note

This repository is intended to distribute independently written interoperability code. It does not include Canon `Remote.exe`, `Eos1v.drv`, `Memory.exe`, modified copies of those files, or binary deltas that reconstruct them. Patching occurs only on files supplied locally by the user.

This structure does not guarantee immunity from copyright, contract, trademark, or anti-circumvention claims and is not legal advice. Applicability depends on the user's jurisdiction, lawful access to the original software, purpose, necessity, license terms, and distribution method. Relevant United States references include [17 U.S.C. § 1201](https://www.law.cornell.edu/uscode/text/17/1201) and the [U.S. Copyright Office Section 1201 Study](https://www.copyright.gov/policy/1201/).

Canon, EOS, EOS-1V, and ES-E1 are trademarks or product names of their respective owner. This project is independent and is not affiliated with or endorsed by Canon.
