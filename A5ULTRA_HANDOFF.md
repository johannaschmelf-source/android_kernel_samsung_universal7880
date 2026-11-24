# Galaxy A5 Ultra – Gesamtstand (Stand 24.11.2026)

Alles, was aus den bisherigen Chats bekannt ist, plus der Plan. Für eine neue (lokale) Sitzung gedacht.

---

## 1. Ziel

- Samsung Galaxy A5 2017 (**SM-A520F**) zum „Flagship-Killer“ machen: CPU, GPU, RAM so hoch wie möglich, **ohne Abstürze**.
- Das Handy ist ein **Drittgerät**: Risiko ist okay, Bootschleifen sollen aber vermeidbar/rückgängig bleiben.
- Wunsch: Branding „Samsung Galaxy A5 Ultra“ (Bootlogo/Bootanimation, Modellname).
- Wunsch: auch Display-Übertaktung und „neueste Treiber“ (beides geprüft → nicht machbar, siehe Abschnitt 10).

## 2. Gerät und Umgebung

| | |
|---|---|
| Modell | SM-A520F (EUR_OPEN), hw_rev 8 |
| SoC | Exynos 7880: 8× Cortex-A53 in 2 Clustern (cl0 = CPU0–3, cl1 = CPU4–7), Mali-T830 MP3 (`Mali-T83x 3 cores r0p1`) |
| PMIC | S2MPU05: BUCK1 = CPU (max 1,3 V), BUCK2 = MIF/RAM (max 1,3 V), BUCK3 = `vdd_g3d` GPU (max 1,3 V) |
| Display | S6E3FA3 AMOLED 1080×1920, MIPI **Command-Mode**, 60 Hz |
| Firmware | One UI 2 / Android 10, Bootloader **A520FXXUGCTKA** |
| Root | Magisk; SELinux permissive (Kernel-cmdline) |
| Kernel aktuell | `3.18.140-riseKernel-10.0-v1.11-A5Ultra-v21-gpu1352` |
| Tuning-App | **HKTweaks** (CPU-/GPU-Max, Spannungen, Governor) |
| PC | Windows, „Maxis-PC“, adb in `C:\Users\schme\Downloads\platform-tools-latest-windows\platform-tools` |

### adb unter Windows (wichtig, sonst kaputte Befehle)
- In PowerShell: `.\adb shell "su -c '...'"` – außen doppelte, innen einfache Anführungszeichen.
- `|`, `;`, `2>/dev/null` gehören **innerhalb** der Anführungszeichen, sonst wertet Windows sie aus.
- `$` muss in PowerShell als `` `$ `` geschrieben werden (z. B. `` `$(cat …) ``), Backslash-Escapes (`\$f`) funktionieren nicht.
- Remote Control (Cloud-Sitzung steuert den PC) hat **nicht** funktioniert: Ordner `platform-tools` wurde als „not_available“ gemeldet,
  obwohl die App aktuell war. Deshalb: lokale Sitzung auf dem PC nutzen.

## 3. Code, Repo, Bauen

- Repo: `johannaschmelf-source/android_kernel_samsung_universal7880`
- Branch mit allem: **`claude/upbeat-brahmagupta-u4v5tf`** (basiert auf `rise` @ `a9d4fd42`, 15.11.)
  - `470d9bd8` – kompletter v21-Quellcode (aus `A5Ultra-v21.patch`, 21 Dateien)
  - `d3d16482` – `scripts/a5ultra/build.sh` mit Test-Overrides
  - `f918b8a3` / Nachfolger – diese Datei
- v21 ist ursprünglich in einem Claude-Chat entstanden und war nie auf GitHub; Actions-Builds/Release `a5-ultra-latest` sind vom 15.11. und älter.
- Bauen (nur Linux/WSL): Toolchain GCC 4.9 `aarch64-linux-android-` (z. B. LineageOS
  `android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9`, Branch `lineage-19.0`).
  ```
  NAME=v22-cpu2054 CPU_KHZ=2054000 TOP_EXTRA_UV=75000 \
    scripts/a5ultra/build.sh <toolchain>/bin/aarch64-linux-android- <boot_v21_backup.img>
  ```
  Overrides: `CPU_KHZ CPU_UV_DELTA TOP_EXTRA_UV GPU_KHZ GPU_UV_MAX GPU_TOP_EXTRA_UV THERMAL_SHIFT`. Ohne Overrides = v21.
