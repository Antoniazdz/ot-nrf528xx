# nRF54L15 RCP: wariant A vs C — decyzja integracji drivera nrf_802154

Notatka decyzyjna — porównanie dwóch realnych ścieżek do produkcyjnego bare-metal RCP na nRF54L15.

**Data:** 2026-08-07 (aktualizacja wyników spike: 2026-08-07)  
**Decyzja do podjęcia:** **A (MPSL + SL-binary)** vs **C (brak MPSL + SL-binary)**  
**Kontekst wyjściowy:** POC (wariant D — brak binarek, własny RSCH) działa na smoke, ale **FAIL pod load**; oba warianty A i C rozwiązują to przez `libnrf-802154-sl.a`.

**Stan spike C (2026-08-07):** G1 link ✅ · G2 `rsch_init()` hardware ✅ · G3+ (CSMA/load) ⏳

Powiązane dokumenty:
- [Wariant A — szczegóły](nrf54l15-mpsl-sl-binary-decision.md)
- [Wariant C — szczegóły](nrf54l15-no-mpsl-sl-binary-decision.md)
- [Obecny POC (wariant D)](nrf54l15-no-binaries-no-mpsl-decision.md)
- [Handoff testy](nrf54l15-handoff-2026-08-05.md) · [Wyniki CTF](nrf54l15-ctf-test-results-2026-08-05.md) · [Migracja nRF52→nRF54](RCP_NRF52_TO_NRF54.md)

