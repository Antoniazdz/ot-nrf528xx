# nRF54L15 RCP: brak binarek + brak MPSL (bare-metal, SL-opensource)

Notatka decyzyjna — wariant **bez `libnrf-802154-sl.a`**, **bez `libmpsl.a`**, ze źródłowym driverem `nrf_802154` i stubem Service Layer (`SL_OPENSOURCE`) uzupełnionym własną warstwą platformy.

**Data:** 2026-08-07  
**Kontekst:** obecny POC ot-nrf54xx na nRF54L15. Smoke i część testów Morty przechodzą; stabilność TX pod obciążeniem — nie.

Powiązane dokumenty:
- [Wariant MPSL + SL-binary](nrf54l15-mpsl-sl-binary-decision.md)
- [Wariant brak MPSL + SL-binary (kompromis flash)](nrf54l15-no-mpsl-sl-binary-decision.md)
- [Handoff testy 2026-08-05](nrf54l15-handoff-2026-08-05.md)
- [Wyniki CTF 2026-08-05](nrf54l15-ctf-test-results-2026-08-05.md)
- [Migracja nRF52 → nRF54](RCP_NRF52_TO_NRF54.md)

---

## 1. Co oznacza ten wariant

| Składnik | Wariant „brak binarek + brak MPSL” |
|----------|-------------------------------------|
| **Driver 802.15.4** | Źródła open source (`nrf_802154/driver/`) |
| **Service Layer** | `sl_opensource/` (stub Nordic) + **własne** RSCH/timer/lptimer |
| **MPSL** | Brak — single-PHY arbiter (własny RSCH) |
| **Binarki Nordic** | Brak (`libnrf-802154-sl.a`, `libmpsl.a`) |
| **Build** | CMake bare-metal, bez Zephyra/NCS |

Oficjalna dokumentacja Nordic ([multiprotocol_support.rst](../third_party/nrf54/nordic/drivers/nrf_802154/doc/multiprotocol_support.rst)): *single PHY arbiter* jest przeznaczony do aplikacji **tylko 802.15.4** (Thread-only, Zigbee-only), gdy MPSL nie jest możliwy. Stub `sl_opensource` realizuje ten model — **nie** zastępuje pełnego SL-binary.

---

## 2. Stan wyjściowy (obecny POC = ten wariant)

| Warstwa | Stan | Lokalizacja |
|---------|------|-------------|
| Driver | Źródła + lokalne patche | `third_party/nrf54/nordic/drivers/nrf_802154` |
| SL | `SL_OPENSOURCE=ON` | `sl/sl_opensource/` |
| Własny RSCH/timer | ~1400 LOC | `platform/nrf_802154_rsch_baremetal.c`, `nrf_802154_sl_timer_baremetal.c` |
| GRTC lptimer | ~650 LOC + opcje bisect | `platform/nrf_802154_platform_sl_lptimer*.c` |
| MPSL | Brak | — |
| OpenThread RCP | Działa (Spinel/UART) | `src/nrf54l15/radio_nrf54.c`, … |

### Rozmiar firmware (pomiar z buildu debug)

| Metryka | Wartość (`build-nrf54l15-uart/bin/ot-rcp`) |
|---------|---------------------------------------------|
| `.text` | ~96 KB |
| `.data` | ~0,4 KB |
| `.bss` | ~13,5 KB |
| **Razem RAM statyczna** | ~14 KB + heap OT |

Dla porównania: sama binarka SL to ~44 KB (`libnrf-802154-sl.a` w `_nowy`); MPSL to kolejne dziesiątki KB — **ten wariant ich nie linkuje**.

### Co działa

- Build bare-metal (CMake, nrfx 4.x, MDK, startup)
- Smoke sieci Thread, część testów Morty PASS
- Ping 16 B, ping 1000 B pojedynczy, rate 2 s (53/53 OK w cliff_debug)
- OpenThread platform (UART, alarm GRTC, crypto, system)

