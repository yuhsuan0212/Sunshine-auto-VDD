# managed-vdd

A small Windows tool that turns a [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
(MTT VDD) on only while Sunshine streams. It is meant to be used as a `pre_display_prep_cmd` Do/Undo pair:

- **Do** (`activate`) adds the client's mode to `vdd_settings.xml` and enables the owned VDD device, before Sunshine
  configures displays.
- Sunshine's own display device configuration then makes the VDD the only display and applies the client's
  resolution, refresh rate and HDR state, and restores the previous displays when the last stream ends.
- **Undo** (`deactivate`) runs after that restoration, disables the VDD and puts `vdd_settings.xml` back.

The tool only ever touches the one device instance it provisioned, verified by an owner token stored on the device.

## Requirements

- Windows 10/11, administrator rights.
- The signed MTT VDD driver package (the `.inf` from its release).
- A Sunshine build with `pre_display_prep_cmd` (LizardByte/Sunshine#5808).
- Moonlight with **Optimize game settings** enabled. Without it Sunshine keeps the display's current resolution,
  which may not exist on the VDD.

## Setup

1. Provision a dedicated, disabled VDD device (once, elevated). The owner file must not exist yet, and its folder
   should only be writable by administrators:

   ```
   managed-vdd provision C:\path\to\MttVDD.inf C:\ProgramData\SunshineManagedVdd\owner.json
   ```

2. Find the VDD's display ID: run `managed-vdd activate <owner file>`, note the `display={...}` it prints, then run
   `managed-vdd deactivate <owner file>`.

3. Configure Sunshine (`sunshine.conf`), with the tool copied next to Sunshine:

   ```
   pre_display_prep_cmd = [{"do":"\"C:\\Program Files\\Sunshine\\tools\\managed-vdd.exe\" activate C:\\ProgramData\\SunshineManagedVdd\\owner.json","undo":"\"C:\\Program Files\\Sunshine\\tools\\managed-vdd.exe\" deactivate C:\\ProgramData\\SunshineManagedVdd\\owner.json","elevated":true}]
   output_name = {display ID from step 2}
   dd_configuration_option = ensure_only_display
   dd_resolution_option = auto
   dd_refresh_rate_option = auto
   dd_hdr_option = auto
   ```

## Commands

| Command | Elevated | Description |
|---|---|---|
| `probe` | no | Print the displays Windows currently reports. |
| `provision INF OWNER_FILE` | yes | Create and bind a new disabled VDD device and write its owner file. |
| `inspect OWNER_FILE` | yes | Show whether the device is disabled, its active targets and whether a checkpoint exists. |
| `activate OWNER_FILE [WIDTH HEIGHT FPS]` | yes | Do command. The mode defaults to `SUNSHINE_CLIENT_WIDTH`/`HEIGHT`/`FPS`. |
| `deactivate OWNER_FILE` | yes | Undo command. Safe to repeat. |
| `recover OWNER_FILE SERVICE ABSOLUTE_STATE_FILE` | yes | Recover an abandoned host transaction from the interactive console, coordinating with the Sunshine service. |

## Recovery after a crash or reboot

Deploy this version of `managed-vdd.exe` to Sunshine's `tools` directory. It records the launching
`sunshine.exe` process ID **and creation time** before display mutation. The launching process must
belong to that installation; manually activated or older checkpoints have no trusted host identity
and are not automatically recovered. Finish an existing stream with the old helper before upgrading.

From an elevated PowerShell session, run the installer in this source directory:

```powershell
.\install-recovery.ps1 -OwnerFile C:\ProgramData\SunshineManagedVddPoc\owner.json
```

The installer checks that the owned VDD is disabled and no checkpoint remains. It installs a hidden
recovery task for the current user, triggered at logon and every minute while logged on. Use
`-ValidateOnly` to validate and print the proposed task without registering it. Custom installations
can supply `-SunshineDirectory` and `-ServiceName`; the executable must be under Program Files and
the service must point to that installation's `tools\sunshinesvc.exe`. The state path is the standard
`config\display_device.state`; custom configuration directories require a correspondingly configured
recovery invocation instead of this installer.

Recovery holds the same cross-process lock as activation and deactivation. It:

1. Leaves live owners, unknown identities, and non-console sessions untouched.
2. Stops the configured service and waits for Sunshine processes to exit before writing display state.
3. Restores the baseline, or tries one attached physical HDMI/DP/DVI/internal output at a time if the
   old layout cannot be restored. Physical rescue intentionally does not promise the old layout.
4. Disables the owned VDD and verifies that its active targets disappeared.
5. Archives the baseline as `*.baseline.json.recovered.json` and retires Sunshine's stale display
   persistence as `display_device.state.managed-vdd-recovered`, then clears the checkpoint.
6. Restarts the service only if recovery stopped it. A durable `*.service.json` receipt survives helper
   termination and lets a subsequent invocation retry a failed restart, even after the checkpoint cleared.

A new Sunshine process cannot reuse an older process's checkpoint through Do/Undo: it must wait for
coordinated recovery. This prevents the normal launch path from racing the recovery task or replaying
Sunshine's obsolete baseline. Do not run a second Sunshine instance on the same desktop.

**Limits:** this is console **logon** recovery, not a guarantee of a working physical login screen
before sign-in. It detects process exit, not a hung-but-alive Sunshine process. Legacy/manual journals
require explicit administrator recovery with Sunshine stopped. An inaccessible or missing owned
device is not silently replaced. The task may wait up to one minute to retry. It has a two-minute
execution limit; a killed recovery retains its journal/service receipt. Failed exact restoration can
already have changed part of the desktop before physical rescue is attempted.

To remove the scheduled recovery (without changing the driver), run:

```powershell
Unregister-ScheduledTask -TaskName 'Sunshine Managed VDD Recovery' -Confirm:$false
```

The recovery flow, including the login-screen limitation, must be tested on the target machine before
claiming reboot recovery is validated. Tests cover lifecycle fault injection and real Windows process
identity/exit checks without toggling the actual display or stopping services.

## Logs and recovery

Sunshine does not keep the output of prep commands, so `activate` and `deactivate` append every step to
`managed-vdd.log` next to the owner file (rotated at 1 MiB), including messages from libdisplaydevice.

Before enabling the VDD, `activate` records the current displays in `OWNER_FILE.baseline.json`. `deactivate`
restores that baseline before disabling the VDD, so it is also the recovery command:

- With scheduled recovery installed, an exited host's checkpoint is recovered independently of the next stream.
  Without it, a new host refuses that checkpoint until coordinated/manual recovery completes.
- If the baseline cannot be restored by normal Undo, the VDD and the checkpoint stay, the command fails and
  Sunshine rejects further streams until the problem is fixed and `deactivate` succeeds.
  Some display settings may already have been restored before another restoration step fails.
- With no monitor connected (for example an unplugged DisplayPort cable), the baseline is headless: restoring it
  just disables the VDD. Windows can keep a path for an unplugged monitor; it is treated as headless only when its
  target name reports no monitor device path. A baseline is only recorded when every connected display could be
  identified; otherwise the tool retries briefly and then refuses the stream.

## Tested

On one Windows 11 host with a single DisplayPort monitor and Moonlight on iOS: connect, disconnect, quick
reconnect, monitor in standby, monitor unplugged (leftover path and no path), monitor plugged back in during a
stream, restarting Sunshine during a stream, and recovery from a skipped Undo.

Not tested on real hardware: multiple monitors, MST hubs, HDR clients, driver or Windows upgrades. The baseline
checks for these cases are covered by unit tests only.

## Building

From the Sunshine checkout, in the MSYS2 UCRT64 environment:

```
cmake -S tools/managed-vdd -B cmake-build-managed-vdd -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-managed-vdd
cmake-build-managed-vdd/tests/test_managed_vdd.exe
```

Required submodules: `third-party/libdisplaydevice` and `third-party/lizardbyte-common` (for googletest).
The tool is linked statically; the tests need `C:\msys64\ucrt64\bin` on `PATH`.
