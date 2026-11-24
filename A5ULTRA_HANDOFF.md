# A5 Ultra – Übergabe für eine neue Sitzung (Stand 24.11.2026)

Gerät: Samsung Galaxy A5 2017, **SM-A520F**, Exynos 7880 (8× Cortex-A53, Mali-T830 MP3), One UI 2 / Android 10,
Bootloader A520FXXUGCTKA, Magisk-Root. Drittgerät, Ziel: maximal übertakten, aber stabil.
Windows-PC, adb liegt in `C:\Users\schme\Downloads\platform-tools-latest-windows\platform-tools`
(PowerShell: `.\adb shell "su -c '...'"`, innen nur einfache Anführungszeichen; `$` in PowerShell als `` `$ `` escapen).

## Repo / Code
- Repo `johannaschmelf-source/android_kernel_samsung_universal7880`, Branch **`claude/upbeat-brahmagupta-u4v5tf`**
  (basiert auf `rise` @ `a9d4fd42`).
  - `470d9bd8` – kompletter v21-Quellcode (Patch `A5Ultra-v21.patch`)
  - `d3d16482` – `scripts/a5ultra/build.sh` mit Test-Overrides
- Bauen (Linux/WSL, GCC 4.9 aarch64-linux-android, z. B. LineageOS `android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9` Branch `lineage-19.0`):
  `NAME=v22-cpu2054 CPU_KHZ=2054000 TOP_EXTRA_UV=75000 scripts/a5ultra/build.sh <prefix>/bin/aarch64-linux-android- <boot_v21_backup.img>`
  Overrides: `CPU_KHZ CPU_UV_DELTA TOP_EXTRA_UV GPU_KHZ GPU_UV_MAX GPU_TOP_EXTRA_UV THERMAL_SHIFT`.
- **boot.img immer mit `scripts/a5ultra/swapkernel.py` aus `boot_v21_backup.img` (Magisk-Ramdisk) bauen.**
  AIK/leere Ramdisk → Bootschleife. swapkernel.py wurde geprüft: Roundtrip mit Original-Kernel ergibt Byte-gleiches Image.
- Sicherung: `boot_v21_backup.img` (32 MB, ganze BOOT-Partition) liegt auf dem PC im platform-tools-Ordner.
  Wiederherstellen per TWRP (Install Image → Boot) oder `dd` nach `/dev/block/platform/13540000.dwmmc0/by-name/BOOT`.

## Wie die Übertaktung funktioniert
- Takte/Spannungen kommen aus der **ECT-Tabelle des Bootloaders**, nicht aus dem DTS (`CONFIG_ECT=y`).
- `drivers/soc/samsung/ect_oc.c` patcht die ECT im RAM: Samsung-Stufen freischalten, L0 auf neuen Takt umschreiben,
  **neuer PLL-Eintrag wird angehängt** (Samsungs 2002-PLL-Eintrag umschreiben → Bootschleife), ASV-Spannung +delta.
- `CONFIG_ECT_OC_BOOT_CAP`: Kernel startet mit 1872 (CPU) / 1034 (GPU); OC-Stufe erst nach Hochsetzen in HKTweaks/sysfs.
- Spannungsgrenzen im Patch: CPU 1,30 V (`CL*_MAX_VOLT`), GPU 1,25 V (`GPU_MAX_VOLT`).
- sysfs: `/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster{0,1}_volt_table` (Schreiben: immer **alle 17 Werte**,
  Kurzform „freq volt“ geht für die oberste Stufe nicht), `/sys/devices/11400000.mali/volt_table` (Kurzform `1352 1112500` geht),
  `time_in_state`, `dvfs_max_lock`, `/sys/kernel/debug/ect/{dvfs_table,asv_table,pll_list}`.

## Messwerte des Chips
- ASV-Gruppe ca. 3–4 (eher durchschnittlich). CPU: 1872 → 1,1125 V, 2002 → 1,20625 V (steiler Anstieg).
- CPU 2002 stabil (Volllast 110 s, 85 °C, keine Drosselung). Undervolt 2002 @ 1,1875 V nur auf cluster1 getestet → stabil.
- GPU 1352 stabil; 1352 @ 1,1125 V → 19.323 GPU-Punkte, 84 % Zeit auf 1352 (vs. 77 % bei 1,1375 V).
- AnTuTu v12 bisher ca. 250–254k (CPU ~103k, GPU 17–19k, MEM 81–85k, UX ~49k). Bestwert 316.251 mit v12-Kernel (MEM 138.960!).

## Bereits getestet (aus früherem Chat)
- CPU 2184 → Neustart nach 7 s. CPU 2054/2106 → AnTuTu-Absturz bei 48 % (CPU-Test), **aber mit alter 1,2-V-Grenze** → neu testen.
- GPU 1400 → `GPU fault 0x4002` / `JOB_READ_FAULT` / Soft-Reset (auch bei 1,175 V).
- RAM > 1539 (1716/1794/1898) instabil. In der ECT hat Samsung 1716/1794 **mit gleicher Spannung wie 1539 (0,8 V)** eingetragen
  → mit mehr MIF-Spannung evtl. machbar. MIF-Unlock-Code fehlt in v21 (`ECT_OC_MIF_*` Kconfig existiert, wird nicht ausgewertet).
- Fester RAM-Takt per `min_freq` + `swapoff` vor AnTuTu war nicht die Ursache der GPU-Fehler. ZRAM an senkt MEM-Wert.
- Samsung-GPU-Governor parkte auf 845 MHz; v20/v21 schalten ab 85 % Last hoch. Taktsteuerung `highspeed_clock 1352/80` brachte nichts.

## Gebaute, noch NICHT getestete Kernel (an den User geschickt)
| Datei | CPU | GPU |
|---|---|---|
| `boot_A5Ultra_v22-cpu2054.img` | 2054 @ ~1,28 V (`TOP_EXTRA_UV=75000`) | 1352 |
| `boot_A5Ultra_v22-gpu1400.img` | 2002 | 1400 @ ~1,21 V (`GPU_TOP_EXTRA_UV=150000`, `GPU_UV_MAX=1250000`) |
| `boot_A5Ultra_v21-gpu1352.img` | 2002 | 1352 (= aktueller Kernel, Rückfall) |

## Nächste Schritte
1. **v22-cpu2054** flashen → `dmesg | grep ECT-OC`, HKTweaks CPU-Max 2054 (beide Cluster), GPU 1352, Governor Interactive,
   Spannungen 0 → AnTuTu. Absturz bei 48 % → 2054-Stufe auf 1300000 µV (alle 17 Werte schreiben) → nochmal. Sonst 2002 = Maximum.
2. **v22-gpu1400** flashen → GPU-Max 1400, CPU 2002 → AnTuTu → `dmesg | grep -i mali | grep -i -e fault -e reset`.
   Fehler → `echo 1400 1250000 > /sys/devices/11400000.mali/volt_table` → nochmal. Weiter Fehler → 1352 bleibt Maximum.
3. Danach: **RAM-Übertaktung** neu implementieren (dvfs_mif-Stufen 1716/1794 freischalten, MIF-ASV-Spannung anheben,
   `freq_info` in `devfreq_mif` des DTS anheben, DRAM-Timing-Zeilen der ECT prüfen). Risiko Datenkorruption → vorher Backup.
4. Endwerte (CPU-/GPU-Undervolt) ins Magisk-Tweaks-Modul (A5Ultra-Tweaks v5.1, Befehl `a5u`) übernehmen.

## Nicht machbar (geprüft)
- Display-OC: S6E3FA3 läuft im Command-Mode, Bildrate erzeugt das Panel selbst; `refresh 60→80` im DTS ändert nichts.
- Neuere Treiber: Mali r28p0 muss zu Samsungs GPU-Userspace-Blobs passen; Kernel 3.18 nicht upgradebar ohne Vendor-Treiber.
- Bootanimation-Text: One UI nutzt Samsungs QMG-Format (`/system/media/bootsamsung*.qmg`), selbst nicht erzeugbar;
  `bootanimation.zip` wird ignoriert. Machbar ist nur das Bootlogo (up_param, macht das Magisk-Modul in `.github/workflows/build.yml`).

## Hinweise
- `.github/workflows/build.yml` schreibt per `dd` in `up_param` (Bootlogo) – vorher sichern.
- Remote Control (Cloud-Sitzung → Windows-PC) funktionierte nicht: Ordner `platform-tools` auf „Maxis-PC“ wurde als „not_available“ gemeldet.
