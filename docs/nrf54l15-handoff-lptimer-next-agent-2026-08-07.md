# nRF54L15 RCP — handoff dla następnego agenta: lptimer + ścieżka do wariantu C

**Data:** 2026-08-07  
**Cel tej fazy:** dokończyć **pełny port platform lptimer GRTC** (CC2 + CC8 + DPPI), potem rozszerzyć spike / integrację **wariantu C** (SL-binary bez MPSL).  
**Kontekst rozmowy:** decyzja A vs C, spike G1/G2, architektura binarek, FEM/DPPI/MPSL — skondensowane poniżej.

---

## 1. Gdzie jesteśmy (stan repo)

### Wariant produkcyjny docelowy (decyzja nie zamknięta)

| Wariant | SL | MPSL | RSCH | Status |
|---------|-----|------|------|--------|
| **D (POC dziś)** | `SL_OPENSOURCE=ON` + własny RSCH | brak | `*_baremetal.c` (~1200 LOC) | smoke OK, **FAIL pod load** |
| **C (kompromis flash)** | `libnrf-802154-sl.a` | stuby ~150 LOC | w binarce SL | spike **G1+G2 PASS** (2026-08-07), G3+ otwarte |
| **A (domyślny produkt)** | `libnrf-802154-sl.a` | `libmpsl.a` + glue | w binarce SL | spike nie wykonany |

**Rekomendacja w docs:** A = default produkt; C = sensowny jeśli ~84 KB MPSL to twardy limit flash **i** G3/G4/G5 przejdą.

### Spike wariantu C (2026-08-07)

| Gate | Wynik | Uwagi |
|------|-------|-------|
| **G1** link SL + stubs bez MPSL | ✅ PASS | brak unresolved symbols |
| **G2** `nrf_802154_rsch_init()` na hardware | ✅ PASS | pusty `sym_*` OK, RTT na DK |
| **G3** `nrf_802154_init()` + TX/RX | ⏳ | nie zaimplementowane w spike |
| **G4** CSMA @ load (1000+ TX @ 0.1 s) | ⏳ | wymaga **pełnego lptimer** |
| **G5** `ot-rcp` + Morty | ⏳ | po integracji C |

**Fix linkera G1:** `sym_stub.c` musi być w **executable**, nie w archiwum stubów (kolejność linku statycznego).

### Build ot-rcp dziś (wariant D)

- `third_party/nrf54/CMakeLists.txt`: **`SL_OPENSOURCE=ON`** (własny SL ze źródeł + baremetal RSCH/timer w platformie).
- `third_party/nrf54/platform/CMakeLists.txt`: **zawsze** linkuje `nrf_802154_rsch_baremetal.c` + `nrf_802154_sl_timer_baremetal.c`.
- Domyślnie: **`NRF54_LPTIMER_CC2_ONLY=ON`** → CC8/DPPI wyłączone (`hw_task_*` → `WRONG_STATE`).

**Uwaga:** integracja spike (`NRF54_SPIKE_SL_BINARY`, `script/spike-c-sl-binary.sh`) była w rozmowie; w głównym `third_party/nrf54/CMakeLists.txt` **może nie być** podpięta — katalog `third_party/nrf54/spike_c/` **istnieje**. Sprawdź przed buildem spike.

---

## 2. Kluczowe ustalenia z rozmowy (nie pomylić)

### RSCH — kto go implementuje

- **Wariant C NIE implementuje własnego RSCH.** RSCH/CSMA jest w **`libnrf-802154-sl.a`**.
- Pliki `nrf_802154_rsch_baremetal.c`, `nrf_802154_sl_timer_baremetal.c` to **wariant D** → **usunąć dopiero po** przejściu na `SL_OPENSOURCE=OFF` + SL-binary (nie na początku pracy nad samym lptimerem w POC).

### Lptimer vs MPSL vs DPPI