### Znane problemy (blokery produktowe)

| Problem | Objaw | Test |
|---------|-------|------|
| Cliff pod load | `radio tx timeout` → RCP failure | `test_app_layer_lost_pings`, cliff_debug |
| CSMA / terminate | `tx_core_deny_terminate_fail` rośnie | ping 1000 B @ 0.1 s |
| Sustained TX | crash RCP po UDP/iperf | IPv6 fragmentation |
| GRTC contention | CC8/DPPI wyłączone (bisect) — CCA/timing | `NRF54_LPTIMER_CC2_ONLY=ON` |

**Przyczyna:** driver źródłowy jest OK, ale **zastąpiono zamknięty SL własną implementacją schedulera** (~2600 LOC w `platform/`), która nie ma referencji produkcyjnej Nordic na nRF54L15.

---

## 3. Architektura wariantu

```
┌─────────────────────────────────────────────────────────┐
│  OpenThread (ot-rcp, Spinel, HDLC)          ✅ działa   │
├─────────────────────────────────────────────────────────┤
│  otPlat* (radio_nrf54.c, UART, alarm, …)    ✅ działa   │
├─────────────────────────────────────────────────────────┤
│  Build: CMake, nrfx, MDK, startup           ✅ działa   │
├─────────────────────────────────────────────────────────┤
│  nrf_802154 driver (źródła)                 ✅ działa   │
├─────────────────────────────────────────────────────────┤
│  sl_opensource (stub Nordic)                ⚠️ minimal  │
├─────────────────────────────────────────────────────────┤
│  własny RSCH + timer + lptimer (~2600 LOC)  ⚠️ ryzyko   │
├─────────────────────────────────────────────────────────┤
│  libnrf-802154-sl.a                         ❌ brak      │
├─────────────────────────────────────────────────────────┤
│  libmpsl.a                                  ❌ brak      │
└─────────────────────────────────────────────────────────┘
```

Przy `SL_OPENSOURCE=ON` driver CMake **wyłącza** (upstream `_nowy`):

- `NRF_802154_CSMA_CA_ENABLED=0`
- `NRF_802154_DELAYED_TRX_ENABLED=0`
- `NRF_802154_FRAME_TIMESTAMP_ENABLED=0`
- `NRF_802154_IFS_ENABLED=0`

Stub SL zwraca `nrf_802154_sl_capabilities_get() == 0` — brak runtime discovery featuów.

---

## 4. Profity tego rozwiązania

### Bare-metal i pamięć (główny profit)

| Profit | Szczegół |
|--------|----------|
| **Najmniejszy flash** | Brak ~44 KB SL + brak MPSL; obecny `ot-rcp` ~96 KB `.text` |
| **Prostszy link** | Tylko źródła + własna platforma — bez synchronizacji wersji binarek |
| **Brak zamkniętych binarek** | SL stub = BSD-3-Clause; pełna widoczność RSCH/timer w debuggerze |
| **Prostszy main loop** | Brak `mpsl_init()`, `mpsl_low_priority_process()` |
| **Brak MPSL IRQ constraints** | Nie trzeba respektować priorytetów MPSL vs 802.15.4 |

### Model deweloperski

- Ten sam workflow co ot-nrf528xx: CMake + `arm-none-eabi-gcc`, Morty/ot-daemon
- Bez Zephyra, bez `west`, bez device tree
- Analogia do nRF52840 w tym repo: `RAAL_SINGLE_PHY=1`, własny RSCH w `NordicSemiconductor/drivers/radio/rsch/` — **sprawdzony wzorzec na nRF52**, ale na innym (starszym) driverze

### Co już działa (nie trzeba budować od zera)

- Cały szkielet OT RCP + integracja Spinel
- nrfx 4.x, MDK nRF54L15, startup, linker
- Smoke Thread, część regresji Morty
- Wiedza o GRTC, clock XO/LFCLK, FEM Skyworks (stub)

