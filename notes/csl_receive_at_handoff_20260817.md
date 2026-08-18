# nRF54L15 SSED — handoff: CSL `receive_at` / hw_task (CC8)

**Data:** 2026-08-17  
**Status:** timestamper wykluczony; **front naprawy = `nrf_802154_receive_at()` + CC8 hw_task**  
**Build:** `build-nrf54l15-uart/bin/ot-cli-ftd` (child), leader = nRF52840  
**Baseline działający:** 2× nRF52840 SSED — problem tylko gdy **nRF54L15 = child**

Powiązane notatki: `ssed_csl_ping_problem_20260817.md`, `opis_ssed_vs_sed_debug_20260817.md`,  
pcap: `notes/pingi.pcapng`, `notes/ping2.pcapng`

---

## Problem (1 zdanie)

OpenThread **planuje okna CSL** (`SubMac` → `otPlatRadioReceiveAt`), ale driver nRF54 **odrzuca ~99,7%** wywołań `nrf_802154_receive_at()` — radio **nie otwiera delayed RX** na kanale CSL. Parent nadaje co 500 ms, child **nie ACK-uje** → brak sync, rosnące `duration`, ping leader→child fail.

---

## Werdykt z debug stats (GDB, sesja 2026-08-17)

Symbol: `g_nrf54_debug_stats` @ **`0x2000cd20`** (`ot-cli-ftd.map`)

```
p g_nrf54_debug_stats   # w gdb-multiarch, po monitor halt
```

### Kluczowe liczniki (pełna sesja ~3 min CSL)

| Pole | Wartość | Interpretacja |
|------|---------|---------------|
| **`csl_receive_at_enter`** | **368** | OT woła `ReceiveAt` ~368× |
| **`csl_receive_at_ok`** | **1** | `nrf_802154_receive_at()` sukces **1×** |
| **`csl_receive_at_fail`** | **367** | **99,7% fail** na granicy platformy |
| **`hw_task_prepare_ok`** | **1** | CC8 hw_task uzbrojony **tylko przy jednym sukcesie** |
| **`hw_task_prepare_fail`** | **0** | brak ścieżki „too late” w prepare (fail wcześniej) |
| **`last_csl_channel`** | **22** | ostatnie okno na kanale CSL |
| **`last_csl_win_duration`** | **12032 µs** | duże okno (~12 ms) — brak `mCslLastSync` |
| **`rx_no_timestamp`** | **0** | timestamper OK |
| **`rx_frame`** | 34 | RX ogólnie działa |
| **`rx_timestamp_ok`** | 72 | 34 RX + ~38 ACK TX timestamps |
| **`cc2_timer_fires`** | 44 | lptimer CC2 OK (≠ receive_at) |
| **`tx_enter` / `tx_done_success`** | 41 / 41 | TX OK |
| **`rsch_*`** | 0 | **martwe liczniki** — nigdzie nie inkrementowane |

**368 = 1 + 367** — spójne.

### Co z tego wynika

1. **Nie timestamper** — `rx_no_timestamp=0`, `GetRxTimestamp` + `end_to_phr_convert` wdrożone.
2. **Nie OT SubMac planowanie** — `enter=368`, logi RTT `CSL window start … duration …` co ~500 ms.
3. **Tak: driver delayed RX** — `receive_at` fail 367/368; jeden sukces = jeden `hw_task_prepare_ok`.
4. Rosnące **`duration`** (+40 µs/okno lub 7236→8036→…) = **oczekiwane** gdy brak sync (`mCslLastSync` stale) — objaw, nie root cause.

---

## Objawy zewnętrzne

| Źródło | Obserwacja |
|--------|------------|
| RTT log | `CSL window start …, duration …` rośnie; brak `Received frame in state …` |
| Sniffer CSL (`pingi.pcapng`) | ~80× parent→child MLE co 500 ms, **0 ACK** childa |
| Sniffer (`ping2.pcapng`) | 12× parent MLE 82 B bez ACK; 4× child uplink 48 B |
| Ping | leader→child fail; child→leader częściowo; burst po Child Update |
| Mac log | sporadycznie `DestinationAddressFiltered` na broadcast — normalne |

---

## Architektura czasu / CSL (skrót)

```
SubMac::HandleCslReceiveAt()
  → Get<Radio>().ReceiveAt(cslChannel, winStart, winDuration)
  → otPlatRadioReceiveAt()                    [radio_nrf54.c]
  → nrf_802154_receive_at(start-1000, 1000, duration, ch)   [SL binary + driver]
  → scheduler / rsch
  → nrf_802154_platform_sl_lptimer_hw_task_prepare()   [CC8 + DPPI]  [platform]
```

