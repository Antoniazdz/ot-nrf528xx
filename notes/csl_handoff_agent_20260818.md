# CSL nRF54L15 child — handoff dla następnego agenta (2026-08-18)

**Skopiuj blok PROMPT poniżej do nowej sesji.** Reszta pliku = kontekst bez czytania całego repo.

---

## PROMPT (wklej do agenta)

```
Kontekst: ot-nrf528xx, nRF54L15 = SSED child (ot-cli-ftd), nRF52840 = leader.
Problem: CSL sync fail, ping leader→child, child↔detached.

Przeczytaj: notes/csl_handoff_agent_20260818.md (ten plik).

NIE patchuj nrf_802154_core.c / drivera — user wymaga fixu platformy jak NCS (ot-nrf528xx radio.c).

Stan kodu (2026-08-18):
- FIXED: otPlatRadioReceiveAt args w src/nrf54l15/radio_nrf54.c (rxTime, duration, channel, DRX_SLOT_RX=0) + scheduled_cancel przed receive_at.
- W kodzie ZŁE FIXy do izolacji/cofnięcia:
  P1: otPlatRadioEnableCsl → nrf_802154_pib_rx_on_when_idle_set(aCslPeriod==0)
  P2: nrf5RadioProcess kPendingEventSleep → nrf_802154_sleep() gdy sCslPeriod>0
  P1+P2 razem zabiły DRX (hw_task_prepare=0, drx_callback=0) — cofnij P1 najpierw, testuj.

Potwierdzony root cause (staty GDB, baseline BEZ P1/P2):
  drx_started_callback ≈ hw_task_prepare_ok ≈ 31
  core_receive_delayed_trx_skipped_already_rx = 31 (100%)
  core_receive_delayed_trx_enter = 0, rx_init_hw_enter = 0, hw_task_update_ppi_enter = 0
  → DRX startuje, ale nrf_802154_core_receive pomija core_receive bo m_state==RADIO_STATE_RX
  → brak rx_init(HW) i ppi_update; radio zostaje na SW-RX kanale mesh.

NCS nie patchuje core — utrzymuje sleep między oknami CSL (rx_on_when_idle + post-window sleep).
NIE rób nrf_802154_sleep() WEWNĄTRZ otPlatRadioReceiveAt — zabija lptimer/DRX.

Debug: p/x g_nrf54_debug_stats (NRF54_DEBUG_STATS=1). Map: build-nrf54l15-uart/ot-cli-ftd.map

Zadanie: [wpisz tu]
```

---

## Problem (1 zdanie)

OT planuje okna CSL (`ReceiveAt`), platforma uzbraja CC8 i DRX callback startuje, ale **driver nie robi HW RX na kanale CSL** bo `core_receive(DELAYED_TRX)` jest **pomijane** gdy radio już w `RADIO_STATE_RX` (ciągły `otPlatRadioReceive` na kanale mesh).

---

## Co już naprawione (ZOSTAW)

| Fix | Plik | Opis |
|-----|------|------|
| Args `receive_at` | `src/nrf54l15/radio_nrf54.c` | `nrf_802154_receive_at(rxTime, aDuration, aChannel, DRX_SLOT_RX)` + `unwrapFutureRadioTimeUs()` |
| Cancel slot | j.w. | `(void)nrf_802154_receive_at_scheduled_cancel(DRX_SLOT_RX)` przed każdym oknem (dup id=0) |
| Debug stats | `third_party/nrf54/platform/nrf54_debug_stats.h`, `core.c`, `delayed_trx.c` | Liczniki DRX/skip — bez zmiany logiki |

---

## Baseline działający (przed P1/P2) — staty GDB

Sesja ze **stabilnym child**, ~31 okien CSL:

| Licznik | Wartość | Znaczenie |
|---------|---------|-----------|
| `csl_receive_at_ok` | ~51–62 | OT scheduluje okna |
| `hw_task_prepare_ok` | **31** | CC8 uzbrojony |
| `drx_started_callback_enter` | **31** | DRX callback OK |
| `drx_receive_attempt_ok` | **31** | request_receive OK |
| `core_receive_delayed_trx_skipped_already_rx` | **31** | **BUG: skip bo już RX** |
| `core_receive_delayed_trx_enter` | **0** | core_receive nigdy |
| `rx_init_hw_enter` | **0** | brak HW RX |
| `hw_task_update_ppi_enter` | **0** | brak DPPI |

**Wniosek:** scheduling + DRX OK; ginie na `nrf_802154_core_receive()` linia ~3024: skip gdy `m_state==RADIO_STATE_RX`.

---

## Złe fixy — NIE powtarzać

| Fix | Efekt statów | Dlaczego źle |
|-----|--------------|--------------|
| `nrf_802154_sleep()` **w** `otPlatRadioReceiveAt` przed `receive_at` | `hw_task=0`, `drx_callback=0`, `skipped=0` | `lptimer_disable` psuje **nowy** schedule |
| P1: `rx_on_when_idle_set(false)` w `EnableCsl` | jak wyżej + fail receive_at ~76% | agresywny sleep/lptimer_disable — **DRX martwy** |
| P2: force sleep w `kPendingEventSleep` | `csl_sleep_after_window=0` (DRX nie kończy okien) | sam P2 nie pomaga bez działającego DRX |
| Patch `core_receive` dla DELAYED_TRX | — | **user nie chce** dotykać drivera |