---

## 5. Co można wykorzystać vs co trzeba nowego / naprawić

### Już jest i zostaje (~70% nakładu POC)

| Obszar | Pliki / komponenty | Status |
|--------|-------------------|--------|
| OpenThread platform | `radio_nrf54.c`, `uart_nrf54.c`, `alarm_nrf54.c`, `system_nrf54.c`, … | ✅ gotowe |
| Build | `nrf54l15.cmake`, `third_party/nrf54/CMakeLists.txt` | ✅ gotowe |
| SDK | `nordicsemi-nrf54l15-sdk` (nrfx clock, GRTC, GPPI) | ✅ gotowe |
| Driver platform (podstawy) | `nrf_802154_irq_platform.c`, `nrf_802154_clock_platform.c`, `nrf_802154_temperature_platform.c` | ✅ gotowe |
| Własny RSCH/timer | `nrf_802154_rsch_baremetal.c`, `nrf_802154_sl_timer_baremetal.c` | ⚠️ działa częściowo |
| GRTC lptimer | `nrf_802154_platform_sl_lptimer.c` (+ stub/hw_task) | ⚠️ bisect, CC8 off |
| Debug | `nrf54_debug_stats.c`, patche w `nrf_802154_core.c` | ✅ pomaga w diagnozie |

### Wymaga dalszej pracy (~30% — **tu leży ryzyko produktowe**)

| Obszar | Co brakuje / co nie działa | Szacunek |
|--------|---------------------------|----------|
| **RSCH pod load** | Cliff TX, `terminate_fail`, timeslot denial | główny blocker |
| **GRTC budget** | OT alarm (CC0–1) vs SL lptimer (CC2, CC8) — konflikt zasobów | 1–2 tyg |
| **CSMA** | Upstream wyłącza CSMA przy SL_OPENSOURCE; lokalnie częściowo włączone — niespójność | decyzja + implementacja |
| **Delayed TRX** | Stub (`nrf_802154_delayed_trx_stub.c`) — brak CSL/TSCH | opcjonalnie |
| **FEM Skyworks** | `fem_nrf54.c` — stub, bez pełnej integracji SL FEM API | 3–5 dni |
| **Driver sync** | POC na starszym drzewie; `_nowy` ma `csma_ca_backoff`, `swi_callouts` — merge/trudność | 1 tyg |
| **Regresja Morty** | cliff_debug, ping pass rate, IPv6 fragmentation — gate produktowy | ciągły |

### Co jest **nowe** względem nRF52840 (nie ma gotowca)

Na nRF52 ten repo używa **monolitycznego** drivera z CSMA w warstwie driver + `single_phy.c` RAAL. Na nRF54L architektura nrfxlib **rozdziela CSMA/RSCH do SL** — bez binariów trzeba to **samemu odtworzyć** w `platform/` (~2600 LOC i rośnie).

---

## 6. Nakład pracy i czas implementacji

### Już włożone (POC)

| Obszar | Efekt |
|--------|-------|
| Bare-metal build + CMake | ✅ fundament |
| Integracja driver + SL stub | ✅ linkuje |
| Platform OT + RCP | ✅ smoke OK |
| Własny RSCH/timer/lptimer | ⚠️ ~2600 LOC — **FAIL pod load** |
| Testy Morty + dokumentacja debug | część PASS, znane FAIL |

### Do domknięcia jako produkt (szacunek — **wysoka niepewność**)

