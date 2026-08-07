# nRF54L15 RCP: decyzja MPSL + SL-binary (bare-metal)

Notatka decyzyjna — porównanie wariantów integracji oficjalnego drivera `nrf_802154` dla produkcyjnego RCP na nRF54L15 w modelu ot-nrf54xx (bez Zephyra/NCS).

**Data:** 2026-08-07  
**Kontekst:** POC bare-metal RCP działa (smoke, część testów Morty PASS), ale ma znane problemy stabilności radia pod load. Oficjalny driver z [sdk-nrfxlib/nrf_802154](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_802154) wymaga binarek SL i MPSL.

Powiązane dokumenty:
- [Wariant brak MPSL + SL-binary (kompromis flash)](nrf54l15-no-mpsl-sl-binary-decision.md)
- [Wariant brak binarek + brak MPSL (obecny POC)](nrf54l15-no-binaries-no-mpsl-decision.md)
- [Handoff testy 2026-08-05](nrf54l15-handoff-2026-08-05.md)
- [Wyniki CTF 2026-08-05](nrf54l15-ctf-test-results-2026-08-05.md)
- [Migracja nRF52 → nRF54](RCP_NRF52_TO_NRF54.md)
- [POC scope (KRKNWK)](../docs/KRKNWK-Proof%20of%20Concept_%20nRF54L%20Support%20in%20the%20ot-nrf54xx%20Line%20of%20Ports-090726-062109-1.md)

---

## 1. Stan wyjściowy (obecny POC)

| Warstwa | Stan | Lokalizacja |
|---------|------|-------------|
| **Driver 802.15.4** | Źródła (oficjalny `nrf_802154`) | `third_party/nrf54/nordic/drivers/nrf_802154` |
| **Service Layer** | `SL_OPENSOURCE=ON` + własne stuby RSCH/timer | `platform/nrf_802154_rsch_baremetal.c`, `nrf_802154_sl_timer_baremetal.c` |
| **MPSL** | Brak | — |
| **Arbiter radia** | `RAAL_SINGLE_PHY=1`, własny RSCH | `third_party/nrf54/CMakeLists.txt` |
| **OpenThread RCP** | Działa (Spinel/UART, ot-daemon) | `src/nrf54l15/radio_nrf54.c`, … |
| **Kopia referencyjna drivera** | Niekompletna (brak `.a`) | `third_party/nrf54/nordic/drivers/nrf_802154_nowy` |

### Co działa

- Build bare-metal (CMake, nrfx 4.x, MDK, startup)
- Smoke sieci Thread, część testów Morty PASS
- OpenThread platform (UART, alarm GRTC, crypto, system)

### Znane problemy (blokery produktowe)

| Problem | Objaw | Test |
|---------|-------|------|
| Cliff pod load | `radio tx timeout` → RCP failure → `RecoverFromRcpFailure()` | `test_app_layer_lost_pings`, cliff_debug |
| CSMA terminate | `tx_core_deny_terminate_fail` rośnie pod batch ping | ping 1000 B @ 0.1 s |
| Fragmentacja / iperf | crash RCP po sustained UDP | IPv6 fragmentation, iperf |

**Przyczyna:** driver źródłowy OK, ale **zastąpiono zamknięty SL + MPSL własnymi stubami RSCH** (~1400 LOC), które nie dowodzą stabilności pod obciążeniem.

---

## 2. Warianty do porównania (bare-metal)

| Wariant | Opis | Produkt? |
|---------|------|----------|
| **A. MPSL + SL-binary** | Oficjalny stack Nordic (rekomendowany) | **Tak** |
| B. MPSL + SL-opensource | Stub RSCH (Nordic: „basic arbiter”) | Nie |
| C. Brak MPSL + SL-binary | Prawdopodobnie niewspierane (SL-binary oczekuje MPSL) | Raczej nie |
| **D. Brak MPSL + SL-opensource** | **Obecny POC** | Nie (demo/POC) |

Skupienie analizy: **wariant A**.

---

