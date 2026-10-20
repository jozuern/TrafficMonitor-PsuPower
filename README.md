# TrafficMonitor PSU Power

A [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) plugin that shows your PC's estimated power draw from the wall in the taskbar.

```
↑: 1.72 MB/s   PSU
↓: 4.87 MB/s   163 W
```

Hovering over the item shows the breakdown: CPU, GPU, rest of the system and the PSU efficiency used.

It runs inside TrafficMonitor, so there is no extra process. Once per TrafficMonitor refresh it reads two values that HWiNFO64 already publishes to the registry. No threads, no timers, no driver.

## How the number is calculated

Most PSUs have no sensor, so the plugin estimates:

```
PSU output = CPU Package Power × (1 + CpuVrmLoss) + GPU Power + OtherWatts
wall draw  = PSU output ÷ efficiency(PSU output)
```

- **CPU Package Power** and **GPU Power** come from HWiNFO64.
- **CpuVrmLoss** adds the motherboard's voltage regulator losses, which package power does not include (typically 5–8%).
- **OtherWatts** covers everything else: motherboard, RAM, drives, fans, pump, USB devices.
- **Efficiency** is interpolated from your PSU's efficiency curve, since efficiency changes a lot with load.

Expect it to be within roughly ±10–15 W of a wall meter once the values match your hardware. With a wall meter you can tune it much closer.

## Requirements

- Windows, 64-bit TrafficMonitor (tested with 1.86, full and Lite)
- HWiNFO64 running in sensors mode

## Setup

1. Download `PsuPower.dll` from [Releases](../../releases) and put it in the `plugins` folder next to `TrafficMonitor.exe` (create the folder if it doesn't exist).
2. In HWiNFO64's sensor window, right-click **CPU Package Power** → *Report value in Gadget*. Do the same for **GPU Power**.
3. Restart TrafficMonitor, right-click the taskbar widget → *Display Settings*, and tick **PSU Power**.

Recommended HWiNFO64 settings, so the values keep coming after a reboot: *Auto Start*, *Show Sensors on Startup*, *Minimize Sensors on Startup*, *Minimize Sensors instead of closing*.

If HWiNFO64 isn't running, or its values stop changing for about 10 seconds, the widget shows `-- W`.

## Configuration

On first load the plugin creates `PsuPower.ini` next to the DLL. Restart TrafficMonitor after editing it.

```ini
[PsuPower]
OtherWatts=35
CpuVrmLoss=0.07
EfficiencyCurve=60:0.811,120:0.879,300:0.912,600:0.899
CpuLabel=CPU Package Power
GpuLabel=GPU Power
```

| Key | Meaning |
|---|---|
| `OtherWatts` | Power of everything except CPU and GPU, in watts (DC). |
| `CpuVrmLoss` | VRM loss as a fraction of CPU package power. |
| `EfficiencyCurve` | `load:efficiency` points, load in watts of PSU output, up to 8 points in ascending order. |
| `CpuLabel`, `GpuLabel` | The exact sensor names shown in HWiNFO64. On AMD GPUs this is usually `GPU ASIC Power` or `Total Board Power`. |

The defaults are for the system this was written on: i7-9700K, RTX 2070, Z390 board, 2×16 GB DDR4 RGB, two SSDs, one 3.5" HDD, a 280 mm AIO, and a be quiet! Pure Power 11 600 W on 230 V.

### Filling in your own values

**EfficiencyCurve:** take the 10%, 20%, 50% and 100% load values from your PSU's datasheet or a [Cybenetics](https://www.cybenetics.com/) report, for your mains voltage (115 V or 230 V). For a 750 W unit, 10% is 75 W, so the first point is `75:0.xx`.

**OtherWatts:** add up rough figures for your parts:

| Part | Watts |
|---|---|
| Motherboard (chipset, LAN, audio) | 10–15 |
| RAM | 1.5–3 per stick, +1 with RGB |
| NVMe / SATA SSD (mostly idle) | 0.5–2 each |
| 3.5" HDD (spinning) | 4–6 each |
| Case fan | 1–2 each |
| AIO pump | 3–5 |
| USB keyboard, mouse, dongles | 0.5–3 each |

Monitors have their own power supply and don't count.

## Building

Needs Visual Studio 2019 or newer with the *Desktop development with C++* workload.

```
build.bat
```

Output: `bin\PsuPower.dll`. It links the CRT statically, so it has no runtime dependencies.

## License

MIT, see [LICENSE](LICENSE). `third_party/TrafficMonitor/PluginInterface.h` comes from TrafficMonitor (tag V1.86) and is under its own license, included in that folder.
