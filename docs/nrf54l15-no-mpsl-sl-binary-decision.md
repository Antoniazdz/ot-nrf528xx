# nRF54L15 RCP: brak MPSL + SL-binary (bare-metal)

Notatka decyzyjna — wariant **z `libnrf-802154-sl.a`**, **bez `libmpsl.a`**, ze źródłowym driverem `nrf_802154` i cienką warstwą platformy zastępującą zależności MPSL (RAAL, coex, license interlock).

**Data:** 2026-08-07  
**Kontekst:** POC bare-metal RCP (wariant D) działa na smoke, ale ma FAIL pod load (własny RSCH ~2600 LOC). Wariant A (MPSL + SL-binary) jest oficjalny, ale dodaje ~84 KB flash. Ten wariant to **kompromis**: produkcyjny RSCH/CSMA z binarki SL, bez pełnego MPSL — **ścieżka niewspierana przez Nordic**, wymagająca analizy binarek i hacków.

Powiązane dokumenty:
- [Wariant MPSL + SL-binary (rekomendowany)](nrf54l15-mpsl-sl-binary-decision.md)
- [Wariant brak binarek + brak MPSL (obecny POC)](nrf54l15-no-binaries-no-mpsl-decision.md)
- [Handoff testy 2026-08-05](nrf54l15-handoff-2026-08-05.md)
- [Wyniki CTF 2026-08-05](nrf54l15-ctf-test-results-2026-08-05.md)
- [Migracja nRF52 → nRF54](RCP_NRF52_TO_NRF54.md)

---

## 1. Co oznacza ten wariant

| Składnik | Wariant „brak MPSL + SL-binary” |
|----------|----------------------------------|
| **Driver 802.15.4** | Źródła open source (`nrf_802154/driver/`) |
| **Service Layer** | `libnrf-802154-sl.a` (~43 KB) — zamknięty RSCH/CSMA/DTRX |
| **MPSL** | Brak — RAAL single-PHY jako własny stub (~100 LOC) |
| **Binarki Nordic** | Tylko SL (`libnrf-802154-sl.a`); bez `libmpsl.a` (~84 KB oszczędności vs A) — szczegóły §1.1 |
| **Build** | CMake bare-metal, bez Zephyra/NCS |

Oficjalna dokumentacja Nordic ([multiprotocol_support.rst](../third_party/nrf54/nordic/drivers/nrf_802154/doc/multiprotocol_support.rst)):

- **Single-PHY arbiter** → open-source SL (`sl_opensource`)
- **Pełny RSCH/CSMA** → closed-source SL-binary, **z MPSL arbiter (REM)**

Ten wariant **łamie ten podział**: bierze SL-binary (produkcyjny scheduler), ale zamiast MPSL dostarcza własne stuby RAAL i platformy. Nordic tego nie dokumentuje ani nie testuje na bare-metal nRF54L15.

### 1.1 Binarki [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib) — wariant C vs A

