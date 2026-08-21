# CSL / SSED — dlaczego nRF52840 łapie dobrą fazę i synchronizację

> Notatka oparta na kodzie platformy w tym repozytorium (`src/src/radio.c`, `src/src/alarm.c`, `src/nrf52840/`).
> Logika synchronizacji fazy CSL **nie jest w platformie** — jest w OpenThread SubMac (`sub_mac_csl_receiver.cpp`).
> Platforma nRF52840 dostarcza spójne API czasu i timed RX; to wystarcza, żeby SubMac mógł utrzymać sync.

---

## Podział odpowiedzialności

| Warstwa | Plik(i) | Rola |
|---------|---------|------|
| **OpenThread SubMac** | `sub_mac_csl_receiver.cpp` (upstream OT) | Init fazy, liczenie okien CSL, sync z RX parenta, wywołania `UpdateCslSampleTime` / `ReceiveAt` |
| **Platforma nRF52840** | `src/src/radio.c`, `src/src/alarm.c` | Jeden zegar RTC, timed RX, timestamp RX, przechowanie `sCslSampleTime` dla CSL IE |

Platforma **nie implementuje** `UpdateCslLastSync` ani nie czyta CSL IE parenta przy attach. Robi tylko to, co SubMac każe — ale robi to na **jednym, spójnym zegarze**.

---

## Przepływ end-to-end

```
SubMac                          nRF52840 (RCP)
──────                          ──────────────
SetCslParams()
  mCslSampleTime = GetNow()  →  otPlatRadioGetNow() = otPlatTimeGet()
  mCslLastSync   = to samo
  HandleCslTimer()

co okres CSL:
  HandleCslReceiveAt()
    winStart, winDuration
    UpdateCslSampleTime(t)   →  sCslSampleTime = t
    ReceiveAt(ch, start, dur) → nrf_802154_receive_at(start-1ms, 1ms, dur)

parent RX:
  GetRxTimestamp()           →  mRxInfo.mTimestamp (ten sam RTC)
  ReceiveDone(frame)
    UpdateCslLastSyncTimestamp()
      mCslLastSync = frame->GetTimestamp()
      RestartCslTimerAfterSyncUpdate()
```

---

## 1. Jeden zegar RTC — fundament synchronizacji

Na nRF52840 **wszystkie** źródła czasu używane przez CSL pochodzą z RTC2 (`alarm.c`):

| API | Implementacja | Użycie w CSL |
|-----|---------------|--------------|
| `otPlatRadioGetNow()` | `otPlatTimeGet()` → `nrf5AlarmGetCurrentTime()` | SubMac: init fazy, planowanie okien |
| `otPlatAlarmMicroGetNow()` | ten sam RTC (32-bit) | `getCslPhase()` — raport fazy do parenta |
| `nrf_802154_lp_timer_*` | kanał RTC `k802154Timer` | timed TX/RX w driverze 802.15.4 |
| `nrf_802154_lp_timer_sync_*` | kanał RTC `k802154Sync` | synchronizacja wewnątrz drivera |
| Timestamp RX | `GetRxTimestamp()` → `nrf5AlarmGetCurrentTime()` + konwersja HW | `mCslLastSync` po odebraniu parenta |

**Skutek:** SubMac planuje okno na `winStart`, RCP otwiera je o `winStart`, a timestamp odebranej ramki jest w tej samej skali. Bez tego `sync_from_rx` nigdy nie zadziała — okna i timestampy „mówią innym językiem”.

Pliki: `src/src/alarm.c` (RTC2, 4 kanały compare), `src/nrf52840/platform-config.h` (`RTC_INSTANCE = NRF_RTC2`).

---

## 2. Init fazy od `GetNow()` — to normalne, nie bug

Przy włączeniu CSL SubMac robi (upstream OT):

```cpp
mCslSampleTime.SetToNow(Get().GetNow());
mCslLastSync = mCslSampleTime.GetAsTime64();
HandleCslTimer();
```

Na nRF52840:

