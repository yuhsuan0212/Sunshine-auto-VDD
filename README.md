<div align="center">
  <img src="sunshine.svg" alt="Sunshine icon" width="256" />
  <h1>Sunshine Auto VDD</h1>
  <p>On-demand virtual displays for Moonlight on Windows</p>
</div>

This is an experimental community fork of [LizardByte Sunshine](https://github.com/LizardByte/Sunshine). It adds automatic management of a separately installed, signed [Virtual Display Driver (VDD)](https://github.com/VirtualDrivers/Virtual-Display-Driver). The general Sunshine streaming features and platforms below come from the upstream project; this fork's VDD integration is for Windows hosts.

## What this fork adds

When the first Moonlight client requests a stream, Sunshine saves the current display layout, adds the client's resolution and integer refresh rate to the VDD settings if needed, enables its own VDD device, waits for Windows to enumerate the mode, and makes the virtual display the only active Windows display. This brings application windows into the streamed desktop instead of leaving them on a physical monitor that Moonlight cannot see. Additional clients share it. After the last stream ends, Sunshine restores the original layout, disables the VDD device, and restores the VDD settings file if no other program changed it.

The first client's request can use a custom size or refresh rate. The prototype accepts requests from 320×200 to 8192×4320 pixels and 24–240 Hz; Windows and the encoder may still reject a particular combination. A rejected launch restores the display state. The VDD package remains installed while idle, so follow [VDD's guidance](https://github.com/VirtualDrivers/Virtual-Display-Driver#%EF%B8%8F-important-gpuchipset-driver-updates) before major GPU or chipset driver updates.

## Set up on Windows

1. Build and install this fork using the upstream [Sunshine build instructions](https://docs.lizardbyte.dev/projects/sunshine/latest/) and the repository's MSYS2 UCRT64 setup. Official upstream Sunshine releases do not contain these changes.
2. Download the signed AMD64 VDD driver package from [VirtualDrivers releases](https://github.com/VirtualDrivers/Virtual-Display-Driver/releases). Build `tools/managed-vdd-poc` and, as administrator, run `managed-vdd-poc provision <path-to-MttVDD.inf> <owner-file>`. This creates one disabled device owned by Sunshine. Keep the owner file in an administrator-controlled directory. Do not point Sunshine at an unrelated VDD device.
3. Set `managed_vdd_owner_file = <owner-file>` in Sunshine's `sunshine.conf`, then restart its service. An empty value disables this feature. A successful startup logs `Managed VDD ready`.
4. Choose a resolution and FPS in Moonlight. On the first stream, Sunshine requests the corresponding VDD resolution and Hz. The [managed VDD development notes](docs/managed_vdd_poc.md) cover diagnostics, recovery and current limits.

This feature has been built and tested on one Windows host, including two custom VDD modes and one Moonlight stream. Resume, cancel, timeout, headless recovery and forced-crash cases still need end-to-end testing.

## Upstream Sunshine features

Sunshine is a self-hosted game stream host for Moonlight, with hardware encoding on supported AMD, Intel and NVIDIA GPUs, software encoding, client pairing and a web interface. Upstream documentation is available at [docs.lizardbyte.dev](https://docs.lizardbyte.dev/projects/sunshine).

## 🎮 Feature Compatibility

<table>
    <caption id="gamepad_emulation">Gamepad Emulation</caption>
    <tr>
        <th>Feature</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td colspan="5" align="center">
        What type of gamepads can be emulated on the host.<br>
        Clients may support other gamepads.
        </td>
    </tr>
    <tr>
        <td>Generic</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>DualShock / DS4 (PlayStation 4)</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>DualSense / DS5 (PlayStation 5)</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Nintendo Switch Pro</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Xbox 360</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Xbox One</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Xbox Series</td>
        <td>🟡<sup>1</sup></td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
    </tr>
</table>

> [!NOTE]
> <sup>1</sup> Missing motion, touchpad input, battery state, RGB LEDs, adaptive triggers, and raw HID output reports.

<table>
    <caption id="encoding_api">Encoding API</caption>
    <tr>
        <th>Encoding API</th>
        <th>GPU Vendor</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td>AMF</td>
        <td>AMD</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>Media Foundation</td>
        <td>Qualcomm</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>NVENC</td>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>QuickSync</td>
        <td>Intel</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td rowspan="3">VAAPI</td>
        <td>AMD</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td rowspan="2">Video Toolbox</td>
        <td>Apple</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td rowspan="3">Vulkan Video</td>
        <td>AMD</td>
        <td>🟡</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Intel</td>
        <td>🟡</td>
        <td>🟡</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NVIDIA</td>
        <td>➖</td>
        <td>🟡</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Software</td>
        <td>Any</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
</table>

<table>
    <caption id="screen_capture">Screen Capture</caption>
    <tr>
        <th>Capture Method</th>
        <th>FreeBSD</th>
        <th>Linux</th>
        <th>macOS</th>
        <th>Windows</th>
    </tr>
    <tr>
        <td>DXGI Desktop Duplication</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>KMS/DRM</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>NvFBC (X11 only)</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>ScreenCaptureKit</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Wayland (wlroots)</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>Windows.Graphics.Capture</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>🟡</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ Portable</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>&nbsp;&nbsp;↳ Service</td>
        <td>➖</td>
        <td>➖</td>
        <td>➖</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>X11</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>XDG Desktop Portal</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
    <tr>
        <td>KWin Screencast</td>
        <td>✅</td>
        <td>✅</td>
        <td>➖</td>
        <td>➖</td>
    </tr>
</table>

<table>
    <caption id="capture_encoding_compat">Capture → Encoding Compatibility (Linux/FreeBSD)</caption>
    <tr>
        <th>Capture Method</th>
        <th>VAAPI</th>
        <th>Vulkan Video</th>
        <th>NVENC (CUDA)</th>
        <th>Software</th>
    </tr>
    <tr>
        <td>KMS/DRM</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>NvFBC</td>
        <td>❌</td>
        <td>❌</td>
        <td>✅</td>
        <td>❌</td>
    </tr>
    <tr>
        <td>Wayland (wlroots)</td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>X11</td>
        <td>✅</td>
        <td>❌</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>XDG Desktop Portal</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
    <tr>
        <td>KWin Screencast</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
        <td>✅</td>
    </tr>
</table>

**Legend:** ✅ Supported | 🟡 Partial Support | ❌ Not Yet Supported | ➖ Not Applicable

## 🖥️ System Requirements

> [!WARNING]
> These tables are a work in progress. Do not purchase hardware based on this information.

<table>
    <caption id="minimum_requirements">Minimum Requirements</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: VCE 1.0 or higher, see: <a href="https://github.com/obsproject/obs-amd-encoder/wiki/Hardware-Support">obs-amd hardware support</a></td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;FreeBSD/Linux: VAAPI-compatible, see: <a href="https://www.intel.com/content/www/us/en/developer/articles/technical/linuxmedia-vaapi.html">VAAPI hardware support</a><br>
            &nbsp;&nbsp;Windows: Skylake or newer with QuickSync encoding support
        </td>
    </tr>
    <tr>
        <td>Nvidia: NVENC enabled cards, see: <a href="https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new">nvenc support matrix</a></td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 3 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i3 or higher</td>
    </tr>
    <tr>
        <td>RAM</td>
        <td>4GB or more</td>
    </tr>
    <tr>
        <td rowspan="6">OS</td>
        <td>FreeBSD: 15.1+</td>
    </tr>
    <tr>
        <td>Linux/Debian: 13+ (trixie)</td>
    </tr>
    <tr>
        <td>Linux/Fedora: 43+</td>
    </tr>
    <tr>
        <td>Linux/Ubuntu: 22.04+ (jammy)</td>
    </tr>
    <tr>
        <td>macOS: 14.2+</td>
    </tr>
    <tr>
        <td>Windows: 11+</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: 5GHz, 802.11ac</td>
    </tr>
    <tr>
        <td>Client: 5GHz, 802.11ac</td>
    </tr>
</table>

<table>
    <caption id="4k_suggestions">4k Suggestions</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.1 or higher</td>
    </tr>
    <tr>
        <td>
            Intel:<br>
            &nbsp;&nbsp;FreeBSD/Linux: HD Graphics 510 or higher<br>
            &nbsp;&nbsp;Windows: Skylake or newer with QuickSync encoding support
        </td>
    </tr>
    <tr>
        <td>
            Nvidia:<br>
            &nbsp;&nbsp;FreeBSD/Linux: GeForce RTX 2000 series or higher<br>
            &nbsp;&nbsp;Windows: Geforce GTX 1080 or higher
        </td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i5 or higher</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: CAT5e ethernet or better</td>
    </tr>
    <tr>
        <td>Client: CAT5e ethernet or better</td>
    </tr>
</table>

<table>
    <caption id="hdr_suggestions">HDR Suggestions</caption>
    <tr>
        <th>Component</th>
        <th>Requirement</th>
    </tr>
    <tr>
        <td rowspan="3">GPU</td>
        <td>AMD: Video Coding Engine 3.4 or higher</td>
    </tr>
    <tr>
        <td>Intel: HD Graphics 730 or higher</td>
    </tr>
    <tr>
        <td>Nvidia: Pascal-based GPU (GTX 10-series) or higher</td>
    </tr>
    <tr>
        <td rowspan="2">CPU</td>
        <td>AMD: Ryzen 5 or higher</td>
    </tr>
    <tr>
        <td>Intel: Core i5 or higher</td>
    </tr>
    <tr>
        <td rowspan="2">Network</td>
        <td>Host: CAT5e ethernet or better</td>
    </tr>
    <tr>
        <td>Client: CAT5e ethernet or better</td>
    </tr>
</table>

## ❓ Support

Our support methods are listed in our [LizardByte Docs](https://docs.lizardbyte.dev/latest/about/support.html).

## 💲 Sponsors and Supporters

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/sponsors.svg' alt="Sponsors"/>
</p>

## 👥 Contributors

Thank you to all the contributors who have helped make Sunshine better!

### GitHub

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/github.Sunshine.svg' alt="GitHub contributors"/>
</p>

### CrowdIn

<p align="center">
  <img src='https://cdn.jsdelivr.net/gh/LizardByte/contributors@dist/crowdin.606145.svg' alt="CrowdIn contributors"/>
</p>