Pełna mapa (wszystkie pakiety nrfxlib, co **nie** linkujemy): [nrf54l15-rcp-driver-variant-comparison.md §3.1](nrf54l15-rcp-driver-variant-comparison.md#31-mapa-binarek-z-sdk-nrfxlib).

| Binarka | Wariant **C** (ten dokument) | Wariant **A** |
|---------|------------------------------|---------------|
| **`libnrf-802154-sl.a`** | ✅ link (~43 KB) | ✅ link (ta sama binarka) |
| **`libmpsl.a`** | ❌ brak (~84 KB oszczędności) | ✅ link |
| **Driver `nrf_802154`** | źródła (open) | źródła (open) |
| **MPSL platform glue** | — | ~500–1500 LOC (źródła, port NCS) |
| **Zastępniki C zamiast MPSL** | RAAL/coex/sym/wifi_coex stubs (~150 LOC) | — (dostarcza MPSL) |

**Ścieżki (nRF54L15 app, hard-float, ot-nrf54xx):**

```
# SL — w kopii _nowy w repo:
third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a

# MPSL — tylko wariant A, z NCS (nie w repo):
<ncs>/nrfxlib/mpsl/lib/nrf54l15_cpuapp/hard-float/libmpsl.a
```

**Nie linkujemy** (mimo że są w [sdk-nrfxlib](https://github.com/nrfconnect/sdk-nrfxlib)): `softdevice_controller` (BLE), `openthread` (prebuilt — my budujemy OT ze źródeł), `nrf_modem`, `nrf_wifi`, `nfc`, `crypto`, itd.

---

## 2. Stan wyjściowy

| Warstwa | Stan | Lokalizacja |
|---------|------|-------------|
| **Driver** | Źródła + lokalne patche | `third_party/nrf54/nordic/drivers/nrf_802154` |
| **SL** | `SL_OPENSOURCE=ON` + własny RSCH | `sl/sl_opensource/`, `platform/nrf_802154_rsch_baremetal.c` |
| **Kopia referencyjna** | Driver bez `.a` w repo | `third_party/nrf54/nordic/drivers/nrf_802154_nowy` |
| **MPSL** | Brak | — |
| **OpenThread RCP** | Działa (Spinel/UART) | `src/nrf54l15/radio_nrf54.c`, … |

### Rozmiar firmware (POC, wariant D — punkt odniesienia)

| Metryka | Wartość (`build-nrf54l15-uart/bin/ot-rcp`) |
|---------|---------------------------------------------|
| `.text` | ~96 KB |
| `.bss` | ~13,5 KB |
| **Binarka SL** | ~43 KB (NCS nrfxlib, nie linkowana dziś) |
| **Binarka MPSL** | ~84 KB (NCS nrfxlib, nie linkowana dziś) |

### Znane problemy POC (motywacja zmiany)

| Problem | Objaw | Test |
|---------|-------|------|
| Cliff pod load | `radio tx timeout` → RCP failure | `test_app_layer_lost_pings`, cliff_debug |
| CSMA terminate | `tx_core_deny_terminate_fail` rośnie | ping 1000 B @ 0.1 s |
| Sustained TX | crash RCP po UDP/iperf | IPv6 fragmentation |

**Przyczyna:** własny RSCH/timer (~2600 LOC w `platform/`) zamiast zamkniętego SL.

---

## 3. Architektura wariantu

```
┌─────────────────────────────────────────────────────────┐
│  OpenThread (ot-rcp, Spinel, HDLC)          ✅ bez zmian │
├─────────────────────────────────────────────────────────┤
│  otPlat* (radio_nrf54.c, UART, alarm, …)    ✅ bez zmian │
├─────────────────────────────────────────────────────────┤
│  Build: CMake, nrfx, MDK, startup           ✅ bez zmian │
├─────────────────────────────────────────────────────────┤
│  nrf_802154 driver (źródła)                 ✅ bez zmian │
├─────────────────────────────────────────────────────────┤
│  libnrf-802154-sl.a (~43 KB)                🆕 binarka   │
│    RSCH/CSMA/DTRX/timestamp WEWNĄTRZ .a                  │
├─────────────────────────────────────────────────────────┤
│  RAAL single-PHY stub (~100 LOC)            🆕 hack      │
│  mpsl_cx + wifi_coex stub (~50 LOC)         🆕 hack      │
│  sym_* stub / license interlock             ⚠️ ryzyko    │
│  platform lptimer GRTC + timestamper        🔧 port NCS  │
├─────────────────────────────────────────────────────────┤
│  libmpsl.a                                  ❌ brak      │
│  własny rsch_baremetal.c (~860 LOC)         ❌ usuń     │
│  własny sl_timer_baremetal.c (~525 LOC)     ❌ usuń     │
└─────────────────────────────────────────────────────────┘
```

Przy `SL_OPENSOURCE=OFF` driver włącza pełną funkcjonalność (CSMA, DTRX, timestamp, IFS) — w przeciwieństwie do stub SL, który je wyłącza.

---

## 4. Profity takiego rozwiązania

### Stabilność radia (główny profit vs POC)

- Produkcyjny RSCH/CSMA Nordic **w binarce SL** — ten sam kod co w NCS/Zephyr
- Delayed TRX, timestamping, precyzyjny timing ACK — w SL-binary, nie w stub
- Usunięcie ~1400 LOC własnego RSCH/timer — główny blocker POC
- Brak reverse-engineeringu schedulera (w przeciwieństwie do wariantu D)

### Pamięć (główny profit vs wariant A)

- **Brak `libmpsl.a`** (~84 KB) — oszczędność flash względem MPSL + SL-binary
- Tylko jedna binarka Nordic do linkowania i wersjonowania
- Prostszy main loop — brak `mpsl_init()`, `mpsl_low_priority_process()`
- Brak buforów/stanu MPSL w RAM

### Bare-metal i workflow

- Bez Zephyra, bez NCS workflow — CMake + `arm-none-eabi-gcc`
- Własny `ot-rcp`, ot-daemon/Spinel, Morty — jak ot-nrf528xx
- Thread-only RCP: single-PHY wystarczy (brak BLE/multiprotocol na tym chipie)

### Reużycie POC (~65–75%)

- Cały szkielet OT RCP, nrfx 4.x, MDK, startup, linker
- Częściowy port GRTC lptimer już w repo
- Wiedza o clock XO/LFCLK, debug stats, testy Morty

---

## 5. Co można wykorzystać vs co nowe

### Już jest i zostaje (~65–75% nakładu POC)

| Obszar | Pliki / komponenty | Status |
|--------|-------------------|--------|
| OpenThread platform | `radio_nrf54.c`, `uart_nrf54.c`, `alarm_nrf54.c`, `system_nrf54.c`, `crypto_nrf54.c`, … | ✅ gotowe |
| Build | `nrf54l15.cmake`, `arm-none-eabi.cmake`, `third_party/nrf54/CMakeLists.txt` | ✅ modyfikacja linków |
| SDK | `nordicsemi-nrf54l15-sdk` (nrfx clock, GRTC, GPPI), MDK, `nrfx_glue.h` | ✅ gotowe |
| Driver platform (podstawy) | `nrf_802154_irq_platform.c`, `nrf_802154_clock_platform.c`, `nrf_802154_temperature_platform.c` | ✅ gotowe |
| GRTC lptimer (częściowy) | `platform/nrf_802154_platform_sl_lptimer.c`, `*_grtc_hw_task.c` | ⚠️ bisect, CC8 off |
| FEM stub | `sl_opensource/nrf_802154_sl_fem.c` (stuby `mpsl_fem_*`) | ⚠️ no-op |
| Debug | `nrf54_debug_stats.c` | ✅ pomaga w diagnozie |

### Usunąć (zastępuje SL-binary)

| Plik | ~LOC | Powód |
|------|------|-------|
| `nrf_802154_rsch_baremetal.c` | ~860 | RSCH w SL-binary |
| `nrf_802154_sl_timer_baremetal.c` | ~525 | Timer w SL-binary |
| Filtry CMake wykluczające rsch/timer ze SL | — | Niepotrzebne przy binarce |

### Nowe / do domknięcia (~25–35% — tu leży ryzyko)

| Obszar | Co trzeba zrobić | Źródło referencji | Szacunek |
|--------|------------------|-------------------|----------|
| **Analiza binarek** | `nm -u libnrf-802154-sl.a` → lista symboli zewnętrznych | NCS nrfxlib | ✅ 2026-08-07 |
| **Spike link** | CMake target bez OT, minimal stubs | `build-spike-c/`, `script/spike-c-sl-binary.sh` | ✅ G1 2026-08-07 |
| **Gate G2 hardware** | `nrf_802154_rsch_init()` + sym_* | RTT na nRF54L15 DK | ✅ 2026-08-07 |
| **RAAL single-PHY** | `nrf_raal_timeslot_request_with_prio` + reszta API | `mrt-802.15.4-driver/.../single_phy.c` + MPSL `nrf_raal_rem.c` | ~100 LOC, 1–2 dni |
| **mpsl_cx stub** | 4 funkcje coex (Thread-only → no-op) | `sl_opensource/nrf_802154_sl_coex.c` | ~50 LOC, 0.5 dnia |
| **sym_* interlock** | Symbol licencyjny wołany z `nrf_802154_rsch_init()` | ✅ G2 PASS — pusty stub | ~~0–2 dni~~ done | ⚠️ do zweryfikowania |
| **Platform lptimer GRTC** | CC2 + CC8 + DPPI hw_task — pełna wersja | NCS `nrf_802154_platform_sl_lptimer_grtc.c` | 1–2 tyg |
| **Platform timestamper** | Cross-domain DPPI dla timestampów ramek | NCS `nrf_802154_platform_timestamper.c` | 3–5 dni |
| **CMake** | `SL_OPENSOURCE OFF`, link `.a`, binarki w repo | NCS `sl/sl/CMakeLists.txt` | 2–3 dni |
| **FEM Skyworks** | Pełna konfiguracja TX (nie stub) | Własna lub częściowe API MPSL FEM | 3–5 dni |
| **Regresja Morty** | cliff, ping pass rate, fragmentation | — | 1–2 tyg |

---

## 6. Analiza binarek — od czego zacząć

**Rekomendacja: zacząć od SL-binary, nie od MPSL.**

Analiza `arm-none-eabi-nm -u libnrf-802154-sl.a` (nrf54l15_cpuapp/soft-float, NCS nrfxlib) pokazuje **89 zewnętrznych symboli**. SL-binary **nie wymaga całego MPSL** — tylko konkretnych interfejsów:

| Grupa | Symbole | Dostawca docelowy |
|-------|---------|-------------------|
| **MPSL coex** | `mpsl_cx_request`, `mpsl_cx_release`, `mpsl_cx_register_callback`, `mpsl_cx_granted_ops_get` | Własny stub (Thread-only) |
| **RAAL (arbiter)** | `nrf_raal_init/uninit`, `nrf_raal_continuous_*`, `nrf_raal_timeslot_request_with_prio`, `nrf_raal_timeslot_us_left_get` | Własny single-PHY stub |
| **License interlock** | `sym_AAFBZUDBSN44RWPA7VLGXWDL5UU6IQAP2VTRXLI` | Zdefiniowany w `libmpsl.a`; wołany z `nrf_802154_rsch_init()` |
| **Platform lptimer** | `nrf_802154_platform_sl_lptimer_*` (~10 funkcji) | Port z NCS → bare-metal nrfx GRTC |
| **Platform timestamper** | `nrf_802154_platform_timestamper_*` (~5 funkcji) | Port z NCS → bare-metal |
| **Wifi coex** | `nrf_802154_wifi_coex_*` (~7 funkcji) | Stub (`coex_is_enabled() == false`) |
| **Atomics/mutex** | `nrf_802154_sl_atomic_*`, `nrf_802154_sl_mutex_*` | Część w SL `.a`, część platform |

**Kluczowy wniosek:** pełny RSCH (`nrf_802154_rsch_*`) **jest wewnątrz SL-binary** — to dokładnie to, czego brakuje w POC.

### Potencjalne problemy (nie wiadomo bez spike)

| Problem | Opis | Jak sprawdzić |
|---------|------|---------------|
| **sym_* interlock** | SL woła symbol z `libmpsl.a` przy init RSCH — możliwy runtime check licencji | ✅ **G2 PASS** — pusty stub, brak crash na nRF54L15 DK |
| **Niewspierana ścieżka** | Nordic nie testuje SL-binary bez MPSL na bare-metal | Brak referencji NCS — tylko własny QA |
| **GRTC CC budget** | SL-binary ma inne wymagania timingowe niż stub (CC8+DPPI) | Spike CSMA pod load; mapa CC: OT vs SL |
| **Wersjonowanie** | Driver + SL muszą być z jednego tagu nrfxlib | Macierz wersji przed integracją |
| **FEM bez MPSL** | Driver woła `mpsl_fem_cleanup()` — stuby mogą nie wystarczyć dla Skyworks | Test TX power na hardware |
| **Peripheral conflicts** | Bez MPSL brak `MPSL_RESERVED_*` — ryzyko kolizji PPI/DPPI | Porównanie mapy peryferiów z NCS |

---

## 7. Wymagane hacki (skrót)

### 7.1 RAAL single-PHY (zastępuje REM z MPSL)

Stary `single_phy.c` z mrt-802.15.4-driver **nie wystarcza** — brakuje `nrf_raal_timeslot_request_with_prio`. Trzeba rozszerzyć do ~100 LOC: zawsze grant timeslotu, `timeslot_us_left_get() → UINT32_MAX`.

### 7.2 Stuby MPSL coex + wifi coex

4× `mpsl_cx_*` + `nrf_802154_wifi_coex_is_enabled() → false`. Wzorzec: NCS `nrf_802154_sl_coex.c`.

### 7.3 License interlock `sym_*`

Symbol zdefiniowany w `libmpsl.a`, niewyjaśniony w dokumentacji. Opcje:

| Opcja | Flash | Ryzyko |
|-------|-------|--------|
| Pusty stub | 0 KB | ✅ **G2 PASS** (2026-08-07) — `rsch_init()` OK na hardware bez MPSL |
| Link całego `libmpsl.a` | +84 KB | To już wariant A — duplikat RAAL |

**Gate spike:** jeśli pusty stub powoduje crash lub undefined behaviour → wariant C odpada.

### 7.4 Platform GRTC lptimer + timestamper

Port z NCS (`nrf/modules/nrfxlib/nrf_802154/sl/platform/`) z Zephyr API na bare-metal nrfx GRTC. POC ma częściowy port z bisect (`NRF54_LPTIMER_CC2_ONLY_BISECT=1`) — SL-binary wymaga **pełnej** wersji (CC2 + CC8 + DPPI hw_task).

---

## 8. Nakład pracy i czas implementacji

### Już włożone (POC)

| Obszar | Efekt |
|--------|-------|
| Bare-metal build + CMake | ✅ fundament |
| Platform OT + RCP | ✅ smoke OK |
| Własny RSCH/timer/lptimer | ⚠️ ~2600 LOC — FAIL pod load |
| Częściowy port GRTC lptimer | ⚠️ bisect, niepełny |
| Testy Morty + debug | część PASS, znane FAIL |

### Do domknięcia (szacunek — **średnia niepewność**)

| Etap | Opis | Czas |
|------|------|------|
| Analiza binarek (`nm`, macierz wersji) | Discovery — lista wymagań SL | 1–2 dni |
| Spike link (SL + stubs, bez OT) | Czy build i `rsch_init()` przechodzą | 2–3 dni |
| RAAL + coex + sym stubs | Minimalna warstwa zastępująca MPSL | 2–3 dni |
| Port lptimer GRTC (pełny) | CC2+CC8+DPPI z NCS → nrfx | 1–2 tyg |
| Port timestamper | Cross-domain DPPI | 3–5 dni |
| CMake + integracja SL-binary | `SL_OPENSOURCE OFF`, binarki w repo | 2–3 dni |
| Usunięcie `*_baremetal.c` | Cleanup po pozytywnym spike | 1–2 dni |
| FEM Skyworks | Pełna konfiguracja TX | 3–5 dni |
| Regresja Morty | cliff, ping pass rate, fragmentation | 1–2 tyg |
| **Razem (optimistycznie)** | | **~3–4 tyg** |
| **Razem (realistycznie)** | Jeśli sym_* / lptimer / spike fail | **~4–6 tyg** lub pivot na A |

**Kluczowa różnica vs wariant A:** brak MPSL platform glue (~1–2 tyg), ale **więcej ryzyka** na hackach i lptimer.

**Kluczowa różnica vs wariant D:** binarka SL zamiast własnego RSCH — krótsza droga do stabilności, jeśli spike przejdzie.

---

## 9. Spike decyzyjny (obowiązkowy przed pełną integracją)

**Cel:** odpowiedź „czy wariant C w ogóle da się zlinkować i uruchomić” w **1–2 tygodnie**, bez pełnego OT.

### Co budować

Osobny target CMake, np. `radio-spike-sl-binary-nrf54l15`:

```c
main()
  → nrf_802154_platform_sl_lp_timer_init()
  → nrf_802154_platform_timestamper_init()
  → nrf_raal_init()                    // własny stub
  → nrf_802154_init()
  → pętla: transmit (CSMA) @ 0.1 s → licznik timeout / terminate_fail
```

Bez OpenThread, bez Spinel, bez MPSL.

### Co mierzyć

| Metryka | Pass (propozycja) |
|---------|-------------------|
| Link SL + stubs | brak unresolved symbols |
| `nrf_802154_rsch_init()` | bez crash (sym_* OK) |
| Flash delta vs POC | < ~50 KB (POC ~96 KB + SL ~43 KB − usunięty RSCH ~20 KB) |
| RAM | brak dużego skoku (> ~10 KB) |
| 1000+ TX CSMA @ 0.1 s | 0× `radio tx timeout` |
| `tx_core_deny_*` | nie rośnie |
| 120 s sustained | bez RCP failure |

**Gate spike:** ✅ **G2 PASS** (2026-08-07) — pusty stub wystarcza; wariant C **nie odpada** na sym_*.

### Decyzja (stan po G1/G2)

| Wynik spike | Akcja |
|-------------|-------|
| Pass (link + rsch_init + CSMA OK) | Kontynuuj wariant C; integracja w `ot-rcp` |
| Fail na sym_* / rsch_init | **Pivot na wariant A** (MPSL + SL-binary) |
| Fail na CSMA pod load | Diagnoza lptimer/GRTC; jeśli bez postępu → pivot na A |
| Fail na FEM | Rozważ wariant A (pełne `mpsl_fem_*`) |

| Gate | Status | Data | Uwagi |
|------|--------|------|-------|
| Discovery (`nm -u`) | ✅ PASS | 2026-08-07 | 54 symbole zewn.; skrypt `script/analyze-sl-binary.sh` |
| **G1** link SL + stubs | ✅ PASS | 2026-08-07 | `sym_stub.c` w executable (kolejność linku `.a`) |
| **G2** `rsch_init()` hardware | ✅ PASS | 2026-08-07 | RTT: pusty `sym_*` OK; bez `libmpsl.a` |
| G3 CSMA / TX pod load | ⏳ | — | spike rozszerzony lub `ot-rcp` |
| G4 pełny lptimer GRTC | ⏳ | — | POC minimal stub w spike |
| G5 regresja Morty | ⏳ | — | po integracji `ot-rcp` |

### 9.1 Ryzyko C vs A przy gotowym lptimerze

Skrót resztkowego ryzyka wariantu C względem A, **gdy port lptimer GRTC (CC2+CC8+DPPI) i timestamper są gotowe** (wspólne dla obu wariantów). Szczegóły: [nrf54l15-rcp-driver-variant-comparison.md §4.4.1](nrf54l15-rcp-driver-variant-comparison.md#441-ryzyko-c-vs-a-przy-gotowym-lptimerze).

**Nie ryzyko C (gate zamknięte):** G1 link, G2 `sym_*` @ init, identyczny RSCH/CSMA w SL-binary.

| Priorytet | Ryzyko C | A unika przez |
|-----------|----------|---------------|
| **1** | FEM / TX power (Skyworks) | `libmpsl.a` → `mpsl_fem_*` |
| **2** | Kolizje DPPI/PPI z OT + driver | `MPSL_RESERVED_*` + check w `peripherals_alloc.c` |
| **3** | RAAL stub (single-PHY) | MPSL REM |
| **4** | Clock glue | MPSL clock API |
| **5** | `sym_*` poza init | prawdziwy MPSL |
| **6** | Coex (Wi‑Fi/BLE) | MPSL `mpsl_cx_*` (Thread-only: stub OK) |
| **7** | Proces (Nordic support, upgrade, QA, multiprotocol) | oficjalna ścieżka NCS |

**Produkt:** DK bez FEM → C możliwe po G3/G5; **Skyworks FEM → rozważ A** nawet z idealnym lptimerem.

---

## 10. Plusy i minusy jako produkt

### Plusy (+)

| | |
|--|--|
| **Stabilność radia** | Produkcyjny RSCH/CSMA z SL-binary — docelowo brak cliff (do weryfikacji) |
| **Mniejszy flash niż A** | Brak ~84 KB MPSL; szac. ~139 KB `.text` vs ~223 KB przy MPSL+SL |
| **Bare-metal** | Bez Zephyra/NCS — prosty build i runtime |
| **Usunięcie własnego RSCH** | ~1400 LOC wysokiego ryzyka out |
| **Jedna binarka Nordic** | Tylko SL do wersjonowania (vs SL + MPSL) |
| **Prostszy main loop** | Brak `mpsl_low_priority_process()` |
| **Reużycie POC** | ~65–75% kodu zostaje |
| **Thread-only RCP** | Single-PHY wystarczy — scope produktu OK |
| **Pełne featury drivera** | CSMA, DTRX, timestamp włączone (vs stub SL) |

### Minusy (−)

| | |
|--|--|
| **Niewspierane przez Nordic** | Brak referencji bare-metal SL-binary bez MPSL |
| **Nie wiadomo czy się da** | Wymaga analizy binarek i spike — **gate przed inwestycją** |
| **sym_* interlock** | Ryzyko licencyjne / runtime — nieudokumentowane |
| **Platform lptimer** | Port z NCS (~1–2 tyg) — ta sama trudność co POC, inne wymagania SL |
| **FEM / TX power** | Bez pełnego MPSL — więcej własnej pracy |
| **Binarka zamknięta** | `libnrf-802154-sl.a`, licencja Nordic-5-Clause |
| **Debug RSCH** | W `.a` — tylko przez referencję NCS |
| **Pin wersji** | Driver + SL zsynchronizowane z tagiem nrfxlib |
| **Brak upgrade path MPSL** | Poprawki arbitera clock/FEM tylko przez własne stuby |
| **Ryzyko certyfikacji / QA** | Trudniejsze uzasadnienie vs referencja NCS (wariant A) |
| **Czas niepewny** | 3–6 tyg. vs ~3–5 tyg. wariant A — **wyższe ryzyko** |

---

## 11. Co daje bare-metal w tym formacie

| Aspekt | Efekt |
|--------|-------|
| **Flash** | Średni wariant: POC ~96 KB + SL ~43 KB − usunięty RSCH ~20 KB ≈ **~120–140 KB** `.text` (do zmierzenia) |
| **Flash vs A** | **~84 KB mniej** niż MPSL + SL-binary |
| **Flash vs D (POC)** | **~25–45 KB więcej** niż obecny stub — ale bez własnego RSCH |
| **RAM** | ~14 KB BSS POC + delta SL (~5–10 KB?) — **bez buforów MPSL** |
| **Boot** | Krótszy init — brak `mpsl_init()` |
| **CPU w idle** | Brak `mpsl_low_priority_process()` co tick |
| **Binarki** | 1× Nordic (SL) zamiast 2× (SL + MPSL) |
| **Licencja** | Nordic-5-Clause na SL; brak MPSL |
| **Produkt RCP Thread-only** | Wystarczający scope — **po pozytywnym spike** |

**Podsumowanie bare-metal:** wariant C to **kompromis pamięci** między lekkim stubem (D) a pełnym stackiem Nordic (A). Płaci ~43 KB flash za produkcyjny scheduler, ale unika ~84 KB MPSL i ~1400 LOC własnego RSCH.

---

## 12. Porównanie wariantów (tabela końcowa)

| | POC (D) | **C: brak MPSL + SL-bin** | A: MPSL + SL-bin | NCS/Zephyr |
|--|---------|---------------------------|------------------|------------|
| Bare-metal build | tak | **tak** | tak | nie |
| Flash `.text` (szac.) | ~96 KB | **~120–140 KB** | ~180–220 KB | największy |
| Binarki Nordic | 0 | **1** (SL) | 2 (SL+MPSL) | 2+ |
| Własny RSCH | ~2600 LOC | **~150 LOC** (stuby) | 0 | 0 |
| CSMA/DTRX/timestamp | wył./stub | **pełne** | pełne | pełne |
| Stabilność pod load | FAIL | **do weryfikacji** | docelowa dobra | dobra |
| Wsparcie Nordic | brak | **brak (hack)** | tak | tak |
| Pewność implementacji | niska | **średnia (spike gate)** | wysoka | wysoka |
| Czas do produktu | 3–6 mies.(?) | **3–6 tyg.(?)** | 3–5 tyg. | migracja buildu |
| Reużycie POC | częściowe | **wysokie** | wysokie | niskie |

---

## 13. Kiedy ten wariant ma sens

### Ma sens jako:

- **Spike / eksploracja** — 1–2 tyg. analizy binarek + link test; niski koszt, wysoka wartość informacji
- **Produkt z twardym limitem flash** — jeśli ~84 KB MPSL to blocker, a spike C przechodzi
- **Most między POC a wariantem A** — szybsza ścieżka do stabilnego CSMA niż debug własnego RSCH (wariant D)
- **Thread-only RCP** — bez BLE, bez multiprotocol

### Nie ma sensu jako:

- **Cel produktowy bez spike** — nie wiadomo czy sym_* / lptimer / link przejdą
- **Substytut wariantu A** — jeśli spike failuje, pivot na MPSL+SL bez żalu
- **Produkt z wymogiem wsparcia Nordic** — brak referencji na tę ścieżkę
- **Multiprotocol** — bez MPSL brak dynamic arbitera

---

## 14. Rekomendacja

Dla **produktu finalnego RCP nRF54L15** w modelu ot-nrf54xx:

> **Wariant A (MPSL + SL-binary) pozostaje rekomendowany** jako cel produktowy — oficjalny, wspierany, przewidywalny czas (~3–5 tyg.).

> **Wariant C (brak MPSL + SL-binary)** — spike G1/G2 **PASS** (2026-08-07): link bez MPSL i `rsch_init()` z pustym `sym_*` na hardware OK. **Nie odpada** na interlock licencyjny. Nadal wymaga G3+ (CSMA/load, lptimer) przed decyzją produktową vs A.

- Daje produkcyjny RSCH bez własnego schedulera (~1400 LOC mniej niż POC)
- Oszczędza ~84 KB flash vs A — istotne jeśli pamięć to twardy constraint
- POC ma już częściowy port lptimer GRTC — nie start od zera
- **G1/G2 zamknięte** — sym_* zweryfikowany; fail na G3/G4 → pivot na A nadal możliwy

**Kolejność pracy (wariant C):**

1. ~~Macierz wersji (jeden tag nrfxlib → driver + SL)~~ ✅ discovery
2. ~~`nm -u libnrf-802154-sl.a` → dokumentacja zależności (sekcja 6)~~ ✅ 2026-08-07
3. ~~Spike link bez OT (sym_*, RAAL, coex stubs)~~ ✅ G1 2026-08-07
4. ~~Gate G2: `rsch_init()` na hardware~~ ✅ 2026-08-07
5. Port lptimer GRTC + timestamper (pełny, bez bisect)
6. Test CSMA pod load (gate G3/G4)
7. Decyzja: C vs pivot na A
8. Jeśli PASS G3+ → integracja `ot-rcp`, usuń `*_baremetal.c`, regresja Morty

---

## 16. Wyniki spike variant C (2026-08-07)

Spike gate **G1 + G2** wykonany na nRF54L15 DK (bare-metal, bez OpenThread, bez MPSL).

### Artefakty

| Element | Ścieżka / wartość |
|---------|-------------------|
| Target CMake | `radio-spike-sl-binary-nrf54l15` |
| Build dir | `build-spike-c/` |
| Skrypt build + G1 | `script/spike-c-sl-binary.sh` |
| Analiza binarki | `script/analyze-sl-binary.sh` |
| SL binary | `third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a` (43 814 B) |
| Spike kod | `third_party/nrf54/spike_c/` (stuby RAAL/coex/sym, `main_spike.c`) |
| Toolchain | `arm-none-eabi`, hard-float, Cortex-M33 |

### G1 — link (PASS)

- `libnrf-802154-sl.a` + stuby + platform POC lptimer/timestamper — **brak unresolved symbols**
- 54 symboli zewnętrznych z `nm -u` — wszystkie rozwiązane (stuby, platform, driver)
- **Fix linkera:** `sym_AAFBZUDBSN44RWPA7VLGXWDL5UU6IQAP2VTRXLI` referencjonowany tylko z SL `.a` (po skanowaniu archiwum stubów) — `sym_stub.c` skompilowany **bezpośrednio w executable**, nie w `libnrf54-spike-c-stubs.a`
- Rozmiar ELF: **text 11 620 B, data 388 B, bss 1 688 B, razem 13 696 B** (minimalny spike, bez OT)

### G2 — hardware `rsch_init()` (PASS)

Flash: `nrfjprog --chiperase --program build-spike-c/bin/radio-spike-sl-binary-nrf54l15.hex --reset`  
RTT Viewer, terminal 0, target **NRF54L15_M33**:

```
spike-c: init lptimer/timestamper/raal
spike-c: calling nrf_802154_rsch_init()...
spike-c: PASS — rsch_init OK (sym_* gate)
```

- **`sym_*` z pustym stubem** (`void sym_...(int license)`) — **brak HardFault**
- Wołanie z binarki SL: `nrf_raal_init()` → `sym_*(1)` → `nrf_802154_sl_mutex_init()` (disassembly `nrf_802154_rsch_init`)
- Po PASS firmware śpi w `while(1) { wfi; }` — brak dalszego logu RTT to oczekiwane zachowanie

### Wnioski po G1/G2

| Pytanie spike | Odpowiedź |
|---------------|-----------|
| Czy SL-binary linkuje się bez MPSL? | **Tak** |
| Czy pusty stub `sym_*` wystarcza na init RSCH? | **Tak** (hardware) |
| Czy wariant C odpada na sym_*? | **Nie** — pivot na A z tego powodu **nie jest wymagany** |
| Czy to dowód produkcyjnej stabilności? | **Nie** — tylko init; G3+ (CSMA/TX load, lptimer pełny, Morty) nadal otwarte |

### Następne gate (przed integracją `ot-rcp`)

| Gate | Pytanie |
|------|---------|
| **G3** | Czy SL RSCH + stub RAAL daje sensowny TX/RX (np. `nrf_802154_init()` + jedna ramka)? |
| **G4** | Czy POC lptimer wystarczy pod timing RSCH / CSMA pod load? |
| **G5** | Czy `ot-rcp` z wariantem C przechodzi attach + ping + cliff testy? |

---

## 15. Jedno zdanie

**Brak MPSL + SL-binary to kompromis: produkcyjny scheduler Nordic w ~43 KB binarki zamiast ~2600 LOC własnego RSCH, ale bez oficjalnego wsparcia — spike G1/G2 (2026-08-07) potwierdził link i `rsch_init()` z pustym sym_* bez MPSL; pełna ścieżka produktowa wymaga jeszcze G3+ (CSMA/load, lptimer, Morty), a wariant A (MPSL+SL) pozostaje bezpieczniejszą alternatywą.**