- **boot.img immer mit `scripts/a5ultra/swapkernel.py` aus `boot_v21_backup.img` erzeugen** (tauscht nur Kernel + dtb, behält Magisk-Ramdisk).
  Ein boot.img mit leerer Ramdisk (AIK) → Bootschleife. swapkernel.py geprüft: mit Original-Kernel ergibt es Byte-gleich das Original.
- `boot_oneui_10.0_a5 (1).img` im Repo ist ein altes Rise-Image **ohne Magisk** – nicht als Basis nehmen.
- `build_rise.sh` erzeugt `exynos7880-a5y17lte_common.dtsi` aus `..._lineage_oneui.dtsi`; im v21-Patch ist die Datei fest drin (identisch).

### Sicherung / Wiederherstellen
- Sicherung: `boot_v21_backup.img` (32 MB = ganze BOOT-Partition, Magisk-Ramdisk geprüft) liegt auf dem PC in `platform-tools`.
  Für Upload: als ZIP packen (Upload-Limit 30 MB): `Compress-Archive -Path .\boot_v21_backup.img -DestinationPath .\boot_v21_backup.zip`
- Flashen per adb:
  ```
  .\adb push <datei>.img /sdcard/
  .\adb shell "su -c 'dd if=/sdcard/<datei>.img of=/dev/block/platform/13540000.dwmmc0/by-name/BOOT && sync && reboot'"
  ```
- Oder TWRP: Install → Install Image → Boot.

## 4. Wie die Übertaktung technisch funktioniert

- Auf dem 7880 kommen Frequenzen, PLL-Teiler und Spannungen aus der **ECT-Tabelle des Bootloaders**, nicht aus dem DTS
  (`CONFIG_ECT=y`). Die cpufreq-Tabelle im DTS wird ignoriert bzw. bei Abweichung durch die CAL-Werte ersetzt.
- Früherer Irrtum: DTS-Einträge 2106/2210 MHz und `echo 2002000 > scaling_max_freq` im alten Modul hatten ohne ECT-Patch keine Wirkung.
- `drivers/soc/samsung/ect_oc.c` (v21) patcht die ECT im RAM direkt nach dem Parsen:
  - **Unlock:** von Samsung vorhandene, aber deaktivierte Stufen einschalten (z. B. CPU 2002, GPU 1086/1200) mit `+UV_DELTA`.
  - **Replace:** oberste Stufe L0 auf neuen Takt umschreiben; **neuer PLL-Eintrag wird angehängt** (Samsungs Original-PLL-Eintrag
    umschreiben → Bootschleife). Spannung L0 = Samsung-ASV + `UV_DELTA` + `TOP_EXTRA_UV`, gedeckelt durch `UV_MAX`.
  - RCC-, Minlock- und Thermal-Tabellen werden mitgezogen; max. +20 % über alter L0 erlaubt; bei Unstimmigkeit bleibt die Domain stock.
  - `THERMAL_SHIFT=10`: Drosselung startet 10 °C später (ab ca. 86 °C), Hardware-Trip unverändert.
- `CONFIG_ECT_OC_BOOT_CAP`: Kernel startet mit CPU 1872 / GPU 1034. Die OC-Stufe gibt es erst nach Hochsetzen
  (HKTweaks bzw. `scaling_max_freq` / GPU `max_clock`). Ein Absturz beim Testen → normaler Neustart, keine Bootschleife.
