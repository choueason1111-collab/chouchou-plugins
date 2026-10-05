# Chouchou Compressor — MaxMSP 對照說明

這份與 JUCE 版是**同一條數學**，方便你對照看懂。

## 在做什麼？

把聲音切成很多頻率 bin（FFT），對**每一個 bin 的振幅**各自做動態：

```
振幅 (dB)
  ^
  |          ╲  highThresh 以上 → 壓低 (downward)
  |           ╲
──|────────────╲──────── high
  |             ║
  |   中間帶    ║  → gain = 1（完全不動）
  |             ║
──|────────────╱──────── low
  |           ╱
  |          ╱  lowThresh 以下 → 拉高 (upward)
```

結果：頻譜動態差變小 → 「黏合 / glue」感。

## 與 chouchouEQGate 的差別

| | EQ Gate（expander） | Compressor（本插件） |
|---|---|---|
| 強的頻率 | 更強 | 壓低 |
| 弱的頻率 | 更弱 | 拉高 |
| 中間 | 視 amount | **不動** |

## 每個 bin 的公式（與 JUCE 相同）

1. 量測該 bin 振幅 `mag`
2. Attack / Release 平滑成 envelope `env`
3. 算 gain：

```
if env < lowThresh:          # 向上壓縮（拉高小聲）
    gainDb = (lowDb - envDb) * (1 - 1/upRatio)

elif env > highThresh:       # 向下壓縮（壓大聲）
    gainDb = (highDb - envDb) * (1 - 1/downRatio)

else:
    gainDb = 0               # 不動

gain = 10^(gainDb/20)
out_mag = mag * ((1-mix) + mix*(gain*makeup))
```

`Mix = 0` → 完全乾聲（Makeup 不作用）；`Mix = 1` → 完整動態 + Makeup。

`ratio = 1` → 無效；`ratio` 越大 → 越用力往閾值「黏」。

## Max 裡怎麼接？

```
adc~ / 音檔
   │
   ▼
pfft~ chouchou_compressor_pfft 2048 4
   │
   ▼
dac~
```

`pfft~` 子 patch 裡（每個 sample = 一個 bin）：

```
fftin~ 1
   │
cartopol~          → mag, phase
   │
gen~ (dual_threshold.genexpr)
   │                 → new_mag, phase
poltocar~
   │
fftout~ 1
```

## 參數對照

| Max / UI | JUCE param | 預設 |
|---|---|---|
| Low Thresh (dB) | `lowthresh` | -50 |
| High Thresh (dB) | `highthresh` | -18 |
| Up Ratio | `upratio` | 2 |
| Down Ratio | `downratio` | 4 |
| Attack (ms) | `attack` | 5 |
| Release (ms) | `release` | 80 |
| Mix | `mix` | 1 |
| Makeup (dB) | `makeup` | 0 |
| hopMs | （由 FFT/hop 推得） | ≈11.6 @ 44.1k / 2048/4 |

## 檔案

- `dual_threshold.genexpr` — 核心公式（貼進 gen~）
- `chouchou_compressor_pfft.maxpat` — pfft 子 patch
- `chouchou_compressor.maxpat` — 主 patch（旋鈕 + pfft~）

先開 `chouchou_compressor.maxpat`。
