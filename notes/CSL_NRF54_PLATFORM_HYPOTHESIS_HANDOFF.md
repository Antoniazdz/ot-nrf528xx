# CSL / SSED nRF54L15 — hipoteza platformy i handoff dla następnego agenta

> **Data:** 2026-08-21  
> **Kontekst:** nRF54L15 child (SSED) + nRF52840 leader. Attach niestabilny, 100% DRX timeout CSL, `sync_from_rx=0`.  
> **Powiązane notatki:** `CSL_INIT_PHASE_GETNOW.md`, `CSL_PHASE_SYNC_NRF52840.md`, `csl_receive_at_handoff_20260817.md`  
> **Status hipotezy:** **P1 — silna, nie zamknięta dowodem GDB** (patrz §4)

---

## 1. TL;DR dla agenta

| Pytanie | Odpowiedź |
|---------|-----------|
| Czy init fazy od `GetNow()` to bug? | **Nie** — ten sam SubMac na 52840 i nRF54; opis w `CSL_INIT_PHASE_GETNOW.md` |
| Czy 52840 ma fix fazy w platformie? | **Nie** — `radio.c` tylko przekazuje `ReceiveAt` + spójny RTC |
| Co psuje nRF54? | **Hipoteza:** SubMac planuje okno poprawnie, ale **faktyczne otwarcie DRX ≠ `winStart`** (platforma + driver SL + margines schedulingu) |
| Czy jestem pewny? | **~75% confidence** na hipotezę platformy; **100%** na to, że pętla CSL (§2) nie zamyka się |
| Co zrobić najpierw? | GDB: porównać `T_plan` vs `T_sched` vs `T_fire` (§6) — 30 min, bez zmian w kodzie |

---

## 2. Fakty potwierdzone (nie spekulacja)

### 2.1 Objawy (GDB + pcap + logi)

| Obserwacja | Wartość | Źródło |
|------------|---------|--------|
| DRX schedule OK | `csl_receive_at_ok == drx_receive_attempt_ok == hw_task_prepare_ok` | GDB Run 2 |
| RX w oknie CSL | **0** | `csl_rx_from_parent_in_window = 0` |
| OT sync | **0** | `csl_sync_from_rx_enter = 0` |
| DRX timeout | **100%** | `csl_drx_timeout_enter == csl_receive_at_ok` |
| Phase gap vs parent | **3–17 ms** przy oknie ~**1 ms** | pcap + debug phase stats |
| Init phase | losowa względem parenta (~225 ms / ~408 ms mod 500 ms) | `last_csl_init_phase_us` |
| Ping / Child Update | chwilowo stabilizuje | logi użytkownika |
| Run 1 (CSL przed attach) | peer `0xFFFE`, gorzej | GDB |
| Run 2 (attach → CSL) | peer `0x0400`, nadal 100% timeout | GDB |

### 2.2 Co z tego wynika (pewne)

1. **HW/D RX „otwiera się”** — problem nie jest „driver w ogóle nie schedule'uje” (stan po fixach od sierpnia; wcześniej było 367/368 fail — patrz handoff 20260817).
2. **W zaplanowanym oknie nie ma ramki parenta** — albo parent nie nadaje wtedy, albo radio słucha w innym momencie absolutnym.
3. **`sync_from_rx=0`** — SubMac nigdy nie dostał SecEnh-ACK od parenta w ścieżce CSL → pętla z `CSL_INIT_PHASE_GETNOW.md` §8 **nie domknęła się**.
4. **Init od `GetNow()` sam nie tłumaczy gapu 3–17 ms** — okno ~900 µs (§3.2) nie pokryje takiego offsetu bez adaptacji parenta.

### 2.3 Model OT (pewny — z notatki i upstream SubMac)

Po `SetCslParams()` child:
- startuje od `GetNow()` (faza arbitralna względem parenta),
- raportuje fazę parentowi przez **CSL IE** (`getCslPhase()`),
- **parent ma nadawać wg fazy childa** (mechanizm 1),
- po trafionym RX: **`sync_from_rx`** (SecEnh-ACK) doprecyzowuje `mCslLastSync`.

**52840 nie robi nic więcej w platformie** — patrz `CSL_PHASE_SYNC_NRF52840.md`.

---

## 3. Porównanie kodu 52840 vs nRF54 (fakty z diff)

### 3.1 Identyczne (nie szukać buga tutaj)