| Warstwa | Gdzie | A | C |
|---------|-------|---|---|
| RSCH/CSMA | SL-binary | ✅ | ✅ (ten sam `.a`) |
| **Platform lptimer** (CC2, CC8, DPPI hw_task) | **źródła** — port NCS → nrfx | **Ty portujesz** | **Ten sam kod co A** |
| RAAL (airtime) | MPSL REM vs stub | MPSL | single-PHY stub (~100 LOC) |
| FEM Skyworks | `mpsl_fem_*` | `libmpsl.a` | `sl_opensource/nrf_802154_sl_fem.c` (no-op) **albo** A |
| DPPI | hardware + driver + lptimer + timestamper | wszędzie | **bez MPSL też** — brak tylko `MPSL_RESERVED_*` check |

**DPPI nie jest „tylko z MPSL”.** Lptimer implementuje **swój** kawałek DPPI (GRTC CC8 → RADIO task). Driver ma **osobną** mapę (DPPIC10 ch 3–23). Kolizja lptimer↔driver już obserwowana — komentarz w `nrf_802154_platform_sl_lptimer_grtc_hw_task.c`.

### Thread-only RCP = single-PHY OK

Stub RAAL w C jest **architektonicznie poprawny** dla RCP (jak `RAAL_SINGLE_PHY=1` na nRF52). MPSL REM nie jest potrzebny do airtime przy jednym protokole.

### FEM

- **`spike_c/stubs/`** = RAAL, coex, sym, wifi_coex — **bez FEM**.
- FEM = **`sl_opensource/src/nrf_802154_sl_fem.c`** (osobny plik, no-op PA/LNA).
- Produkt ze **Skyworks** → rozważ **wariant A** (pełne `mpsl_fem_*` z MPSL); stub nie steruje PA/LNA.

### Ryzyko resztkowe C (przy gotowym lptimerze)

1. **Mapa DPPI/GRTC** (driver vs lptimer vs OT alarm CC0–1 vs timestamper) — główne ryzyko **cliff pod load**.
2. **FEM** — głównie moc/zasięg (Skyworks), nie typowy root cause load na DK bez FEM.

Szczegóły: `docs/nrf54l15-rcp-driver-variant-comparison.md` §4.4.1, `docs/nrf54l15-no-mpsl-sl-binary-decision.md` §9.1.

---

## 3. Zadanie na tę fazę: pełny lptimer

### Co znaczy „pełny”

Referencja NCS: `nrf_802154/sl/platform/` → `nrf_802154_platform_sl_lptimer_grtc.c` + `lptimer_grtc_hw_task.c`.

| Mechanizm | Rola | Plik POC |
|-----------|------|----------|
| **CC2** + `schedule_at` | timer software (ISR → callback SL) | `nrf_802154_platform_sl_lptimer.c` |
| **CC8** + `hw_task_*` + **DPPI** | GRTC compare → RADIO task bez CPU | `nrf_802154_platform_sl_lptimer_grtc_hw_task.c` |

API kontraktu SL (wołane z binarki): `third_party/nrf54/nordic/drivers/nrf_802154/sl/include/platform/nrf_802154_platform_sl_lptimer.h` — m.in. `hw_task_prepare`, `hw_task_update_ppi`, `hw_task_cleanup`.

### Stan POC lptimer

| Plik | ~LOC | Stan |
|------|------|------|
| `platform/nrf_802154_platform_sl_lptimer.c` | 499 | CC2 częściowo; bisect flagi |
| `platform/nrf_802154_platform_sl_lptimer_grtc_hw_task.c` | 108 | istnieje; **wyłączone** gdy `NRF54_LPTIMER_CC2_ONLY=ON` |
| `platform/nrf_802154_platform_sl_lptimer_stub.c` | 109 | minimal stub (spike / link-only) |

### CMake / flagi (`third_party/nrf54/platform/CMakeLists.txt`)

| Opcja | Domyślnie | Efekt |
|-------|-----------|-------|
| `NRF54_POC_MINIMAL_TIMERS` | OFF | pełny `lptimer.c` vs `lptimer_stub.c` |
| `NRF54_LPTIMER_CC2_ONLY` | **ON** | CC8/DPPI off → `NRF54_LPTIMER_CC2_ONLY_BISECT=1` |
| `NRF54_LPTIMER_BISECT_STUB` | OFF | bisect test |