- Spannungsgrenzen in v21: CPU 1,30 V (`CL0/CL1_MAX_VOLT`), GPU 1,25 V (`GPU_MAX_VOLT`). Vor v18 war die CPU bei **1,20 V** gedeckelt.
- GPU-Steuerung v20/v21: hochschalten ab 85 % Last, runter erst unter 60 % und nach 3 Messungen (Samsung: erst ab 98 %, parkte auf 845).
- v21-Defconfig: `ECT_OC_CPU_KHZ=2002000`, `CPU_UV_DELTA=25000`, `CPU_UV_MAX=1300000`, `GPU_KHZ=1352000`, `GPU_UV_DELTA=37500`,
  `GPU_UV_MAX=1150000`, `GPU_TOP_EXTRA_UV=75000`, `TOP_EXTRA_UV=37500`, `THERMAL_SHIFT=10`, MIF-Unlock aus.
  Variante v21-gpu1300: `GPU_KHZ=1300000`, `GPU_UV_MAX=1125000`, `GPU_TOP_EXTRA_UV=37500`.
- Weitere Inhalte des v21-Patches: schnelleres Laden (Ladeströme im Battery-DTS angehoben), DT2W (`dt2w_enable`),
  Sound-Boost (`/sys/kernel/a5u_sound/{hp,spk,ep}_boost`), F2FS + NTFS, TCP westwood, `gpu_oc_max`-sysfs.

### Wichtige sysfs-Pfade
| Pfad | Zweck |
|---|---|
| `/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster{0,1}_volt_table` | CPU-Spannungen. **Immer alle 17 Werte in einer Zeile schreiben**; die Kurzform „freq volt“ trifft bei der obersten Stufe die falsche Stufe |
| `/sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster{0,1}_{max,min}_freq` | CPU-Grenzen |
| `/sys/power/cpufreq_table` | CPU-Frequenzliste |
| `/sys/devices/11400000.mali/volt_table` | GPU-Spannungen, Kurzform geht: `echo 1352 1112500 > …` |
| `/sys/devices/11400000.mali/{clock,time_in_state,dvfs_max_lock,dvfs_governor,utilization,highspeed_clock,highspeed_load}` | GPU-Status |
| `/sys/class/devfreq/17000010.devfreq_mif/{cur_freq,min_freq}` | RAM (MIF) |
| `/sys/power/cpuhotplug/{enabled,min_online_cpu}` | Samsung-Hotplug |
| `/sys/kernel/debug/ect/{dvfs_table,asv_table,pll_list,ap_thermal}` | ECT-Dump (debugfs ist schon gemountet) |
| `dmesg \| grep ECT-OC` | was ect_oc beim Boot gemacht hat |

## 5. Daten des Chips (aus ECT / Messungen)

- ASV-Tabelle Version 2/3, Gruppe ca. 3–4 (eher durchschnittlich).
- CPU-Spannungen (beide Cluster gleich): 2002 → 1,20625 V · 1872 → 1,1125 V · 1794 → 1,0875 V · 1690 → 1,04375 V · …
  Volle Liste (17 Werte, 2002→345):
  `1206250 1112500 1087500 1043750 1006250 968750 925000 887500 862500 818750 806250 768750 737500 718750 681250 662500 662500`
- Steiler Anstieg: +94 mV für +130 MHz (1872→2002) → Chip am oberen Ende.
- CPU-PLL: 26 MHz × M / P. 2002 = M 231, P 3. 2054 = M 237, 2106 = M 243 (sauber erzeugbar).
- GPU-Spannungen v21: 1352 → 1,1375 V · 1086 → 1,0625 V · 1034 → 0,99375 V · 962 → 0,95 V · 845 → 0,8875 V …
- MIF (RAM): Stufen 1794/1716 in ECT vorhanden aber **aus**; 1539 = aktiver Höchstwert.
  **Samsung trägt für 1794, 1716 und 1539 dieselbe Spannung ein (0,80 V bei guten Gruppen)** → mehr MIF-Spannung könnte helfen.
- Temperaturen: Leerlauf ~33 °C, 8 Kerne Volllast @ 2002: 72 → 85 °C in ~60 s, dann stabil bei 85 °C ohne Drosselung.