| Element | Plik |
|---------|------|
| SubMac CSL logic | `openthread/src/core/mac/sub_mac_csl_receiver.cpp` |
| `getCslPhase()` formuła | `src/src/radio.c` vs `src/nrf54l15/radio_nrf54.c` |
| `otPlatRadioGetNow()` → `otPlatTimeGet()` | oba |
| `CSL_UNCERT=20`, `MIN_RECEIVE_ON_AHEAD=104` | config obu platform |

### 3.2 Różnice istotne dla CSL (fakty)

| Aspekt | nRF52840 (`ot-nrf528xx`) | nRF54L15 (`ot-nrf528xx`) |
|--------|--------------------------|---------------------------|
| **`CSL_RECEIVE_TIME_AHEAD`** | **2000 µs** | **512 µs** |
| **Lead przy wywołaniu `ReceiveAt`** | ~**2000 µs** przed `winStart` | ~**512 µs** |
| **Platforma `ReceiveAt`** | `receive_at(aStart−1000, 1000, dur, ch)` | `receive_at(rxTime, dur, ch, id)` + unwrap 64-bit |
| **`SAFE_DELTA`** | **1000 µs** (platforma) | **brak** |
| **Czas w przeszłości** | driver zwraca fail | **`rxTime = now`** (late clamp) |
| **Timer radia** | `lp_timer` na **RTC2** (ten sam co alarm) | **GRTC** CC2 + **CC8 hw_task/DPPI** |
| **`GetRxTimestamp`** | unwrap 32→64 + `end_to_phr_convert` | surowy 64-bit GRTC, bez convert |
| **Driver API** | `receive_at(t0, dt, timeout, ch)` | `receive_at(rx_time, timeout, ch, id)` |
| **Cancel DRX** | `receive_at_cancel()` | `receive_at_scheduled_cancel(DRX_SLOT_RX)` |
| **HFCLK** | prostszy wake | XO ramp ~152 µs (CSL-F1 HFCLK hold) |

### 3.3 Ważna korekta: szerokość okna SubMac

`winStart` i `winDuration` liczone przez SubMac są **takie same** na obu platformach (różnica 2000 vs 512 kasuje się przy `aTimeAhead -= kCslReceiveTimeAhead`).

Przy init (`elapsed ≈ 0`):
```
winStart    ≈ sample − 504 µs
winDuration ≈ 904 µs   (504 + ~400 uncertainty)
```

**Nie** ~2900 µs — wartość ~2500 µs w notatce to lead **timera** CSL, nie szerokość okna DRX.

---

## 4. Hipoteza główna (P1) — „zerwana self-consistency platformy”

### 4.1 Teza

SubMac + OT planują okno przy **`winStart`** i raportują fazę spójną z **`mCslSampleTime`**.  
Na nRF54 **radio faktycznie słucha przy `winStart + Δ`**, gdzie **Δ** wynika z:

1. za krótkiego lead (~512 µs vs ~2000 µs na 52840),
2. braku platformowego `SAFE_DELTA` (1000 µs),
3. **`late clamp`** w `otPlatRadioReceiveAt` (`rxTime < now → rxTime = now`),
4. opóźnienia ścieżki **delayed_trx → hw_task CC8 → HFCLK ramp → RADIO RX**,
5. ewentualnie main-loop latency (FTD na chipie).

**Skutek:** parent (52840) dostaje CSL IE z fazą **f(T)**, nadaje wg **f(T)**, ale child słucha przy **f(T)+Δ** → miss → brak SecEnh-ACK → `sync_from_rx=0` → wieczna pętla.

### 4.2 Dlaczego to pasuje do danych

| Obserwacja | Pasuje? |
|------------|---------|
| DRX OK, 100% timeout | tak — słucha, ale „w złym miejscu” |
| gap 3–17 ms >> okno ~1 ms | tak — nie da się trafić bez korekty absolutnego czasu |
| `likely_phase == timeout` 100% | tak |
| 52840 child + ten sam leader działa | tak — platforma 52840 ma większy margines + SAFE_DELTA |
| ping pomaga | częściowo — wymusza wymianę ramek, nie naprawia Δ |

### 4.3 Confidence

| Aspekt | Confidence |
|--------|------------|
| Pętla CSL nie zamyka się (`sync_from_rx=0`) | **100%** (GDB) |
| Init `GetNow()` nie jest root cause | **95%** (model OT + ten sam SubMac) |
| Platforma psuje absolutny timing DRX | **~75%** (diff kodu + staty lead; **brak pomiaru T_fire vs winStart**) |
| Parent nie adaptuje fazy (alternatywa) | **~25%** (pcap: parent na stałej fazie — może być skutek, nie przyczyna) |
| Ręczna korekta fazy do parenta | workaround bootstrapu, **nie** wzorzec 52840 |