**Cel produkcyjny (A i C):** `NRF54_LPTIMER_CC2_ONLY=OFF`, link `grtc_hw_task.c`, `NRF54_LPTIMER_CC2_ONLY_BISECT=0`.

### Znany problem do rozwiązania

W `nrf_802154_platform_sl_lptimer_grtc_hw_task.c`:

- DPPIC10 **ch 3–23** = driver 802.15.4.
- Lptimer cross-domain używa **DPPIC10 ch 0** (PERI→RAD).
- Claim całego DPPIC10 psuł **CCA** przy włączonym CC8 — nie wystarczy „flip flagi”; trzeba trzymać się mapy jak w NCS / komentarzu w pliku.

### GRTC budget (OT vs SL)

- OpenThread alarm: GRTC **CC0–1** (platform OT).
- SL lptimer: **CC2**, **CC8**.
- Mapę CC udokumentować; unikać overlap.

### Timestamper

- Spike ma `spike_c/stubs/timestamper_stub.c`.
- Pełny port z NCS (cross-domain DPPI) — **osobny krok**; G3 może wymagać więcej niż stub.

---

## 4. Plan pracy (sugerowany)

### Faza 1 — lptimer na obecnym POC (wariant D) — **najpierw to**

Cel: walidacja timingów **przed** dużą integracją SL-binary.

1. Ustaw `NRF54_LPTIMER_CC2_ONLY=OFF` w CMake (lub `-DNRF54_LPTIMER_CC2_ONLY=OFF`).
2. Porównaj z NCS `lptimer_grtc.c` / `lptimer_grtc_hw_task.c` (ten sam tag co driver `_nowy`).
3. Napraw kolizję DPPI (DPPIC10 ch 0 vs 3–23) — regression: CCA, smoke TX.
4. Test load na **obecnym** `ot-rcp` (Morty / ping 1000 B @ 0.1 s) — czy cliff z wariantu D znika **częściowo** (RSCH nadal baremetal, ale lptimer pełny).

**Szacunek:** 1–2 tygodnie (nie 1–2 dni).

### Faza 2 — spike / wariant C

1. Przywróć / zweryfikuj build spike: `SL_OPENSOURCE=OFF`, `NRF54_SL_BINARY_PATH` → `libnrf-802154-sl.a` (`nrf54l15_cpuapp/hard-float`).
2. Spike **bez** `*_baremetal.c` w platformie (warunek CMake przy SL-binary).
3. Linkuj: `spike_c/stubs/*` + `sym_stub.c` w executable + `sl_opensource/nrf_802154_sl_fem.c` (FEM no-op).
4. Rozszerz `main_spike.c`: G3 `nrf_802154_init()` + pętla CSMA TX; G4 load.
5. Stuby RAAL/coex/sym — **już gotowe** (~167 LOC w `spike_c/stubs/`).

### Faza 3 — integracja `ot-rcp` wariant C

1. `SL_OPENSOURCE=OFF`, link SL-binary.
2. **Usuń** z platformy:
   - `nrf_802154_rsch_baremetal.c` (~688 LOC)
   - `nrf_802154_sl_timer_baremetal.c` (~511 LOC)
3. Pełny lptimer + timestamper (wspólne A/C).
4. Regresja Morty (G5).

### Faza 4 — decyzja C vs A

- C PASS + flash constraint → kontynuuj C.
- Fail G4/G5 lub Skyworks FEM → pivot **A** (lptimer zostaje wspólny).

---

## 5. Pliki — mapa

### Lptimer (edytować)

```
third_party/nrf54/platform/nrf_802154_platform_sl_lptimer.c
third_party/nrf54/platform/nrf_802154_platform_sl_lptimer_grtc_hw_task.c
third_party/nrf54/platform/nrf_802154_platform_sl_lptimer_grtc_hw_task.h
third_party/nrf54/platform/CMakeLists.txt
third_party/nrf54/platform/nrf54_debug_stats.c          # opcjonalnie statystyki hw_task
```

### Usunąć **po** SL-binary (nie na start lptimer w POC)

```
third_party/nrf54/platform/nrf_802154_rsch_baremetal.c      (~688 LOC)
third_party/nrf54/platform/nrf_802154_sl_timer_baremetal.c  (~511 LOC)
```