| Etap | Opis | Czas |
|------|------|------|
| Diagnoza cliff | PCAP, debug stats, izolacja GRTC/RSCH | 1–2 tyg |
| Stabilizacja RSCH | Poprawki `rsch_baremetal`, HFCLK, prio, delayed TS | 2–4 tyg |
| GRTC / lptimer | CC8+DPPI bez psucia CCA **lub** alternatywny timing | 1–3 tyg |
| CSMA spójność | Zgodność z driver `_nowy` + własny SL path | 1–2 tyg |
| FEM Skyworks | Pełna konfiguracja TX (nie stub) | 3–5 dni |
| Merge driver `_nowy` | Nowsze ACK/CSMA/peripherals bez binarek | 1–2 tyg |
| Regresja Morty | cliff, ping pass rate, fragmentation, iperf | 1–2 tyg |
| **Razem (optimistycznie)** | | **~6–10 tyg** |
| **Razem (realistycznie)** | Bez referencji Nordic — możliwe kolejne iteracje | **~3–6 mies.** |

**Kluczowa różnica vs wariant MPSL+SL-binary:** tam Nordic daje gotowy RSCH/CSMA; tutaj **sami jesteście właścicielem schedulera** — każdy bug pod load to Wasz bug.

---

## 7. Spike / gate przed inwestycją

**Cel:** potwierdzić, czy da się ustabilizować własny RSCH bez binarek.

### Test minimalny (bez OT)

```c
main()
  → nrf_802154_init()
  → pętla: transmit (CSMA jeśli włączone) @ 0.1 s, 1000 B PSDU
  → licznik: tx timeout / terminate_fail
```

### Pass (propozycja)

| Metryka | Pass |
|---------|------|
| 1000+ TX @ 0.1 s | 0× `radio tx timeout` |
| `tx_core_deny_*` | nie rośnie |
| Flash | ≤ obecny ~96 KB `.text` (± kilka KB) |
| 120 s sustained | bez RCP failure |

### Decyzja

| Wynik | Akcja |
|-------|-------|
| Pass | Kontynuuj ten wariant; domknij FEM + regresję Morty |
| Fail po 2–4 tyg | **Pivot na SL-binary** (patrz [notatka MPSL+SL](nrf54l15-mpsl-sl-binary-decision.md)) — nie inwestuj miesięcy w reverse-engineering RSCH |

---

## 8. Plusy i minusy jako produkt

### Plusy (+)

| | |
|--|--|
| **Najmniejszy flash/RAM** | Brak binarek SL+MPSL; obecny build ~96 KB text |
| **100% audytowalny kod schedulera** | RSCH/timer w repo — debugger, static analysis |
| **Licencja** | Brak Nordic-5-Clause na binariach SL/MPSL |
| **Prosty runtime** | Brak MPSL state machine w main loop |
| **Reużycie POC** | ~70% już zrobione |
| **Thread-only RCP** | Single PHY wystarczy — BLE nie na tym chipie |
| **Workflow** | Ten sam bare-metal co nRF52840 w Morty |

### Minusy (−)

| | |
|--|--|
| **Brak wsparcia Nordic na nRF54L15** | `sl_opensource` to stub pod Zephyr/NCS, nie produkt bare-metal |
| **Stabilność pod load** | Potwierdzony FAIL — cliff, RCP crash |
| **Wyłączone featury** | Brak oficjalnego CSMA/DTRX/timestamp/IFS w konfiguracji upstream |
| **~2600 LOC własnej platformy** | Wysokie koszty utrzymania — każdy upgrade drivera = merge |
| **Brak upgrade path SL** | Poprawki timingu tylko u Was, nie z tagu nrfxlib |
| **GRTC jako bottleneck** | OT + radio dzielą te same CC — trudne do rozwiązania bez Nordic reference |
| **FEM / TX power** | Bez pełnego SL — więcej własnej pracy |
| **Czas do produktu** | Niepewny (3–6 mies.) vs ~3–5 tyg z binarkami |
| **Ryzyko certyfikacji / QA** | Trudniejsze uzasadnienie vs referencja NCS |

---

## 9. Co daje bare-metal w tym formacie