### 4.4 Hipotezy odrzucone / poboczne (P2–P3)

| Hipoteza | Status |
|----------|--------|
| „Losowa faza init to bug OT” | **odrzucona** — zamierzone, 52840 też tak startuje |
| „52840 ma ukryty fix fazy w radio.c” | **odrzucona** — diff potwierdza brak |
| „Dwa różne zegary GRTC vs OT” | **mało prawdopodobna** — ten sam `nrfx_grtc_syscounter_get()` |
| „Timestamper RX zły” | **poboczna** — `rx_timestamp_ok>0`; wpływa na sync **po** trafieniu |
| „Skip ReceiveAt w kStateReceive” | **poboczna** — wspólny SubMac; opóźnia bootstrap |
| „CSL przed attach (peer 0xFFFE)” | **potwierdzona** jako pogorszenie Run 1 |

---

## 5. Co NIE robić (feedback od użytkownika)

- **Nie** implementować rozbudowanej ręcznej korekty fazy do fazy parenta w platformie — to workaround, odwraca model OT (parent podąża za childem).
- **Nie** modyfikować SubMac / submodułów OT na razie — użytkownik chce fix platformy.
- **Nie** zakładać, że problem to wyłącznie „losowy init” — GDB pokazuje stały gap faz, nie chaos.

---

## 6. Pierwszy krok weryfikacji (OBOWiĄZKOWY przed fixem)

### 6.1 GDB na nRF54 — jeden halt w trakcie CSL

Symbol: `g_nrf54_debug_stats` (adres z `ot-cli-ftd.map` / `ot-rcp.map`).

```bash
p/x g_nrf54_debug_stats.last_csl_win_start              # T_plan  — od SubMac
p/x g_nrf54_debug_stats.last_csl_receive_at_arg_start   # T_sched — do drivera (po clamp)
p/x g_nrf54_debug_stats.last_grtc_at_csl_receive_at     # T_now   — przy ReceiveAt
p/x g_nrf54_debug_stats.last_hw_task_cc_at_update_ppi   # T_fire  — CC8 hw_task
p/x g_nrf54_debug_stats.last_csl_start_minus_now_us     # lead = T_plan − T_now
p g_nrf54_debug_stats.csl_plat_win_lead_short           # lead < 400 µs
p g_nrf54_debug_stats.csl_plat_win_in_past              # T_plan < T_now
```

**Kryterium potwierdzenia hipotezy P1:**

```
Δ_sched  = T_sched − T_plan     →  > 0 jeśli late clamp
Δ_fire   = T_fire  − T_plan     →  > ~100 µs jeśli hw_task/ramp spóźnia
lead     = T_plan − T_now       →  często < 512 µs (porównaj z csl_plat_win_lead_short)
```

**Kryterium obalenia hipotezy P1:**

```
T_fire ≈ T_plan (± kilkadziesiąt µs)  AND  lead ≥ 1500 µs  AND  nadal in_window=0
→ szukać: parent nie adaptuje CSL IE, SecEnh-ACK, kanał, peer
```

### 6.2 GDB na nRF52840 (ten sam scenariusz, kontrola)

Breakpoint w `otPlatRadioReceiveAt`:

```bash
p aStart - otPlatAlarmMicroGetNow()    # oczekiwane: ~1500–2500 µs
p aDuration                            # ~900 µs
```

### 6.3 Pcap (opcjonalnie, 10 min)

Porównaj **przed/po pierwszym Enh-ACK z CSL IE** od childa:
- czy faza TX parenta (52840 leader) **skacze** na fazę childa?
- jeśli **nie** — problem bootstrap CSL IE (zanim platforma)
- jeśli **tak**, ale child nadal missuje — **potwierdzenie P1** (słucha w złym miejscu)

---

## 7. Kierunki fixu (dopiero po §6)

Uporządkowane od najmniej inwazyjnych:

| # | Zmiana | Plik | Uzasadnienie |
|---|--------|------|--------------|
| **A** | `CSL_RECEIVE_TIME_AHEAD`: 512 → **2000** (jak 52840) | `openthread-core-nrf54l15-config.h` | więcej czasu na hw_task przed `winStart` |
| **B** | Dodać **`SAFE_DELTA=1000`** w `otPlatRadioReceiveAt` (odpowiednik 52840 w API 64-bit) | `radio_nrf54.c` | wcześniejsze obudzenie radia |
| **C** | Usunąć / ograniczyć **late clamp** (`rxTime = now`) — zwracać fail jak 52840 | `radio_nrf54.c` | nie przesuwać okna po cichu |
| **D** | Dopasować `GetRxTimestamp` do 52840 (`end_to_phr_convert`) | `radio_nrf54.c` | po pierwszym trafieniu — lepszy `sync_from_rx` |
| **E** | Diff NCS `hw_task_schedule` / cleanup CC8 | `nrf_802154_platform_sl_lptimer*.c` | historyczne 367/368 fail |

