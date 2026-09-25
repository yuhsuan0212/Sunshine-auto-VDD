# Managed VDD prototype

This is an experimental Windows integration. The standalone backend can be built independently, and the Sunshine integration has been built and tested on a Windows host.

## Current scope

The first provider enables/disables one explicitly provisioned device instance. It does not remove device instances or driver packages at disconnect. The VDD source is unchanged, so the official signed package can be used.

`managed_vdd_owner_file` is an experimental configuration key. Its default is empty (disabled). When set, the prototype uses the owned display, makes it primary, extends the existing topology, and requests the first client's resolution, refresh rate and HDR state. Before enabling VDD it adds the requested mode to `vdd_settings.xml` when needed, then checks that Windows advertises and accepts it. The original XML is retained in the recovery journal and restored when the last stream ends, unless another program edited the file meanwhile. Unsupported modes fail the launch and invoke recovery. Additional clients share that display. The normal Sunshine `dd_*` configuration/revert scheduler is bypassed for managed sessions to avoid conflicting changes; this prototype does not implement its mode remapping or ensure-only-display options.

The active topology, primary display, modes, HDR and positions are saved **before activation**. A headless baseline is allowed only when display enumeration succeeds with zero active targets. Disabling the owned device returns to headless; zero paths are not passed to SetDisplayConfig. Newly connected physical displays in the headless case are not forcibly disabled. DPI scaling and color profiles are not changed or explicitly restored.

## Ownership and recovery

Provisioning creates a new disabled root device from the explicitly supplied INF. Driver selection/install is restricted to that new instance, rather than updating every device matching the hardware ID. The owner file stores its instance ID and a random owner token; the same token is stored on the devnode. Every mutation verifies both, plus the `Root\MttVDD` hardware ID. A named mutex excludes simultaneous controllers of that instance. An existing user-managed VDD is not adopted automatically.

The baseline journal uses a flushed temporary file and atomic rename. Failed restoration, ambiguous enumeration, a missing marker, or reboot-required status retains the journal and prevents another activation. An ordinary capture shutdown joins workers before releasing its lease. Pending RTSP expiry releases only a pending lease; it cannot remove an active stream's display. Cancellation, failed launch, startup recovery and shutdown use the same lifecycle.

Modes are generated for the first client's integer dimensions and refresh rate. Requests outside 320–8192 pixels wide, 200–4320 pixels high or 24–240 Hz are rejected before activation. A mode inside those bounds can still be rejected by Windows or the video encoder. Existing global refresh rates are reused; a missing rate is added only to the requested resolution. The VDD XML is modified before PnP activation, then restored after the owned device is disabled. A user's concurrent XML edit is preserved.

The manager cannot clean up at the instant its process is killed. It retains a journal for the next startup or explicit `recover`. It never forcibly removes a driver or automatically reboots Windows.

## Build the independent prototype on Windows

From the Sunshine checkout, using the repository-required MSYS2 UCRT64 environment:

```text
C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c "cmake -S tools/managed-vdd-poc -B cmake-build-vdd-poc -G Ninja && cmake --build cmake-build-vdd-poc"
```

Required source submodules: `third-party/libdisplaydevice` and `third-party/lizardbyte-common/third-party/googletest`. CMake fetches the display library's Boost and JSON dependencies when they are not installed. `-DVDD_BUILD_WINDOWS_PROVIDER=OFF` builds only the portable lifecycle tests. Tests are at `cmake-build-vdd-poc/tests/test_sunshine.exe`.

Build dependencies are resolved from the MSYS2 UCRT64 environment or pinned upstream sources.

## Explicit operations

```text
managed-vdd-poc probe
managed-vdd-poc provision SIGNED_MttVDD.inf OWNER.json
managed-vdd-poc inspect OWNER.json
managed-vdd-poc cycle OWNER.json 1
managed-vdd-poc cycle-mode OWNER.json 3
managed-vdd-poc recover OWNER.json
```

`probe` is read-only. `provision`, `cycle`, `cycle-mode` and `recover` require elevation. `cycle-mode` additionally makes the VDD primary at the specified size and FPS (default 1920x1080/60 Hz SDR) before restoring the baseline. The state directory must exist and should be writable only by Administrators/SYSTEM when used by the service. The executable currently uses the UCRT64 runtime DLLs, so `C:\msys64\ucrt64\bin` must be on PATH.

Provisioning refuses an existing ownership file. If provisioning is interrupted, inspect that exact instance and recover it; do not keep rerunning provisioning to generate more devices. The standalone cycle exercises enable/enumerate/restore/disable and reports display identity and activation latency; it does not stream video or exercise the Sunshine RTSP integration.

## Validation and remaining gates

The portable lifecycle and Windows backend tests pass, including fault injection, concurrent timer/capture startup and simulated repeated reconnects. A full Sunshine build passed the relevant test selection. On one Windows host, the owned signed driver completed several real PnP cycles, accepted both a nonstandard resolution and a refresh rate absent from its original configuration, and returned the physical layout and XML to their prior state. A Moonlight client then streamed at a custom resolution and 60 FPS; after disconnect, VDD was disabled and the temporary mode and recovery checkpoint were removed.

These results cover the tested requests, not every possible display mode. Resume, cancel, timeout, headless operation and forced-crash recovery still need end-to-end testing. GPU driver updates and Windows upgrades have not been tested. Follow the upstream VDD project's driver-update instructions.

Create/remove is a later provider, not part of this first implementation.