## 6. Versionsverlauf (soweit bekannt)

| Version | Inhalt / Ergebnis |
|---|---|
| alte Actions-Builds („BEAST“) | sed-Patches: GPU 1248, CPU-Stufe 2054, RAM 1794, Display 60→80 Hz (wirkungslos), Lade-Hacks |
| v3 | ZRAM 1,5 GB → MEM-Wert −13 % |
| v10 | – |
| **v12** | **Bestwert AnTuTu 316.251**: CPU 109.166, GPU 16.425, **MEM 138.960**, UX 51.700. Grenzen noch 1,20 V / 1,10 V, GPU hing meist auf 1034 |
| v16 | CPU 2054 ersetzt 2002 (kein 2002 wählbar), CPU-Grenze 1,20 V → AnTuTu-Absturz bei 48 %, +25 mV wirkungslos (gekappt) |
| v18 | Grenze CPU 1,30 V, GPU 1400 → GPU-Faults / Soft-Reset |
| v18-cpu2002 | CPU 2002, GPU 1400: einmal fehlerfrei (Glück), 246.725 Punkte (CPU 100.664, GPU 13.957, MEM 82.881, UX 49.223) |
| v19-gpu1352 / v19-gpu1300 | CPU 2054 + GPU 1352/1300 (gebaut) |
| v20 | CPU 2002, GPU 1400 + neue GPU-Steuerung (85 %) → wieder GPU-Faults (`0x4002`, `JOB_READ_FAULT`, Soft-Reset) |
| **v21-gpu1352** | CPU 2002, GPU 1352, neue GPU-Steuerung → **läuft aktuell, stabil** |
| v21-gpu1300 | Alternative mit GPU 1300 |
| v22-cpu2054 / v22-gpu1400 | neu gebaut, **ungetestet** (siehe Abschnitt 8) |

## 7. Messungen in diesem Chat (v21-gpu1352, AnTuTu v12)

| Lauf | Einstellung | CPU | GPU | MEM | UX | Gesamt |
|---|---|---|---|---|---|---|
| 1 | Serie (GPU 1,1375 V) | 102.547 | 18.411 | 81.098 | 48.420 | 250.476 |
| 2 | GPU `highspeed 1352/80`, warm | 102.863 | 17.474 | 84.732 | 49.122 | 254.191 |
| 3 | GPU 1352 @ 1,1125 V | 103.297 | **19.323** | 82.911 | 48.560 | 254.091 |

GPU-Zeitanteile unter Last (ohne Leerlauf), aus `time_in_state`:
- Serie: 77 % auf 1352, 16 % auf 845 · warmer Lauf: 64 % / 26 % · **1,1125 V: 84 % / 10 %** → weniger Spannung = weniger Hitze = mehr Takt.
- `highspeed_clock 1352 / highspeed_load 80` brachte nichts → zurück auf `756 / 95`.

CPU-Undervolt: 2002 @ 1,1875 V auf **cluster1** 110 s Volllast stabil (85 °C). cluster0 noch nicht abgesenkt/getestet.

## 8. Fertige Kernel (an den User geschickt, NICHT getestet)

Alle aus `boot_v21_backup.img` gebaut (Magisk-Ramdisk, dtb identisch zu v21):

| Datei | CPU | GPU | Zweck |
|---|---|---|---|
| `boot_A5Ultra_v22-cpu2054.img` | 2054 @ ~1,28 V (`TOP_EXTRA_UV=75000`), Grenze 1,30 V | 1352 | testet CPU 2054 mit ausreichender Spannung |
| `boot_A5Ultra_v22-gpu1400.img` | 2002 | 1400 @ ~1,21 V (`GPU_TOP_EXTRA_UV=150000`, `GPU_UV_MAX=1250000`), bis 1,25 V | testet, ob GPU-Faults an zu wenig Spannung lagen |
| `boot_A5Ultra_v21-gpu1352.img` | 2002 | 1352 | Rückfall = aktueller Kernel |

## 9. Plan / nächste Schritte (immer nur eine Sache gleichzeitig ändern)