- **Alarm OT / CSL timer:** GRTC via `alarm_nrf54.c` (`OT_GRTC_US_PER_TICK=1`).
- **SL lptimer:** `third_party/nrf54/platform/nrf_802154_platform_sl_lptimer.c` — CC2 callback + **CC8 hw_task** (`NRF54_LPTIMER_CC2_ONLY_BISECT=0`).
- **Timestamper RX:** `third_party/nrf54/platform/nrf_802154_platform_timestamper.c` (variant-c-stubs, whole-archive link).
- **CSL sync w OT:** `mCslLastSync` aktualizowany tylko przy RX + `mAckedWithSecEnhAck` (`sub_mac_csl_receiver.cpp`).

---

## Już zrobione (nie rozwiązało CSL)

### 1. Timestamper (`nrf_802154_platform_timestamper.c`)

- `grtc_capture_prepare()`: `nrfx_grtc_syscounter_cc_absolute_set(COUNTER_SPAN, false)` (nie `compare_event_enable`).
- Cleanup GPPI/PPIB w `cross_domain_connections_clear` / `local_domain_connections_clear`.
- Makra GRTC: `NRF_GRTC_SYSCOUNTERL_VALUE_MASK` itd.

### 2. `GetRxTimestamp()` (`src/nrf54l15/radio_nrf54.c`)

```c
// BYŁO (źle): NO_TIMESTAMP → nrf5AlarmGetCurrentTime() — maskowało brak capture
// JEST:
if (aTime == NRF_802154_NO_TIMESTAMP) { rx_no_timestamp++; return 0; }
return nrf_802154_timestamp_end_to_phr_convert(aTime, aLength);
```

### 3. Debug stats (`third_party/nrf54/platform/nrf54_debug_stats.h`, `radio_nrf54.c`)

- `rx_timestamp_ok`, `rx_no_timestamp`
- `csl_receive_at_enter/ok/fail`, `last_csl_channel/win_start/win_duration`
- `OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE=1` w `openthread-core-nrf54l15-config.h`

---

## Pliki do grzebania (priorytet)

| Priorytet | Plik | Dlaczego |
|-----------|------|----------|
| **P0** | `src/nrf54l15/radio_nrf54.c` — `otPlatRadioReceiveAt()` | liczniki ok/fail; wywołanie `nrf_802154_receive_at` |
| **P0** | `third_party/nrf54/platform/nrf_802154_platform_sl_lptimer.c` — `hw_task_prepare()` | tylko 1× ok; early return `NO_RESOURCES` **bez licznika** |
| **P0** | `third_party/nrf54/platform/nrf_802154_platform_sl_lptimer_grtc_hw_task.c` | CC8 → DPPI → RADIO task |
| **P1** | SL binary / `nrf_802154_receive_at` w driverze (`third_party/nrf54/nrfxlib/nrf_802154/`) | dlaczego zwraca `false` — może wymagać breakpoint / nowych counterów w platformie |
| **P1** | `openthread/src/core/mac/sub_mac_csl_receiver.cpp` | skip `ReceiveAt` gdy `mState == kStateReceive`; `LogReceived` wymaga `kStateRadioSample` (przy `RECEIVE_TIMING` deviation log nie działa) |
| **P2** | `notes/nrf_802154_platform_timestamper.c` | referencja NCS/Zephyr (timestamper już OK) |
| **P2** | NCS `nrf_802154_platform_sl_lptimer_grtc_hw_task.c` | diff z naszą platformą |

### Hipotezy do sprawdzenia w kodzie

1. **`m_hw_task_state` utknięty ≠ IDLE** → `hw_task_prepare()` → `NO_RESOURCES` → SL zwraca fail z `receive_at` (367×), **bez** `hw_task_prepare_fail++`.
2. **Brak cleanup CC8** po oknie / po fail — kolejne `receive_at` odrzucane.
3. **Compare event CC8** nie re-armowany po `syscounter_cc_absolute_set` (analogia do buga timestampera).
4. **Kolizja stanu radia** — child w ciągłym RX (`kStateReceive` poll/Child Update) vs delayed RX na innym kanale.
5. **Timing „too late”** — `winStart` w przeszłości względem GRTC (mniej prawdopodobne przy stałym fail, ale warto zmierzyć `last_csl_win_start` vs `nrfx_grtc_syscounter_get()`).

---

## Sugerowane następne kroki (agent)

1. **Dodać liczniki** w `hw_task_prepare()`:
   - `hw_task_no_resources` (CAS fail na IDLE→SETTING_UP)
   - `hw_task_wrong_state` (inne early return)
   - `last_hw_task_state` (snapshot `m_hw_task_state`)
   - opcjonalnie hook w SL przez wrapper jeśli możliwe