```c
uint64_t otPlatRadioGetNow(...) { return otPlatTimeGet(); }
uint64_t otPlatTimeGet(void)     { return nrf5AlarmGetCurrentTime(); }
```

**Początkowa faza jest losowa względem parenta** (np. 408 ms vs 225 ms w okresie 500 ms). Parent nie przekazuje swojej fazy CSL przy attach — child startuje od własnego zegara.

To nie jest problem, dopóki **później nastąpi sync z RX**. Pierwsze okna mogą missować leadera — to oczekiwane.

---

## 3. Sync z RX parenta — SubMac + timestamp platformy

Synchronizacja fazy następuje, gdy SubMac dostanie ramkę parenta z **SecEnh-ACK**:

```cpp
if ((mCslPeriod > 0) && aFrame->IsAckedWithSecEnhAck())
{
    mCslLastSync = aFrame->GetTimestamp();
    RestartCslTimerAfterSyncUpdate();
}
```

Timestamp ramki pochodzi z platformy nRF52840:

```c
// radio.c — callback RX
receivedFrame->mInfo.mRxInfo.mTimestamp = GetRxTimestamp(time, p_data[0]);

static uint64_t GetRxTimestamp(uint32_t aTime, uint8_t aLength)
{
    uint64_t now = nrf5AlarmGetCurrentTime();
    aTime = nrf_802154_timestamp_end_to_phr_convert(aTime, aLength);
    return now + (uint64_t)(int32_t)(aTime - (uint32_t)now);
}
```

`GetRxTimestamp()` mapuje sprzętowy timestamp 802.15.4 (SFD) na ten sam RTC co `GetNow()`. Dzięki temu SubMac wie, **kiedy naprawdę** parent nadawał, i przesuwa kolejne okna.

`RestartCslTimerAfterSyncUpdate()` cofa `mCslSampleTime` o jeden okres i ponownie woła `HandleCslTimer()` — okna są przeliczane z nowym `mCslLastSync`.

---

## 4. Planowanie okien CSL — `ReceiveAt` + marginesy

SubMac co okres:

1. Liczy `winStart` / `winDuration` (drift zegara + uncertainty parent i child)
2. Woła `otPlatRadioUpdateCslSampleTime(nextSampleTime)` — RCP zapisuje w `sCslSampleTime`
3. Woła `otPlatRadioReceiveAt(channel, winStart, winDuration)`

Platforma nRF52840:

```c
otError otPlatRadioReceiveAt(..., uint32_t aStart, uint32_t aDuration)
{
    result = nrf_802154_receive_at(aStart - SAFE_DELTA, SAFE_DELTA, aDuration, aChannel);
    // SAFE_DELTA = 1000 µs — radio budzi się 1 ms wcześniej
}
```

Po timeoutie okna (brak ramki) → sleep **bez błędu**:

```c
if (error == NRF_802154_RX_ERROR_DELAYED_TIMEOUT || error == NRF_802154_RX_ERROR_TIMESLOT_ENDED)
{
    sReceiveError = OT_ERROR_NONE;
    setPendingEvent(kPendingEventSleep);
}
```

Konfiguracja marginesów (`src/nrf52840/openthread-core-nrf52840-config.h`):

| Parametr | Wartość | Znaczenie |
|----------|---------|-----------|
| `OPENTHREAD_CONFIG_CSL_RECEIVE_TIME_AHEAD` | 2000 µs | Czas na ramp-up radia przed oknem |
| `OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AHEAD` | 104 µs | Preamble + SFD + PHR |
| `OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AFTER` | 0 µs | Radio sam przedłuża okno po detekcji SHR |
| `CSL_UNCERT` (platforma) | 20 (= ±200 µs) | Niepewność raportowana parentowi |
| `XTAL_ACCURACY` (alarm.c) | 40 ppm×2 → CSL accuracy 20 ppm | Dokładność zegara |

---

## 5. `sCslSampleTime` i `getCslPhase()` — raport fazy do parenta