## 3. Architektura docelowa (wariant A)

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
│  libnrf-802154-sl.a                         🆕 binarka   │
├─────────────────────────────────────────────────────────┤
│  libmpsl.a + MPSL platform glue              🆕 nowe     │
├─────────────────────────────────────────────────────────┤
│  własny RSCH/timer/lptimer (baremetal)       ❌ usuń     │
└─────────────────────────────────────────────────────────┘
```

**Ważne:** MPSL ≠ Zephyr/NCS. To biblioteka `.a` z nrfxlib — linkowana do firmware bez kernela, bez `west`, bez device tree.

Oficjalna architektura drivera ([architecture.rst](../third_party/nrf54/nordic/drivers/nrf_802154/doc/architecture.rst)): driver współpracuje z arbitrem radia w MPSL.

---

## 4. Profity wersji docelowej (produkt finalny)

### Stabilność radia (główny profit)

- Oficjalny RSCH/CSMA Nordic zamiast własnych stubów
- Delayed TRX, timestamping, precyzyjny timing ACK
- MPSL jako arbiter zegarów i peryferiów (PPI/DPPI, HFCLK, FEM)

### Zachowanie modelu bare-metal

- Bez Zephyra, bez NCS workflow — CMake + `arm-none-eabi-gcc`
- Własny `ot-rcp`, ot-daemon/Spinel
- Ten sam styl dev co ot-nrf528xx i Morty

### Reużycie inwestycji POC (~60–70% kodu)

- nrfx 4.x, MDK, startup, linker, CMake
- `radio_nrf54.c`, UART, alarm, crypto, entropy, system
- Testy CTF/Morty, wiedza o GRTC, clock XO/LFCLK

### Utrzymanie długoterminowe

- Upgrade: driver + MPSL + SL z jednego tagu nrfxlib
- Usunięcie ~1400 LOC własnego RSCH/timer (wysokie ryzyko)
- Debug przez referencję NCS, nie reverse-engineering własnego schedulera

---

## 5. Co się przyda z obecnej platformy (nie od zera)

### Zostaje praktycznie bez zmian (~60–70%)

| Obszar | Pliki / komponenty |
|--------|-------------------|
| OpenThread platform | `radio_nrf54.c`, `uart_nrf54.c`, `alarm_nrf54.c`, `system_nrf54.c`, `crypto_nrf54.c`, `entropy_nrf54.c`, `temp_nrf54.c`, `misc_nrf54.c` |
| Konfiguracja OT | `openthread-core-nrf54l15-config*.h`, `platform-config.h` (+ nowe define) |
| Build | `nrf54l15.cmake`, `arm-none-eabi.cmake`, `third_party/nrf54/CMakeLists.txt` (modyfikacja flag/linków) |
| SDK | `nordicsemi-nrf54l15-sdk` (nrfx clock, GRTC, GPPI), MDK, CMSIS, `nrfx_glue.h` |
| Driver platform (część) | `nrf_802154_irq_platform.c`, `nrf_802154_temperature_platform.c`, `nrf_802154_clock_callbacks.c` |

### Wymaga przeróbki (~15–20%)

| Plik | Zmiana |
|------|--------|
| `nrf_802154_clock_platform.c` | Unifikacja z MPSL clock API |
| `fem_nrf54.c` | Dziś stub → konfiguracja Skyworks via `mpsl_fem_*` |
| `system_nrf54.c` | `mpsl_init()`, `mpsl_low_priority_process()` w pętli |
| `third_party/nrf54/CMakeLists.txt` | `SL_OPENSOURCE OFF`, link binarek, usuń filtry baremetal |

### Usunąć (~1400 LOC — zastępuje SL-binary + MPSL)

| Plik | ~LOC | Powód |
|------|------|-------|
| `nrf_802154_rsch_baremetal.c` | ~860 | RSCH w SL-binary |
| `nrf_802154_sl_timer_baremetal.c` | ~525 | Timer w SL-binary |
| `nrf_802154_platform_sl_lptimer*.c` | ~400+ | Workaround pod stub RSCH |
| `-DRAAL_SINGLE_PHY=1` | — | Zastępuje MPSL arbiter |

---

## 6. Co trzeba nowo zaimplementować

### MPSL platform layer (nowy katalog, np. `third_party/nrf54/mpsl/platform/`)

Port z referencji NCS (`modules/lib/mpsl/`), szacunek **5–10 plików**, ~500–1500 LOC:

| Obszar | API / funkcja |
|--------|----------------|
| Init | `mpsl_init()`, `mpsl_uninit()` |
| Main loop | `mpsl_low_priority_process()` |
| Clock | Spięcie z nrfx XO/LFCLK |
| IRQ | Priorytety zgodne z `NRF_802154_SL_RTC_IRQ_PRIORITY` |
| FEM | `mpsl_fem_config` dla Skyworks |
| Assert/log | `mpsl_assert_handler`, opcjonalnie RTT |

### Binarki + wersjonowanie

Z **jednego tagu** nrfxlib/NCS (np. NCS v3.3.0):

| Artefakt | Ścieżka (przykład) |
|----------|-------------------|
| `libmpsl.a` | `nrfxlib/mpsl/lib/nrf54l/soft-float/` |
| `libnrf-802154-sl.a` | `nrfxlib/nrf_802154/sl/sl/nrf54l/soft-float/` |
| Driver źródła | `nrfxlib/nrf_802154/driver/` |

Dla nRF54L15: ABI **soft-float**.

---

## 7. Nakład pracy

### Już włożone (POC)

| Obszar | Efekt |
|--------|-------|
| nrfx 4.x + MDK + bare-metal build | Działa — fundament |
| CMake + integracja `nrf_802154` | Działa |
| Platform OT + `radio_nrf54.c` | Działa |
| Własny RSCH/timer | Działa częściowo — **FAIL pod load** |
| Testy Morty / dokumentacja debug | Część PASS, znane FAIL |

**Wniosek:** POC to szkielet produktu, nie throwaway. Główna luka: warstwa schedulera radia.

### Do domknięcia produktu (szacunek)

| Etap | Czas |
|------|------|
| Binarki + CMake | 2–3 dni |
| MPSL platform glue | 1–2 tyg |
| FEM Skyworks | 3–5 dni |
| Usunięcie stubów, integracja w ot-rcp | 3–5 dni |
| Spike bez OT (pomiar flash/RAM/radio) | 2–3 dni |
| Regresja Morty | 1–2 tyg |
| **Razem** | **~3–5 tygodni** |

---

## 8. Spike decyzyjny (przed pełną integracją)

**Cel:** odpowiedź „czy ta ścieżka ma sens” w 2–5 dni, bez pełnego OT.

### Co budować

Osobny target CMake, np. `radio-spike-nrf54l15`:

```c
main()
  → mpsl_init()
  → nrf_802154_init()
  → pętla: sleep → receive → transmit (CSMA) → powtórz