### Spike C (gotowe, rozszerzyć main)

```
third_party/nrf54/spike_c/main_spike.c
third_party/nrf54/spike_c/CMakeLists.txt
third_party/nrf54/spike_c/stubs/nrf_raal_single_phy.c
third_party/nrf54/spike_c/stubs/mpsl_cx_stub.c
third_party/nrf54/spike_c/stubs/sym_stub.c
third_party/nrf54/spike_c/stubs/wifi_coex_stub.c
third_party/nrf54/spike_c/stubs/timestamper_stub.c
```

### SL-binary

```
third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a
third_party/nrf54/bin/libnrf-802154-sl.a   # kopia build (gitignored)
third_party/nrf54/nordic/drivers/nrf_802154/sl/sl/CMakeLists.txt  # link .a gdy SL_OPENSOURCE=OFF
```

### FEM (osobno od spike stubów)

```
third_party/nrf54/nordic/drivers/nrf_802154/sl/sl_opensource/src/nrf_802154_sl_fem.c
src/nrf54l15/fem_nrf54.c   # OT platform stub — produkt Skyworks wymaga pracy
```

### Dokumentacja

```
docs/nrf54l15-rcp-driver-variant-comparison.md    # A vs C, §4.4.1 ryzyko przy gotowym lptimerze
docs/nrf54l15-no-mpsl-sl-binary-decision.md       # wariant C, spike, §9.1
docs/nrf54l15-mpsl-sl-binary-decision.md          # wariant A
docs/nrf54l15-no-binaries-no-mpsl-decision.md      # wariant D (POC)
docs/RCP_NRF52_TO_NRF54.md
```

---

## 6. Binarki — przypomnienie

| Binarka | A | C | D |
|---------|---|---|---|
| `libnrf-802154-sl.a` | ✅ | ✅ | ❌ |
| `libmpsl.a` | ✅ | ❌ | ❌ |

Toolchain: **hard-float**, `nrf54l15_cpuapp/hard-float/`.  
Driver + SL z **jednego tagu** nrfxlib / NCS.

---

## 7. Bilans LOC (uproszczony)

| | Usuń (D→C/A) | Dodaj |
|--|--------------|-------|
| RSCH/timer baremetal | ~1200 LOC | — |
| Stuby MPSL (C) | — | ~150 LOC |
| MPSL glue (A) | — | ~500–1500 LOC |
| Lptimer pełny | bisect POC → port NCS | refactor ~700 LOC (nie +700 od zera) |

---

## 8. Testy / pass criteria

| Test | Pass |
|------|------|
| Lptimer CC8 włączony | build bez bisect; CCA nie zepsute |
| POC ot-rcp smoke | attach, ping podstawowy |
| Load | brak eskalacji `radio tx timeout`, `tx_core_deny_terminate_fail` |
| Spike G3 | jedna ramka CSMA / sniffer |
| Spike G4 | 1000+ TX @ 0.1 s |
| G5 Morty | cliff, ping pass rate |

Handoff testów sprzętu: `docs/nrf54l15-handoff-2026-08-05.md`, `docs/nrf54l15-ctf-test-results-2026-08-05.md`.

---

## 9. Czego NIE robić na początku

- Nie usuwać `*_baremetal.c` przed działającym buildem ze SL-binary.
- Nie zakładać, że lptimer CC2-only wystarczy dla SL-binary pod load (G4).
- Nie mylić stubów spike (RAAL/coex/sym) z FEM (`sl_fem.c`).
- Nie linkować `libmpsl.a` „tylko dla DPPI” — DPPI jest w driverze i lptimerze; MPSL daje rezerwację + FEM.
- Commity tylko na prośbę użytkownika.

---

## 10. Jedno zdanie dla agenta

**Dokończ pełny port lptimer GRTC (CC2+CC8+DPPI, napraw mapę DPPIC10 vs driver), przetestuj na obecnym ot-rcp; potem podłącz wariant C (SL-binary + istniejące stuby, usuń baremetal RSCH) i zamknij G3–G5 — decyzja C vs A zależy od load i FEM, nie od RSCH stubów.**