1. **CPU 2054 testen** (v22-cpu2054)
   - Nach Boot: `.\adb shell "su -c 'uname -r; dmesg | grep ECT-OC; head -2 /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster1_volt_table'"`
   - HKTweaks: CPU-Max 2054 (beide Hälften), GPU-Max 1352, GPU-Governor Interactive, alle Spannungen 0 → AnTuTu.
   - Absturz bei 48 % (= CPU-Test) → 2054-Stufe auf 1,30 V:
     ``.\adb shell "su -c 'for c in 0 1; do echo 1300000 1112500 1087500 1043750 1006250 968750 925000 887500 862500 818750 806250 768750 737500 718750 681250 662500 662500 > /sys/devices/system/cpu/cpufreq/mp-cpufreq/cluster`${c}_volt_table; done'"``
   - Immer noch Absturz → 2002 ist das CPU-Maximum. Crash-Info: `.\adb shell "su -c 'uname -r; logcat -b crash -d | tail -30'"`
2. **GPU 1400 testen** (v22-gpu1400)
   - HKTweaks: GPU-Max 1400, CPU-Max 2002 → AnTuTu → danach:
     `.\adb shell "su -c 'uname -r; head -1 /sys/devices/11400000.mali/volt_table; dmesg | grep -i mali | grep -i -e fault -e reset | tail -10'"`
   - Faults → `echo 1400 1250000 > /sys/devices/11400000.mali/volt_table` → nochmal. Weiter Faults → 1352 bleibt Maximum
     (Gewinn wäre ohnehin nur +3,5 % Takt).
3. **Undervolting fertig ausloten** (bringt laut Messung mehr als OC, weil der Chip thermisch limitiert ist)
   - CPU: beide Cluster gemeinsam in 12,5-mV-Schritten senken, je 2 min Volllast-Test, Endwert = letzter stabiler + 25 mV.
   - GPU 1352: 1,1125 V lief; nächster Schritt 1,10 V usw., Kriterium: keine Bildfehler, keine `mali`-Faults, GPU-Punkte ≥ ~18.400.
4. **RAM-Übertaktung neu implementieren** (größtes Potenzial: 1539 → 1716 = +11 % Bandbreite)
   - In `ect_oc.c` MIF-Unlock ergänzen: `dvfs_mif`-Stufen 1716/1794 in DVFS + ASV freischalten, MIF-Spannung anheben
     (Kconfig `ECT_OC_MIF_*` existiert, wird in v21 nicht ausgewertet).
   - DTS `devfreq_mif`: `freq_info` (cal_qos_max, max_freq) von 1539000 anheben; DRAM-Timing-Zeilen der ECT prüfen (`S5E7880-drampara.c`).
   - Früher bei 1716/1794/1898 instabil, aber vermutlich mit Samsungs 0,80 V. Risiko: Datenkorruption → vorher Backup.
   - Hinweis: fester RAM-Takt per `min_freq 1539000` war **nicht** Ursache der GPU-Faults.
5. **MEM-Wert untersuchen**: v12 hatte MEM 138.960, seitdem 72–85k unabhängig vom Kernel → Ursache finden
   (ZRAM aus? Hotplug? Speicherbelegung?). Benchmark-Befehl, der getestet wurde:
   `echo 8 > /sys/power/cpuhotplug/min_online_cpu; echo deadline > /sys/block/mmcblk0/queue/scheduler; swapoff /dev/block/zram0; echo 0 > /proc/sys/vm/swappiness; am kill-all; sync; echo 3 > /proc/sys/vm/drop_caches`
   (ZRAM erhöht den MEM-Wert nicht, sondern senkt ihn.) Hotplug aus (`echo 0 > /sys/power/cpuhotplug/enabled`) erst separat testen.
6. **Endwerte ins Magisk-Modul** übernehmen (A5Ultra-Tweaks v5.1), damit nach jedem Boot automatisch gesetzt.

## 10. Geprüft und NICHT machbar