**Test A+B razem** = najbliższy parity z 52840, ~5 linii config + kilka linii platformy.

**Kryterium sukcesu po fixie:**

```
csl_rx_from_parent_in_window  > 0
csl_sync_from_rx_enter        > 0
csl_drx_timeout_enter         << csl_receive_at_ok
ping leader→child             stabilny bez ręcznego ping-sync
```

---

## 8. Pliki kluczowe

| Plik | Rola |
|------|------|
| `notes/CSL_INIT_PHASE_GETNOW.md` | model OT — jak 52840 radzi sobie z GetNow() |
| `notes/CSL_PHASE_SYNC_NRF52840.md` | architektura CSL 52840 |
| `notes/csl_receive_at_handoff_20260817.md` | wcześniejszy DRX fail 367/368 |
| `/home/anzd/src/ot-nrf528xx/src/src/radio.c` | referencja 52840 ReceiveAt |
| `/home/anzd/src/ot-nrf528xx/src/src/alarm.c` | referencja RTC2 / lp_timer |
| `src/nrf54l15/radio_nrf54.c` | nRF54 ReceiveAt, GetRxTimestamp, getCslPhase |
| `src/nrf54l15/alarm_nrf54.c` | GRTC alarm OT |
| `src/nrf54l15/openthread-core-nrf54l15-config.h` | CSL_RECEIVE_TIME_AHEAD=512 |
| `third_party/nrf54/platform/nrf_802154_platform_sl_lptimer.c` | CC8 hw_task |
| `third_party/nrf54/platform/nrf54_csl_debug.c` | staty fazy (GDB) |
| `third_party/nrf54/platform/nrf54_debug_stats.h` | wszystkie liczniki |
| `openthread/src/core/mac/sub_mac_csl_receiver.cpp` | SubMac (nie ruszać na razie) |

---

## 9. Odrzucone propozycje z wcześniejszej sesji

| Propozycja | Dlaczego odrzucona |
|------------|-------------------|
| ~200-liniowy fix fazy do parenta w `radio_nrf54.c` | workaround, odwraca model OT, zbyt rozbudowany |
| ~40-liniowy minimalny phase shift | nadal workaround; najpierw §6 |
| Zmiany w SubMac `SetCslParams` | poza scope (user: tylko platforma) |

---

## 10. Diagram stanu (handoff)

```
                    ┌─────────────────────────────────────┐
                    │ SetCslParams: phase = GetNow()      │
                    │ (OK — tak samo na 52840 i nRF54)    │
                    └──────────────┬──────────────────────┘
                                   │
                    ┌──────────────▼──────────────────────┐
                    │ SubMac: winStart, winDuration       │
                    │ getCslPhase() → CSL IE do parenta   │
                    └──────────────┬──────────────────────┘
                                   │
              ┌────────────────────┼────────────────────┐
              │ 52840              │                     │ nRF54 (hipoteza)
              ▼                    │                     ▼
    receive_at(start−1ms,…)        │         receive_at(rxTime,…) + clamp
    lead ~2ms + SAFE_DELTA       │         lead ~512ms, brak SAFE_DELTA
              │                    │                     │
              ▼                    │                     ▼
    RX @ winStart ✓                │         RX @ winStart+Δ ✗
              │                    │                     │
              ▼                    │                     ▼
    SecEnh-ACK → sync_from_rx      │         timeout 100%, sync=0
              │                    │                     │
              └────────────────────┴─────────────────────┘
                                   │
                    ┌──────────────▼──────────────────────┐
                    │ NASTĘPNY AGENT: §6 GDB → fix A/B/C  │
                    └─────────────────────────────────────┘
```

---

## 11. Jednozdaniowy brief dla agenta

**nRF54L15 psuje CSL nie przez losowy init fazy (ten sam SubMac co 52840), lecz przez prawdopodobne rozjechane wykonanie `otPlatRadioReceiveAt` względem planu SubMac — najpierw zmierz `T_plan` vs `T_sched` vs `T_fire` w GDB (§6), potem wyrównaj do 52840: `CSL_RECEIVE_TIME_AHEAD=2000`, `SAFE_DELTA`, ogranicz late clamp.**