Platforma **nie synchronizuje** fazy — tylko przechowuje sample time od SubMaca:

```c
void otPlatRadioUpdateCslSampleTime(..., uint32_t aCslSampleTime)
{
    sCslSampleTime = aCslSampleTime;
}
```

Faza w CSL IE (Enh-ACK / TX) liczona jest lokalnie:

```c
static uint16_t getCslPhase(void)
{
    uint32_t curTime       = otPlatAlarmMicroGetNow();
    uint32_t cslPeriodInUs = sCslPeriod * OT_US_PER_TEN_SYMBOLS;
    uint32_t diff = (cslPeriodInUs - (curTime % cslPeriodInUs)
                   + (sCslSampleTime % cslPeriodInUs)) % cslPeriodInUs;
    return (uint16_t)(diff / OT_US_PER_TEN_SYMBOLS + 1);
}
```

SubMac co okres aktualizuje `sCslSampleTime`, więc faza raportowana parentowi jest spójna z planowanymi oknami — **o ile** `mCslLastSync` jest poprawny.

---

## 6. Checklist — dlaczego na nRF52840 to działa

- [x] **Jeden zegar RTC2** dla `GetNow`, `AlarmMicroGetNow`, lp_timer 802.15.4 i timestampów RX
- [x] **Timed RX** przez `nrf_802154_receive_at` z `SAFE_DELTA = 1 ms` head-start
- [x] **Timestamp RX w tej samej skali** co planowanie okien (`GetRxTimestamp`)
- [x] **Sync po pierwszym trafieniu** — SubMac ustawia `mCslLastSync` z timestampu ramki parenta
- [x] **Init od GetNow() jest OK** — pierwsze okna mogą missować; po `sync_from_rx` faza się stabilizuje
- [x] **Timeout okna → sleep**, nie error — cykl CSL kontynuuje się mimo missów

---

## 7. Diagnostyka — mapowanie counterów GDB

| Counter / objaw | Co oznacza | Gdzie szukać |
|-----------------|------------|--------------|
| `init_phase` losowa | SubMac init od `GetNow()`, nie parent CSL IE | Normalne — czekaj na sync z RX |
| `sync_from_rx == 0` | SubMac nie dostał SecEnh-ACK od parenta z poprawnym timestamp | Parent, SecEnh-ACK, timestamp w RTC |
| `in_window == 0` | Skutek braku sync — okna w złej fazie | Naprawia się po `sync_from_rx > 0` |
| `likely_phase == timeout` | `ReceiveAt` timeout → sleep (platform OK) | Problem fazy/sync, nie HW/DRX |

---

## 8. Co platforma nRF52840 **nie robi**

- Nie czyta CSL IE parenta przy attach
- Nie implementuje `UpdateCslLastSync` (to SubMac)
- Nie liczy okien CSL (to SubMac)
- Nie naprawia rozjechanych zegarów — zakłada, że wszystkie API czasu są spójne

**Wniosek:** nRF52840 „łapie dobrą fazę”, bo daje SubMacowi **wiarygodny, jednolity czas** i **wiarygodne timestampy RX**. Reszta to logika OpenThread upstream.

---

## Pliki referencyjne w repo

| Plik | Co zawiera |
|------|------------|
| `src/src/radio.c` | `GetRxTimestamp`, `otPlatRadioReceiveAt`, `otPlatRadioGetNow`, `getCslPhase`, `otPlatRadioUpdateCslSampleTime` |
| `src/src/alarm.c` | RTC2, `otPlatTimeGet`, `otPlatAlarmMicroGetNow`, `nrf_802154_lp_timer_*` |
| `src/nrf52840/platform-config.h` | `RTC_INSTANCE = NRF_RTC2` |
| `src/nrf52840/openthread-core-nrf52840-config.h` | `CSL_RECEIVE_TIME_AHEAD`, `MIN_RECEIVE_ON_*` |
| OpenThread upstream | `src/core/mac/sub_mac_csl_receiver.cpp` — pełna logika sync |