```

Bez OpenThread, bez Spinel.

### Co mierzyć

| Metryka | Pass (propozycja) |
|---------|-------------------|
| Flash delta vs obecny `ot-rcp` | < ~50 KB |
| RAM | brak dużego skoku (> ~10 KB) |
| Boot | porównywalny z POC |
| Radio sleep/RX/TX | sniffer widzi ramki |
| CSMA pod load | brak cliff / `terminate_fail` |

### Decyzja

| Wynik spike | Akcja |
|-------------|-------|
| Pass (flash OK + radio OK + CSMA OK) | Pełna integracja w `ot-rcp`, usuń stub RSCH |
| Fail | Diagnoza (clock, wersje binarek, PPI) — nie inwestuj tygodni w ciemno |

---

## 9. Plusy i minusy — efekt finalny

### Plusy (+)

| | |
|--|--|
| Stabilność radia | Oficjalny CSMA/RSCH — docelowo brak cliff i RCP crash |
| Bare-metal | Bez Zephyra/NCS — prosty build i runtime |
| Reużycie POC | ~60–70% kodu zostaje |
| Mniej własnego RSCH | Usunięcie ~1400 LOC wysokiego ryzyka |
| FEM / TX power | Pełne API Nordic |
| Upgrade path | Driver + binarki z jednego tagu |
| Testy | Ten sam Morty/ot-daemon |
| Produkt | RCP gotowy do wdrożenia |

### Minusy (−)

| | |
|--|--|
| Binarki zamknięte | `libmpsl.a` + `libnrf-802154-sl.a`, licencja Nordic |
| Flash/RAM | +~20–50 KB vs stub (do zmierzenia) |
| Pin wersji | Driver, MPSL, SL zsynchronizowane |
| Nowa warstwa | MPSL platform glue do utrzymania |
| Clock/FEM | Unifikacja clock + config Skyworks pod MPSL |
| Debug RSCH | W `.a` — przez referencję NCS |
| Nakład | ~3–5 tyg do pełnej regresji |

---

## 10. Porównanie wariantów (tabela końcowa)

| | POC (stub SL) | **MPSL + SL-binary** | Pełny NCS/Zephyr RCP |
|--|---------------|----------------------|----------------------|
| Bare-metal build | tak | **tak** | nie |
| Stabilność pod load | słaba | **docelowa dobra** | dobra |
| Własny RSCH | ~1400 LOC | brak | brak |
| Binarki Nordic | brak | tak | tak (+ więcej) |
| Flash | najmniejszy | średni (+20–50 KB) | największy |
| Nakład do produktu | nieznany | **3–5 tyg** | migracja buildu |
| Reużycie POC | częściowe | **wysokie** | niskie |

---

## 11. Rekomendacja

Dla **produktu finalnego RCP nRF54L15** w modelu ot-nrf54xx:

> **Wariant A: bare-metal OT RCP + MPSL + SL-binary + cienka warstwa MPSL platform (port z NCS)**

- Zachowuje profit bare-metal (build, workflow, brak RTOS)
- Reużywa większość POC
- Usuwa główną blokadę produktową (własny RSCH)
- Nakład domknięcia: ~3–5 tygodni po pozytywnym spike

**Kolejność pracy:**

1. Macierz wersji (jeden tag nrfxlib → driver + obie binarki)
2. Spike bez OT (flash, radio, CSMA)
3. MPSL platform glue
4. Integracja SL-binary, usunięcie `*_baremetal.c`
5. FEM Skyworks
6. Pełny `ot-rcp` + regresja Morty (cliff_debug, ping pass rate, IPv6 fragmentation)

---

## 12. Jedno zdanie

**POC nRF54L15 to szkielet produktu; przy MPSL+SL przerabiacie ~20% (clock/FEM/CMake), dodajecie ~15% (MPSL glue + binarki), a wyrzucacie ~15% (własny RSCH), który nie dowozi stabilności pod load — zyskując produkcyjne RCP w waszym modelu bare-metal.**