| Aspekt | Efekt |
|--------|-------|
| **Flash** | Najmniejszy możliwy wariant RCP na 54L15 w tym repo (~96 KB OT+driver+stub platform) |
| **RAM** | ~14 KB BSS platformy + OT; brak buforów MPSL |
| **Boot** | Krótszy init — brak `mpsl_init()` |
| **CPU w idle** | Brak `mpsl_low_priority_process()` co tick |
| **Determinizm** | Teoretycznie lepszy (mniej warstw) — **praktycznie gorszy** dopóki RSCH nie stabilny |
| **Debug** | Pełny source-level na RSCH — **jeśli macie czas go debugować** |
| **Produkt RCP Thread-only** | Wystarczający **feature scope** (bez BLE, bez CSL) — **po ustabilizowaniu TX** |

**Podsumowanie bare-metal:** ten wariant maksymalizuje kontrolę i minimalizuje pamięć, ale **płaci za to własnym scheduleraem radia**, którego Nordic nie testuje na nRF54L15 poza ścieżką SL-binary.

---

## 10. Porównanie z innymi wariantami

| | **Brak bin + brak MPSL** (ten) | Brak bin + brak MPSL → tylko demo | MPSL + SL-binary |
|--|--------------------------------|-----------------------------------|------------------|
| Flash | **~96 KB** (najmniej) | — | ~+20–50 KB |
| Binarki Nordic | brak | brak | tak |
| Własny RSCH | ~2600 LOC | ~2600 LOC | brak |
| Stabilność pod load | **słaba (dziś)** | akceptowalna dla demo | docelowa dobra |
| Wsparcie Nordic | brak | — | tak |
| Czas do produktu | 3–6 mies. (?) | już jest (POC) | ~3–5 tyg |
| Audyt kodu | pełny | pełny | częściowo `.a` |

---

## 11. Kiedy ten wariant ma sens

### Ma sens jako:

- **POC / demo** — smoke, laboratorium, wczesna walidacja OT na 54L15
- **Produkt niszowy** z twardym limitem flash i akceptacją własnego QA schedulera
- **Krok pośredni** przed migracją na SL-binary (wiedza o GRTC, clock, FEM)

### Nie ma sensu jako:

- **Produkt RCP z regresją Morty / sustained load** — bez naprawy cliff (dziś FAIL)
- **Substytut NCS** — brak parity CSMA/DTRX/timestamp
- **Multiprotocol** (BLE + Thread) — bez MPSL nie ma dynamic arbitera

---

## 12. Rekomendacja

Dla **produktu finalnego RCP nRF54L15** w modelu ot-nrf54xx:

> **Ten wariant nie jest rekomendowany jako cel produktowy** — jest **obecnym stanem POC** z potwierdzonymi problemami stabilności TX.

Jako **ścieżka optymalizacji pamięci** ma sens **dopiero po** pozytywnym spike (sekcja 7): jeśli własny RSCH da się ustabilizować w 2–4 tygodnie, można rozważyć kontynuację. Jeśli nie — pivot na [MPSL + SL-binary](nrf54l15-mpsl-sl-binary-decision.md) (nawet bez MPSL: **SL-binary bez MPSL** jako kompromis flash vs stabilność).

**Kolejność pracy (jeśli zostajecie przy tym wariancie):**

1. Spike TX load bez OT (1000+ ramek @ 0.1 s)
2. Diagnoza GRTC (mapa CC: OT vs SL lptimer vs RSCH timer)
3. Stabilizacja `rsch_baremetal` + decyzja CSMA (włączyć spójnie z driver `_nowy` **lub** wyłączyć i polegać na OT CSMA)
4. FEM Skyworks (pełna, nie stub)
5. Regresja Morty jako gate
6. Dopiero wtedy: dokumentacja produktowa, wersjonowanie hex

---

## 13. Jedno zdanie

**Brak binarek + brak MPSL to najlżejszy bare-metal RCP pod kątem flash i licencji, ale przenosi całą odpowiedzialność za scheduler radia (~2600 LOC) na Was — POC pokazał, że smoke przechodzi, a produkcyjny load dziś nie, więc bez szybkiego spike ten wariant zostaje demo, nie produkt.**