- **Display-OC**: S6E3FA3 im Command-Mode erzeugt die Bildrate selbst (TE-Signal vom Panel). `refresh 60→80` im DTS ändert nichts.
  Echte Änderung bräuchte undokumentierte Panel-Register.
- **Neueste Treiber**: Mali-Kernel-Treiber r28p0 muss zu Samsungs GPU-Bibliotheken im ROM passen; neuer Treiber = keine Grafik.
  Kernel 3.18 lässt sich ohne Samsungs Hardware-Treiber nicht auf neuere Versionen heben.
- **Bootanimation mit Text**: One UI nutzt das Samsung-eigene QMG-Format (`/system/media/bootsamsung.qmg`, `bootsamsungloop.qmg`).
  QMG kann man nicht selbst erzeugen (laut Samsung Knox nur Samsung aus PNGs), `bootanimation.zip` wird ignoriert.
  SMbootFX (Magisk) liefert nur fertige QMG-Themes, A5 2017 nicht bestätigt.
  **Machbar:** Bootlogo (erstes Bild) über `up_param` – macht das Magisk-Modul aus `.github/workflows/build.yml` bereits.
- CPU 2184: Neustart nach 7 s. GPU-Stufen 1404/1456: sinnlos, solange 1400 nicht stabil ist.

## 11. Magisk-Module

**A5Ultra-Tweaks v5.1** (nach jedem Boot):
- DT2W an, Sound-Boost (Kopfhörer +5 dB, Lautsprecher +3 dB, Hörmuschel +2 dB), TCP westwood,
  Log `/data/local/tmp/a5ultra.log` (ECT-OC-Meldungen).
- Absturzsicheres OC: mit `a5u keep` gespeicherte Werte werden 1 min nach Boot gesetzt; Absturz innerhalb 5 min → beim nächsten Boot OC aus.
- Befehl `a5u`: Status von CPU/GPU/RAM · `a5u cpu …` / `a5u gpu …` · `a5u stress` (8 Kerne 2 min Volllast).
- Setzt **nicht**: RAM fest, alle Kerne an, Scheduler – das ist der Benchmark-Befehl oben.

**„Galaxy A5 Ultra“-Modul** aus `.github/workflows/build.yml` (Workflow „Build A5 Ultra Magisk Module“):
- Modellname „Galaxy A5 Ultra“ (build.prop), diverse Props, Bootlogo „SAMSUNG / Galaxy A5 Ultra“ ins `up_param`,
  mixer_paths-Lautstärke, ZRAM 4,5 GB + swappiness 100, `scaling_max_freq 2002000`, Animationen 0,25.
- ⚠️ schreibt per `dd` in `up_param` → vorher sichern. ZRAM 4,5 GB senkt den AnTuTu-MEM-Wert.

## 12. Stresstest / nützliche Befehle

- CPU-Volllast 2 min mit Takt + Temperatur:
  ``.\adb shell "su -c 'for i in 1 2 3 4 5 6 7 8; do timeout 120 sh -c \"while :; do :; done\" & done; for t in 1 2 3 4 5 6 7 8 9 10 11 12; do sleep 10; echo `$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq) `$(cat /sys/devices/system/cpu/cpu4/cpufreq/cpuinfo_cur_freq 2>/dev/null) `$(cat /sys/class/thermal/thermal_zone0/temp); done'"``
- GPU-Zeitanteile: `time_in_state` vor und nach dem GPU-Test lesen, Differenz bilden, 343 (Leerlauf) herausrechnen.
- GPU-Live-Monitor während 3D-Test:
  ``while ($true) { .\adb shell "su -c 'echo GPU: `$(cat /sys/devices/11400000.mali/clock) RAM: `$(cat /sys/class/devfreq/17000010.devfreq_mif/cur_freq) Temp: `$(cat /sys/class/thermal/thermal_zone0/temp)'"; Start-Sleep 2 }``
- Ein Neustart setzt alle sysfs-Werte (Spannungen, Takte, ZRAM) auf die Kernel-Standards zurück.