Oficjalny driver: [sdk-nrfxlib/nrf_802154](https://github.com/nrfconnect/sdk-nrfconnect/sdk-nrfxlib/tree/main/nrf_802154)  
Kopia w repo: `third_party/nrf54/nordic/drivers/nrf_802154_nowy`

---

## 1. Executive summary — A vs C

| | **A: MPSL + SL-binary** | **C: brak MPSL + SL-binary** |
|--|-------------------------|------------------------------|
| **Idea** | Oficjalny stack Nordic na bare-metal | Ten sam SL-binary, ale **MPSL zastąpione hackami** |
| **Flash `.text`** | ~180–220 KB | ~120–140 KB (**~84 KB mniej**) |
| **Binarki** | `libnrf-802154-sl.a` + `libmpsl.a` | tylko `libnrf-802154-sl.a` |
| **Nowa warstwa** | MPSL platform glue (~500–1500 LOC) | RAAL/coex/sym stubs + lptimer (~150 LOC + port GRTC) |
| **Wsparcie Nordic** | **Tak** (NCS referencja) | **Nie** (niewspierana ścieżka) |
| **Pewność** | Wysoka | Średnia → **wyższa po G2** (sym_* OK); G3+ nadal otwarte |
| **Czas do produktu** | **3–5 tyg.** | 3–6 tyg. (niepewny) |
| **Verdict** | **Domyślny cel produktowy** | **G1/G2 PASS** — warto G3+ jeśli flash to constraint |
| **Spike status** | ⏳ nie wykonany | ✅ **G1+G2 PASS** (2026-08-07) |

**Wspólne dla A i C:** oba linkują `libnrf-802154-sl.a` (~43 KB) — produkcyjny RSCH/CSMA/DTRX/timestamp; oba usuwają ~1400 LOC własnego RSCH z POC; oba zachowują bare-metal (CMake, bez Zephyra).

**Różnica w jednym zdaniu:** A płaci ~84 KB flash za oficjalny arbiter MPSL i przewidywalność; C oszczędza te ~84 KB, ale **sam dostarcza stuby** (RAAL, coex, sym_*, lptimer) na niewspieranej ścieżce Nordic.

---

## 2. Punkt wyjścia (POC — wariant D, tylko kontekst)

Obecny build (~96 KB `.text`) używa `SL_OPENSOURCE=ON` + własny RSCH (~2600 LOC). Smoke OK, load FAIL:

| Problem | Objaw |
|---------|-------|
| Cliff pod load | `radio tx timeout` → RCP failure |
| CSMA terminate | `tx_core_deny_terminate_fail` rośnie |
| Sustained TX | crash po UDP/iperf |

**Wniosek:** driver źródłowy OK; blocker to brak produkcyjnego SL. **A i C rozwiązują to tak samo** — przez binarkę SL. Różnica leży wyłącznie w tym, **skąd bierze się arbiter radia i platform glue**.

Warianty B (MPSL + SL-opensource) i kontynuacja D **nie są rozważane** — B łączy koszt MPSL z problemami stub SL; D ma potwierdzony FAIL pod load.

---

## 3. Architektura — A vs C obok siebie

```
┌──────────────────────────────────────────────────────────────────────────┐
│  OpenThread RCP + otPlat* (radio_nrf54.c, UART, alarm, …)   ✅ bez zmian  │
├──────────────────────────────────────────────────────────────────────────┤
│  Build: CMake, nrfx 4.x, MDK, startup                        ✅ bez zmian  │
├──────────────────────────────────────────────────────────────────────────┤
│  nrf_802154 driver (źródła, SL_OPENSOURCE=OFF)               ✅ bez zmian  │
├──────────────────────────────────────────────────────────────────────────┤
│  libnrf-802154-sl.a (~43 KB)                    🆕 OBA warianty          │
│    RSCH / CSMA / DTRX / timestamp WEWNĄTRZ binarki                       │
├───────────────────────────────┬──────────────────────────────────────────┤
│  A: libmpsl.a (~84 KB)        │  C: brak libmpsl.a                       │
│  A: MPSL platform glue        │  C: RAAL single-PHY stub (~100 LOC)      │
│     mpsl_init()               │  C: mpsl_cx stub (~50 LOC)               │
│     mpsl_low_priority_process │  C: sym_* license interlock ⚠️           │
│     clock, IRQ, FEM via MPSL  │  C: wifi_coex stub                       │
├───────────────────────────────┴──────────────────────────────────────────┤
│  Platform lptimer GRTC + timestamper          🔧 OBA — port z NCS       │
│    (A: referencja NCS kompletna; C: pełny port, bez bisect CC8)          │
├──────────────────────────────────────────────────────────────────────────┤
│  *_baremetal.c (rsch, timer, lptimer workaround)   ❌ OBA — usuń        │
└──────────────────────────────────────────────────────────────────────────┘
```

**Co A ma, a C nie:**
- Oficjalny RAAL/arbiter (REM) z MPSL
- `MPSL_RESERVED_*` — rezerwacja PPI/DPPI/GRTC, mniejsze ryzyko kolizji z OT
- Pełne `mpsl_fem_*` dla Skyworks
- Upgrade arbitera/clock/FEM z tagu nrfxlib

**Co C ma, a A nie:**
- ~84 KB mniej flash
- Prostszy main loop (brak `mpsl_low_priority_process()`)
- Brak buforów/stanu MPSL w RAM
- Jedna binarka Nordic do wersjonowania (tylko SL)

### 3.1 Mapa binarek z [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib)

Repozytorium [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib) zawiera **wiele** pakietów z prebuilt `.a` / `.lib` (BLE controller, modem, WiFi, NFC, …).  
Dla **bare-metal Thread RCP nRF54L15** (ot-nrf54xx) interesują nas **tylko dwa** pakiety z tego repo — reszta **nie jest linkowana**.

#### Stos — co jest binarką, a co źródłami

```
ot-rcp + OpenThread          ← źródła (submodule openthread/, nie nrfxlib/openthread .a)
platform (radio_nrf54, UART) ← źródła
nrf_802154 driver            ← źródła (nrfxlib/nrf_802154/driver/ → vendor w repo)
─────────────────────────────────────────────────────────
libnrf-802154-sl.a           ← binarka (nrfxlib/nrf_802154/)     A ✅  C ✅  D ❌
libmpsl.a                    ← binarka (nrfxlib/mpsl/)             A ✅  C ❌  D ❌
─────────────────────────────────────────────────────────
nrfx + MDK + startup         ← źródła (NCS/nrfx, nie nrfxlib)
```

#### Wariant A — linkowane binarki z nrfxlib

| Binarka | Pakiet nrfxlib | Rola | Ścieżka (nRF54L15 app, przykład) | Flash (szac.) |
|---------|----------------|------|----------------------------------|---------------|
| **`libnrf-802154-sl.a`** | [`nrf_802154`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_802154) | Zamknięty Service Layer: RSCH, CSMA, DTRX, timer coord, część atomics | `nrf_802154/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a` | ~43 KB |
| **`libmpsl.a`** | [`mpsl`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/mpsl) | Multiprotocol Service Layer: RAAL (arbiter radia), zegary, FEM API, coex (`mpsl_cx_*`), `sym_*`, rezerwacja PPI/DPPI | `mpsl/lib/nrf54l15_cpuapp/hard-float/libmpsl.a` (lub `mpsl/lib/nrf54l/...` — zależnie od tagu NCS) | ~84 KB |

**Razem binarki Nordic (A):** **2× `.a`** (~127 KB).  
**Ze źródeł (ten sam tag nrfxlib):** `nrf_802154/driver/`, `nrf_802154/common/`, platform lptimer/timestamper (port NCS → bare-metal), **MPSL platform glue** (~500–1500 LOC, nie jest w `.a`).

Wersjonowanie: driver źródłowy + obie `.a` z **jednego tagu** [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib) / NCS.

#### Wariant C — linkowane binarki z nrfxlib

| Binarka | Link? | Zastępnik gdy brak |
|---------|-------|-------------------|
| **`libnrf-802154-sl.a`** | ✅ **tak** (identyczna jak w A) | — |
| **`libmpsl.a`** | ❌ **nie** | Własne stuby: RAAL single-PHY, `mpsl_cx_*`, `sym_*`, wifi_coex (~150 LOC); spike G2: pusty `sym_*` OK |

**Razem binarki Nordic (C):** **1× `.a`** (~43 KB).  
Interfejsy, które w A dostarcza MPSL, C implementuje w **`third_party/nrf54/spike_c/stubs/`** (+ platform lptimer/timestamper ze źródeł).

#### Wariant D (POC dziś) — binarki nrfxlib

| Binarka | Link? |
|---------|-------|
| `libnrf-802154-sl.a` | ❌ — zamiast tego `sl_opensource` (źródła) + własny RSCH |
| `libmpsl.a` | ❌ |

**Razem binarki Nordic (D):** **0**.

#### Pakiety nrfxlib — **nie** używane w Thread RCP (A/C/D)

Te katalogi istnieją w [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib), ale **nie linkujesz ich** do ot-rcp Thread-only:

| Pakiet nrfxlib | Typowe binarki | Dlaczego nie w RCP Thread |
|----------------|----------------|---------------------------|
| [`softdevice_controller`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/softdevice_controller) | BLE Link Layer `.a` | BLE to **osobny** stack; RCP Thread nie potrzebuje kontrolera BLE |
| [`openthread`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/openthread) | Prebuilt OT dla NCS/Zephyr | ot-nrf54xx buduje OpenThread ze **źródeł** (`openthread/`), nie z nrfxlib |
| [`nrf_modem`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_modem) | Modem LTE `.a` | Cellular — poza scope RCP 802.15.4 |
| [`nrf_wifi`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_wifi) / `nrf71_wifi` | WiFi firmware `.bin` | WiFi to osobny chip/stack; coex z WiFi w RCP → stub `mpsl_cx_*` (C) lub MPSL (A) |
| [`nfc`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nfc) | NFC `.a` | Nie używane |
| [`crypto`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/crypto) | Crypto `.a` (CC310/CC3xx) | ot-rcp: mbedTLS z OpenThread, nie nrfxlib crypto |
| [`gzll`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/gzll), [`lc3`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/lc3), [`nrf_dm`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_dm), [`nrf_fuel_gauge`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_fuel_gauge), [`nrf_rpc`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_rpc), [`softperipheral`](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/softperipheral) | różne | Poza scope 802.15.4 RCP |

**Multiprotocol BLE + Thread** (poza obecnym produktem): wymagałby dodatkowo m.in. `softdevice_controller` + host BLE **oraz** MPSL REM — to nadal **nie** jest „trzecia binarka 802.15.4”, tylko osobny stack BLE obok tego samego SL+MPSL.

#### Gdzie binarki leżą w ot-nrf54xx

| Artefakt | W repo |
|----------|--------|
| SL (referencja) | `third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/` |
| SL (build spike C) | `third_party/nrf54/bin/libnrf-802154-sl.a` (kopia, gitignored) |
| MPSL | **nie w repo** — z NCS workspace: `nrfxlib/mpsl/lib/...` |
| Driver aktywny | `third_party/nrf54/nordic/drivers/nrf_802154/` — **tylko źródła**, bez `sl/sl/lib/` |

Toolchain ot-nrf54xx: **hard-float** (`arm-none-eabi.cmake`); wybieraj wariant `nrf54l15_cpuapp/hard-float/`, nie soft-float z przykładów NCS.

---

## 4. Porównanie A vs C — aspekt po aspepcie

### 4.1 Stabilność radia i featury

| Aspekt | A | C |
|--------|---|---|
| CSMA / RSCH | Produkcyjny (SL-binary) | Produkcyjny (SL-binary) — **ten sam kod** |
| Delayed TRX, timestamp, IFS | ✅ | ✅ |
| Stabilność pod load | Docelowa dobra (ref. NCS) | **Do weryfikacji** — spike gate |
| Referencja do debugowania | NCS/Zephyr spike na tym samym SL | Brak — tylko własny QA |
| Thread-only RCP | ✅ wystarczy | ✅ wystarczy |

**Wniosek:** warstwa schedulera radia jest **identyczna** (SL-binary). Ryzyko C leży w **platform glue pod SL**, nie w samym RSCH.

### 4.2 Pamięć

| Aspekt | A | C | Delta (C vs A) |
|--------|---|---|----------------|
| Flash `.text` (szac.) | ~180–220 KB | ~120–140 KB | **−~84 KB** |
| vs POC (~96 KB) | +84–124 KB | +24–44 KB | C bliżej POC |
| Binarki Nordic | 2 (SL + MPSL) | 1 (SL) | −1 binarka |
| RAM | + buforów MPSL | ~14 KB + delta SL | C mniejszy |
| Boot / idle CPU | `mpsl_init()` + LP process co tick | krótszy init, prostsza pętla | C lżejszy runtime |

**Wniosek:** C ma sens **tylko** jeśli ~84 KB MPSL to twardy blocker produktowy. Inaczej A jest prostszy do uzasadnienia.

### 4.3 Nakład pracy

| Etap | A | C |
|------|---|---|
| Binarki + CMake | 2–3 dni (SL + MPSL) | 2–3 dni (SL + stubs) |
| Nowa warstwa platform | MPSL glue: 1–2 tyg | RAAL/coex/sym: 2–3 dni + lptimer: 1–2 tyg |
| Spike (bez OT) | 2–5 dni | 1–2 tyg (więcej unknowns) |
| FEM Skyworks | 3–5 dni (`mpsl_fem_*`) | 3–5 dni (własne/stub — więcej ryzyka) |
| Usunięcie `*_baremetal.c` | 3–5 dni | 1–2 dni |
| Regresja Morty | 1–2 tyg | 1–2 tyg |
| **Razem** | **3–5 tyg.** | **3–6 tyg.** (wyższe ryzyko opóźnienia) |
| Reużycie POC | ~60–70% | ~65–75% |
| Własny kod do utrzymania | ~500–1500 LOC (MPSL glue) | ~150 LOC stuby + port lptimer |

**Wniosek:** nakład podobny rzędu wielkości, ale **A ma mniejszą wariancję** — MPSL glue ma referencję NCS; C ma niewiadome (`sym_*`, lptimer pod load).

### 4.4 Ryzyka

| Ryzyko | A | C |
|--------|---|---|
| Niewspierana ścieżka Nordic | Nie | **Tak** |
| sym_* license interlock | Nie dotyczy (MPSL linkowany) | ✅ **G2 PASS** — pusty stub OK na hardware |
| GRTC CC budget / lptimer | Referencja NCS kompletna | Ten sam port co A, ale **bez MPSL reserved channels** |
| FEM / TX power | Pełne API Nordic | Więcej własnej pracy |
| Kolizje PPI/DPPI z OT | MPSL rezerwuje zasoby | **Ryzyko** — brak `MPSL_RESERVED_*` |
| Wersjonowanie | 1 tag → driver + 2× `.a` | 1 tag → driver + SL + własne stuby |
| Certyfikacja / QA | Niskie (ref. NCS) | Średnie-wysokie |
| Pivot cost (fail) | — | Niski: spike 1–2 tyg., potem A |

### 4.4.1 Ryzyko C vs A przy gotowym lptimerze

**Założenie:** port lptimer GRTC (CC2 + CC8 + DPPI) i timestamper **zakończone i wspólne** dla A i C. RSCH/CSMA pochodzi z tego samego `libnrf-802154-sl.a` — różnica leży w **platform glue pod SL**, które w A dostarcza MPSL, a w C zastępują ~150 LOC stubów.

#### Co już nie jest ryzykiem C (gate zamknięte)

| Obszar | Status |
|--------|--------|
| Link SL bez MPSL | ✅ G1 PASS |
| `sym_*` przy `rsch_init()` | ✅ G2 PASS — pusty stub OK |
| RSCH / CSMA / delayed TRX | Ten sam SL-binary co A |

#### Ranking ryzyka C, którego A unika (lptimer = done)

| Priorytet | Ryzyko C | A unika przez |
|-----------|----------|---------------|
| **1** | **FEM / TX power** (Skyworks PA/LNA) | `libmpsl.a` — pełne `mpsl_fem_*` (driver woła je w `nrf_802154_trx.c`); C: stub `sl_opensource` lub własny port |
| **2** | **Kolizje DPPI/PPI z OT + driver** | `MPSL_RESERVED_*` + compile-time check w `nrf_802154_peripherals_alloc.c`; C: ręczna mapa kanałów |
| **3** | **RAAL stub** (zawsze grant, `timeslot_us_left → ∞`) | MPSL REM — oficjalny arbiter z referencją NCS |
| **4** | **Clock glue** (HFCLK/LFCLK vs driver/SL) | MPSL clock API + unifikacja z `nrf_802154_clock_platform.c` |
| **5** | **`sym_*` poza init** | Prawdziwy symbol z `libmpsl.a` (G2 tylko init; głębszy check mało prawdopodobny, nieudokumentowany) |
| **6** | **Coexistence** (Wi‑Fi PTA / BLE) | MPSL `mpsl_cx_*` — C: stub zawsze grant (OK dla Thread-only) |
| **7** | **Proces** (wsparcie Nordic, upgrade stubów, QA/certyfikacja, multiprotocol) | Oficjalna ścieżka NCS; C: niewspierana kombinacja SL bez MPSL |

#### Scenariusze produktowe

| Scenariusz | C po gotowym lptimerze |
|------------|------------------------|
| DK, antena onboard, Thread-only, bez zewnętrznego FEM | Może wystarczyć po G3/G5 |
| Produkt ze **Skyworks FEM** | **Słabe** — nawet z idealnym lptimerem; rozważ A |
| BLE + Thread multiprotocol | C **nie** — wymaga MPSL REM |

> **Wniosek:** przy gotowym lptimerze C to nadal „Thread-only hack”: ten sam scheduler w SL, ale własne stuby zamiast MPSL w arbiterze, rezerwacji peryferiów i FEM. **FEM + mapa DPPI** to główne ryzyka resztkowe, nie lptimer.

### 4.5 Build, utrzymanie, upgrade

| Aspekt | A | C |
|--------|---|---|
| Bare-metal (bez Zephyr) | ✅ | ✅ |
| Morty / ot-daemon | ✅ | ✅ |
| Debug schedulera (RSCH) | W `.a`, ref. NCS | W `.a`, ref. NCS — **identycznie** |
| Debug arbitera | W `.a` MPSL, ref. NCS | **Własny stub** — source-level, ale bez referencji |
| Upgrade z nrfxlib | Driver + obie binarki | Driver + SL; stuby ręcznie |
| Multiprotocol (BLE+Thread) | ✅ (MPSL REM) | ❌ |

---

## 5. Tabela zbiorcza A vs C

| | **A: MPSL + SL-bin** | **C: brak MPSL + SL-bin** |
|--|----------------------|---------------------------|
| Produkcyjny RSCH/CSMA | ✅ SL-binary | ✅ SL-binary (ten sam) |
| Flash `.text` | ~180–220 KB | ~120–140 KB |
| Oszczędność flash vs A | — | **~84 KB** |
| Wsparcie Nordic | **tak** | nie (hack) |
| Główne ryzyko | flash size, MPSL glue | sym_*, lptimer, brak MPSL reserved |
| Własny kod nowy | MPSL platform glue | RAAL/coex/sym stubs |
| Własny RSCH (POC) | usuń ~1400 LOC | usuń ~1400 LOC |
| Czas | **3–5 tyg.** | 3–6 tyg. (spike gate) |
| Pewność | wysoka | średnia |
| **Rekomendacja** | **domyślny cel** | **G2 PASS → G3+**; pivot A tylko przy fail load/lptimer |

---

## 6. Wariant A — ścieżka produktowa (szczegóły)

### 6.1 Co dodajesz

| Element | Opis | Źródło referencji | Szacunek |
|---------|------|-------------------|----------|
| `libmpsl.a` + `libnrf-802154-sl.a` | Binarki z jednego tagu nrfxlib (soft-float, nrf54l15_cpuapp) | NCS nrfxlib | 2–3 dni |
| MPSL platform layer | `mpsl_init/uninit`, `mpsl_low_priority_process`, clock, IRQ, assert | NCS `modules/lib/mpsl/` | 1–2 tyg |
| Clock unifikacja | `nrf_802154_clock_platform.c` ↔ MPSL clock API | NCS | 2–3 dni |
| FEM Skyworks | `mpsl_fem_config` + `fem_nrf54.c` | NCS + własne piny | 3–5 dni |
| `system_nrf54.c` | MPSL w main loop | — | 1–2 dni |
| CMake | `SL_OPENSOURCE OFF`, link `.a`, usuń filtry baremetal | NCS `sl/sl/CMakeLists.txt` | 2–3 dni |

### 6.2 Co usuwasz (wspólne z C)

| Plik | ~LOC | Powód |
|------|------|-------|
| `nrf_802154_rsch_baremetal.c` | ~860 | RSCH w SL-binary |
| `nrf_802154_sl_timer_baremetal.c` | ~525 | Timer w SL-binary |
| `nrf_802154_platform_sl_lptimer*.c` (workaround) | ~400+ | Port NCS zamiast bisect |
| `-DRAAL_SINGLE_PHY=1` | — | MPSL daje arbiter REM |

### 6.3 Spike A (2–5 dni)

Target: `radio-spike-nrf54l15`

```c
main()
  → mpsl_init()
  → nrf_802154_init()
  → pętla: sleep → receive → transmit (CSMA) → powtórz
```

| Metryka | Pass |
|---------|------|
| Link SL + MPSL | brak unresolved symbols |
| Flash vs POC | zmierzyć (może > +50 KB — akceptowalne jeśli < ~220 KB) |
| Radio sleep/RX/TX | sniffer widzi ramki |
| CSMA @ 0.1 s, 1000+ TX | 0× timeout / terminate_fail |

Pass → pełna integracja w `ot-rcp`.

---

## 7. Wariant C — hack bez MPSL (szczegóły)

### 7.1 Co dodajesz (zamiast MPSL)

| Element | Opis | Źródło | Szacunek |
|---------|------|--------|----------|
| `libnrf-802154-sl.a` | Ta sama binarka co w A | NCS nrfxlib | 2–3 dni |
| RAAL single-PHY stub | `nrf_raal_*` incl. `timeslot_request_with_prio` | `single_phy.c` + MPSL `nrf_raal_rem.c` jako wzór | ~100 LOC, 1–2 dni |
| mpsl_cx stub | 4× funkcje coex → no-op (Thread-only) | NCS `nrf_802154_sl_coex.c` | ~50 LOC |
| sym_* interlock | ✅ **G2 PASS** (2026-08-07) | ~100 LOC, ~~1–2 dni~~ done |
| wifi_coex stub | `nrf_802154_wifi_coex_is_enabled() → false` | NCS | 0.5 dnia |
| Platform lptimer GRTC | CC2 + CC8 + DPPI hw_task — **pełny**, bez bisect | NCS → bare-metal nrfx | 1–2 tyg |
| Platform timestamper | Cross-domain DPPI | NCS → bare-metal | 3–5 dni |

### 7.2 Od czego zacząć analizę — SL-binary, nie MPSL

W C **celowo nie linkujesz MPSL**. Kontrakt definiuje **`libnrf-802154-sl.a`**.

```bash
# Krok 1: discovery
arm-none-eabi-nm -u libnrf-802154-sl.a   # nrf54l15_cpuapp/soft-float

# Krok 2: macierz wersji — jeden tag nrfxlib → driver + SL.a

# Krok 3: spike link — czy rsch_init() przechodzi bez libmpsl.a
```

**~89 symboli zewnętrznych** (NCS nrfxlib), grupy:

| Grupa | Dostawca w C |
|-------|--------------|
| `mpsl_cx_*` (4 fn) | Własny stub |
| `nrf_raal_*` (~8 fn) | Własny single-PHY stub |
| `sym_AAFBZUDBSN44RWPA7VLGXWDL5UU6IQAP2VTRXLI` | ✅ **G2 PASS** — pusty stub na hardware (2026-08-07) |
| `nrf_802154_platform_sl_lptimer_*` (~10 fn) | Port NCS (ten sam co A) |
| `nrf_802154_platform_timestamper_*` (~5 fn) | Port NCS (ten sam co A) |
| `nrf_802154_wifi_coex_*` (~7 fn) | Stub |
| `nrf_802154_sl_atomic/mutex_*` | Część w SL, część platform |

**MPSL analizuj dopiero przy failu na `sym_*`** — wtedy pivot na A.

**Kluczowy wniosek:** RSCH (`nrf_802154_rsch_*`) jest **wewnątrz SL-binary** — C i A mają identyczny scheduler; różnica to tylko warstwa pod spodem.

### 7.3 sym_* — decydujący unknown C

Symbol zdefiniowany w `libmpsl.a`, wołany z `nrf_802154_rsch_init()`:

| Opcja | Flash | Skutek |
|-------|-------|--------|
| Pusty stub | 0 KB | ✅ **G2 PASS** (2026-08-07) — `nrf_802154_rsch_init()` bez crash na nRF54L15 DK |
| Link `libmpsl.a` | +84 KB | To już wariant A |

**Jeśli sym_* blokuje → C odpada, A wchodzi bez żalu.**

**Jeśli sym_* blokuje → C odpada, A wchodzi bez żalu.**  
**Stan 2026-08-07:** sym_* **nie blokuje** — G2 PASS z pustym stubem.

### 7.4 Spike C — wyniki i kolejne gate

Target: `radio-spike-sl-binary-nrf54l15` · build: `build-spike-c/` · skrypt: `script/spike-c-sl-binary.sh`

#### Wykonane (2026-08-07)

| Gate | Metryka | Wynik |
|------|---------|-------|
| Discovery | `nm -u libnrf-802154-sl.a` | ✅ 54 symbole zewn.; `script/analyze-sl-binary.sh` |
| **G1** | Link SL + stubs bez MPSL | ✅ brak unresolved symbols; ELF **13 696 B** (spike minimal) |
| **G2** | `nrf_802154_rsch_init()` na hardware | ✅ RTT PASS; pusty `sym_*` bez MPSL |

RTT (nRF54L15 DK, terminal 0):

```
spike-c: init lptimer/timestamper/raal
spike-c: calling nrf_802154_rsch_init()...
spike-c: PASS — rsch_init OK (sym_* gate)
```

Uwagi techniczne G1: `sym_stub.c` w executable (nie w `.a`) — kolejność linku statycznego; SL binary: `nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a`.

#### Otwarte gate

| Gate | Metryka | Pass (propozycja) |
|------|---------|-------------------|
| **G3** | `nrf_802154_init()` + TX/RX | sniffer / jedna ramka CSMA |
| **G4** | CSMA @ load, lptimer POC | 1000+ TX @ 0.1 s, 0× timeout |
| **G5** | `ot-rcp` + Morty | attach, ping, cliff |

Spike docelowy (pełny) — `main()` jak poniżej — **G3/G4 jeszcze nie zaimplementowane w spike**:

```c
main()
  → nrf_802154_platform_sl_lp_timer_init()
  → nrf_802154_platform_timestamper_init()
  → nrf_raal_init()                    // własny stub, NIE mpsl_init()
  → nrf_802154_init()
  → pętla: transmit (CSMA) @ 0.1 s → licznik timeout / terminate_fail
```

| Wynik spike C | Akcja |
|---------------|-------|
| **G1+G2 Pass** | ✅ **2026-08-07** — kontynuuj G3/G4 |
| **G3+G4 Pass** | Integracja C w `ot-rcp`, regresja Morty |
| Fail sym_* / rsch_init | ~~Pivot na A~~ — **nie dotyczy** (G2 PASS) |
| Fail CSMA / lptimer | Diagnoza GRTC; bez postępu → **pivot na A** |
| Fail FEM | **Pivot na A** (`mpsl_fem_*`) |

---

## 8. Co A i C mają wspólne (reużycie POC)

Oba warianty **nie ruszają** (~65–75% POC):

| Obszar | Pliki | Status |
|--------|-------|--------|
| OpenThread platform | `radio_nrf54.c`, `uart_nrf54.c`, `alarm_nrf54.c`, `system_nrf54.c`, … | ✅ (A: + MPSL w system) |
| Build / SDK | `nrf54l15.cmake`, nrfx 4.x, MDK, `nrfx_glue.h` | ✅ |
| Driver platform (podstawy) | irq, clock, temperature platform | ✅ |
| Debug | `nrf54_debug_stats.c` | ✅ |
| Testy | Morty, ot-daemon, cliff_debug | gate produktowy |

Oba warianty **usuwają** ten sam kod POC:

| Plik | ~LOC |
|------|------|
| `nrf_802154_rsch_baremetal.c` | ~860 |
| `nrf_802154_sl_timer_baremetal.c` | ~525 |

Oba warianty **wymagają** tego samego portu (różnica: C bardziej krytyczny):

| Obszar | A | C |
|--------|---|---|
| Platform lptimer GRTC (pełny, CC2+CC8+DPPI) | tak | **tak — gate** |
| Platform timestamper | tak | tak |
| FEM Skyworks (pełna TX, nie stub) | via MPSL | własne — trudniejsze |

---

## 9. Decyzja — kiedy A, kiedy C

### Wybierz A, jeśli:

- Flash ~180–220 KB `.text` jest akceptowalny
- Chcesz **najmniejsze ryzyko** i referencję NCS do debugowania
- FEM Skyworks musi działać produkcyjnie od razu
- QA / certyfikacja wymaga uzasadnienia „jak NCS”
- Nie chcesz spike gate — wolisz przewidywalne 3–5 tyg.

### Wybierz C (po spike), jeśli:

- **~84 KB MPSL to twardy blocker** flash (np. `.text` musi zostać ≤ ~140 KB)
- Spike C przechodzi: sym_* OK, CSMA pod load OK, lptimer OK
- Akceptujesz niewspieraną ścieżkę i własne QA
- Thread-only RCP — single-PHY wystarczy
- Masz plan B: pivot na A w 1–2 tyg. jeśli spike fail

### Macierz decyzyjna flash

| Constraint `.text` | Rekomendacja |
|--------------------|--------------|
| ≤ ~140 KB | **Spike C obowiązkowy**; A tylko jeśli C fail |
| ~140–180 KB | Spike C równolegle ze spike A; porównaj wyniki |
| ≤ ~220 KB | **A bezpośrednio** — najmniejsze ryzyko |

---

## 10. Proponowany plan pracy (A + C równolegle)

```
Tydzień 1:  [2026-08-07 — częściowo DONE]
  ├─ Macierz wersji (1 tag nrfxlib → driver + obie binarki)
  ├─ nm -u libnrf-802154-sl.a → lista zależności C          ✅
  └─ Spike C: link SL + RAAL/coex/sym stubs (bez OT, bez MPSL) ✅ G1

Tydzień 1–2: [2026-08-07 — G2 DONE]
  ├─ Spike C: rsch_init() na hardware (gate sym_*)            ✅ G2
  ├─ Spike C: CSMA @ load (gate G3/G4)                        ⏳
  ├─ Spike A: mpsl_init + link SL+MPSL + CSMA @ load (równolegle) ⏳
  └─ DECYZJA po G3/G4: zmierz flash obu spike'ów

Tydzień 2–3:
  ├─ C PASS G3+G4 + flash OK → kontynuuj C (lptimer, ot-rcp, Morty)
  └─ C FAIL lub flash A OK → pivot / start A (MPSL glue)

Tydzień 3–7:
  └─ Pełna integracja wybranego wariantu + regresja Morty
```

**Koszt równoległego spike:** ~1–2 tyg. — daje twarde dane (flash, sym_*, CSMA) zamiast spekulacji.

---

## 11. Finalna rekomendacja

| Priorytet | Wariant | Uzasadnienie |
|-----------|---------|--------------|
| **1 (domyślny)** | **A** | Oficjalny, przewidywalny, ten sam SL-binary co C, referencja NCS |
| **2 (warunkowy)** | **C** | G1/G2 PASS (2026-08-07); **G3+ wymagany** przed wyborem produktowym; sensowny jeśli ~84 KB MPSL to twardy constraint |
| **Plan B dla C** | **A** | Fail G3/G4 (lptimer/CSMA) → pivot (sym_* już nie blokuje) |

> **Strategia (stan 2026-08-07):** spike C zamknął **sym_* i link** (G1/G2). Następny krok to **G3/G4** (CSMA/load, lptimer) — dopiero potem decyzja produktowa C vs A. Równoległy spike A nadal wartościowy dla porównania flash i referencji CSMA. Jeśli flash nie jest problemem, **A wygrywa domyślnie** mimo G2 PASS dla C.

---

## 12. Jedno zdanie

**A i C dzielą ten sam produkcyjny RSCH w SL-binary; A dokupuje oficjalny MPSL (~84 KB flash, ~1–2 tyg. glue) za przewidywalność, C próbuje go zastąpić stubami — spike G1/G2 (2026-08-07) potwierdził link i init RSCH bez MPSL (sym_* OK); decyzja produktowa wymaga jeszcze G3+ (CSMA/load, lptimer, Morty).**

---

*Skonsolidowano z: nrf54l15-mpsl-sl-binary-decision.md, nrf54l15-no-mpsl-sl-binary-decision.md, nrf54l15-no-binaries-no-mpsl-decision.md.*