2. **Breakpoint GDB:**
   ```gdb
   break otPlatRadioReceiveAt
   break nrf_802154_platform_sl_lptimer_hw_task_prepare
   ```
   Przy `csl_receive_at_fail++`: stack trace, stan `m_hw_task_state`, `fire_lpticks` vs `nrfx_grtc_syscounter_get()`.

3. **Diff z NCS** dla:
   - `hw_task_schedule()` — `nrfy_grtc_sys_counter_compare_event_enable` po absolute_set
   - cleanup po zakończeniu / anulowaniu delayed RX
   - `receive_at` margin (`SAFE_DELTA=1000` w `radio_nrf54.c` — standard Nordic)

4. **Kryterium sukcesu po fixie:**
   - `csl_receive_at_ok ≈ csl_receive_at_enter` (± kilka)
   - `hw_task_prepare_ok` rośnie co okno CSL
   - `last_csl_win_duration` stabilne (nie +40 co okno)
   - sniffer: ACKi childa na parent MLE co 500 ms
   - ping leader→child przechodzi

---

## Debug — szybka ściąga

### GDB (preferowane)

```bash
cd build-nrf54l15-uart/bin
gdb-multiarch ot-cli-ftd
# target extended-remote :2331, monitor halt
p g_nrf54_debug_stats
p/x g_nrf54_debug_stats.csl_receive_at_enter
p/x g_nrf54_debug_stats.csl_receive_at_ok
p/x g_nrf54_debug_stats.csl_receive_at_fail
p/x g_nrf54_debug_stats.hw_task_prepare_ok
```

### nrfjprog (wyrównanie 8 B!)

```bash
# CAŁY blok CSL + rx (od CE20, 9 słów):
nrfjprog -f nrf54l --halt
nrfjprog -f nrf54l --memrd 0x2000CE20 --w 32 --n 9
# hw_task:
nrfjprog -f nrf54l --memrd 0x2000CD40 --w 32 --n 2
nrfjprog -f nrf54l --run
```

**Nie używać** `memrd 0x2000CE2C` — nrf54l wymaga adresu 8-byte aligned dla `--n > 1`.

### Logi RTT (nie UART CLI)

- Build: `-DOT_LOG_LEVEL=DEBG` (domyślnie CRIT wycina `LogDebg`)
- `OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE=1` już w configu
- Logi idą przez **SEGGER RTT** (`src/nrf54l15/logging.c`), nie przez UART Spinel

---

## Konfiguracja build

```bash
./script/build nrf54l15 UART_trans -- -DOT_LOG_LEVEL=DEBG
# flash:
nrfjprog --program build-nrf54l15-uart/bin/ot-cli-ftd.hex --chiperase -f nrf54l --verify
```

Platform CMake: `NRF54_LPTIMER_CC2_STUB_BISECT=0`, `NRF54_LPTIMER_CC2_ONLY_BISECT=0` (CC2+CC8+DPPI).

Timestamper: `third_party/nrf54/platform/nrf_802154_platform_timestamper.c` w **`nrf54-variant-c-stubs.a`** (whole-archive), nie w `nrf-802154-platform.a`.

---

## OpenThread — pułapki (nie mylić z root cause)

- **`LogCslWindow`** w RTT **≠** sukces `receive_at` — log jest przed warunkiem stanu; `ReceiveAt` pomijany gdy `mState == kStateReceive`.
- **`rsch_dly_start`** w stats — **dead counter**, nie interpretować.
- **`OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_TIMING_ENABLE=1`** w platform config — na FTD caps i tak z `otPlatRadioGetCaps()`; CSL używa `HandleCslReceiveAt` (hardware timing).

---

## Mapowanie offsetów `g_nrf54_debug_stats` (dla memrd)

Baza: **`0x2000CD20`**

| Offset | Adres | Pole |
|--------|-------|------|
| +0x100 | CE20 | `rx_frame` |
| +0x104 | CE24 | `rx_timestamp_ok` |
| +0x108 | CE28 | `rx_no_timestamp` |
| +0x10C | CE2C | `csl_receive_at_enter` |
| +0x110 | CE30 | `csl_receive_at_ok` |
| +0x114 | CE34 | `csl_receive_at_fail` |
| +0x118 | CE38 | `last_csl_channel` |
| +0x11C | CE3C | `last_csl_win_start` |
| +0x120 | CE40 | `last_csl_win_duration` |
| +0x020 | CD40 | `hw_task_prepare_ok` |
| +0x024 | CD44 | `hw_task_prepare_fail` |
| +0x010 | CD30 | `cc2_timer_fires` |

---

## Jedno zdanie dla następnego agenta

**Napraw `nrf_802154_receive_at` → CC8 hw_task na nRF54L15 platformie; timestamper i OT CSL planning są OK — 367/368 wywołań `receive_at` kończy się fail i radio nie słucha parenta na kanale CSL.**