---

## Aktualny kod (DO COFNIĘCIA / TESTU A/B)

**P1** — `src/nrf54l15/radio_nrf54.c` ~1691:
```c
nrf_802154_pib_rx_on_when_idle_set(aCslPeriod == 0);
```

**P2** — `src/nrf54l15/radio_nrf54.c` ~1239–1269:
```c
if (sCslPeriod > 0) { nrf_802154_sleep(); ... csl_sleep_after_window++; }
```

**Stat po P1+P2 rebuild (~2026-08-18):** `hw_task_prepare=0`, `drx_callback=0`, `csl_sleep_after_window=0`, `receive_at fail=149/195` — **gorsze niż baseline**.

**Rekomendacja:** cofnij **P1**, zostaw P2, retest → jeśli wraca baseline (31/31 skip), szukaj łagodniejszego sleep (np. jednorazowy sleep po `EnableCsl`, nie globalne PIB).

---

## NCS kontrakt (platforma, nie core)

Z `openthread/src/core/mac/sub_mac_csl_receiver.cpp`:
- Przy `OT_RADIO_CAPS_RECEIVE_TIMING` OT **nie woła** `Radio::Sleep()` między oknami.
- Po `ReceiveAt` radio **w sleep do startu okna**; **po oknie** platforma usypia (`receive_failed DELAYED_TIMEOUT` → sleep).
- nRF52840 `radio.c`: `receive_at` **bez** sleep na początku; post-window via `receive_failed` + `sleep_if_idle`.

Gap bare-metal: między oknami driver zostaje w **RX** po `otPlatRadioReceive()` → przy DRX callback `skipped_already_rx`.

---

## Przepływ (działający vs zepsuty)

```
[DZIAŁA scheduling, GINIE na core]
ReceiveAt → receive_at OK → hw_task_prepare → [RX mesh] → DRX callback
  → receive_attempt → core_receive SKIP (m_state==RX) → brak ppi_update

[ZEPSUTE przez sleep/rx_on_when_idle agresywnie]
ReceiveAt → sleep/lptimer_disable → receive_at (często fail) → hw_task=0, callback=0
```

---

## GDB — szybka ściąga

```gdb
p/x g_nrf54_debug_stats.drx_started_callback_enter
p/x g_nrf54_debug_stats.hw_task_prepare_ok
p/x g_nrf54_debug_stats.core_receive_delayed_trx_skipped_already_rx
p/x g_nrf54_debug_stats.core_receive_delayed_trx_enter
p/x g_nrf54_debug_stats.rx_init_hw_enter
p/x g_nrf54_debug_stats.hw_task_update_ppi_enter
p/x g_nrf54_debug_stats.csl_receive_at_ok
p/x g_nrf54_debug_stats.csl_receive_at_fail
p/x g_nrf54_debug_stats.csl_sleep_after_window
```

**Sukces docelowy (platforma):** `skipped_already_rx→0`, `core_receive_delayed_trx_enter>0`, `rx_init_hw_enter>0`, `hw_task_update_ppi_enter>0`.

**Breakpointy:** unikaj `rx_init` bez warunku; używaj statów. `REQ_ORIG_DELAYED_TRX=5`.

---

## Kluczowe pliki

| Plik | Rola |
|------|------|
| `src/nrf54l15/radio_nrf54.c` | **platforma OT** — ReceiveAt, EnableCsl, receive_failed, sleep |
| `third_party/nrf54/platform/nrf_802154_platform_sl_lptimer*.c` | CC8 hw_task, DPPI |
| `third_party/nrf54/platform/nrf54_debug_stats.h` | liczniki |
| `third_party/nrf54/nrfxlib/.../nrf_802154_core.c` | skip RX — **tylko stats, nie fix usera** |
| `openthread/.../sub_mac_csl_receiver.cpp` | kontrakt OT CSL |
| `notes/csl_receive_at_handoff_20260817.md` | starszy handoff (timestamper, mapy) |

Build: `./script/build nrf54l15 UART_trans` → `build-nrf54l15-uart/bin/ot-cli-ftd`

---

## Otwarte kierunki (platforma only)

1. Cofnij P1, A/B test P2 vs baseline.
2. Sleep **po** `EnableCsl` / po `otPlatRadioReceive` gdy CSL ON — **nie** w `ReceiveAt`, **nie** globalne `rx_on_when_idle` bez testu.
3. Upewnij się `receive_failed(DELAYED_TIMEOUT)` → `kPendingEventSleep` faktycznie domyka okna (baseline: sleep_if_idle fail w RX).
4. Dup `receive_at` fail — `scheduled_cancel` już jest; dalej ~50% fail w baseline.

---

*Agent transcript: `agent-transcripts/1de321a6-3c5e-4d6e-ad35-7b89065d2575/`*