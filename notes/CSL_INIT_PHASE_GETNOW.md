# CSL init fazy od `GetNow()` — dokładny opis i mechanizmy wyrównania

> Notatka wyłącznie o tym, **skąd bierze się początkowa faza CSL**, co znaczy że jest
> „losowa”, i **jak OpenThread + platforma nRF52840** radzą sobie bez fazy parenta przy attach.
>
> Logika: OpenThread SubMac (`sub_mac_csl_receiver.cpp`, upstream).
> Platforma: `src/src/radio.c`, `src/src/alarm.c` (nRF52840).

---

## 1. Problem, który rozwiązujemy

SSED (child z CSL) musi wiedzieć, **kiedy otworzyć krótkie okno RX**, żeby odebrać ramkę od parenta.
Parent musi wiedzieć, **kiedy nadać** do tego konkretnego childa.

Obie strony muszą się zgrać co do **fazy** — offsetu w okresie CSL (np. co 500 ms, w którym
momencie te 500 ms jest „punkt zerowy”).

**Pytanie:** skoro child **nie dostaje fazy parenta przy attach**, skąd bierze początkową fazę
i jak potem utrzymuje synchronizację?

**Odpowiedź OpenThread:** child startuje od własnego `GetNow()`, parent dostosowuje się do
fazy zgłoszonej przez childa, a potem child doprecyzowuje timing po każdym trafionym RX.

---

## 2. Kluczowe zmienne — kto za co odpowiada

| Zmienna | Warstwa | Znaczenie |
|---------|---------|-----------|
| `mCslSampleTime` | SubMac | Absolutny czas (µs) następnego „sample point” — kiedy child *oczekuje* MHR ramki parenta |
| `mCslLastSync` | SubMac | Ostatni znany punkt synchronizacji — kiedy ostatnio *wiemy*, że parent nadawał |
| `mCslPeriod` | SubMac | Okres CSL w jednostkach 160 µs (10 symboli O-QPSK) |
| `sCslSampleTime` | Platforma (`radio.c`) | Kopia `mCslSampleTime` przekazana przez `otPlatRadioUpdateCslSampleTime()` — używana w `getCslPhase()` |
| CSL IE phase | Platforma (`getCslPhase()`) | Faza raportowana parentowi w Enh-ACK / TX (jednostki 160 µs) |
| `winStart`, `winDuration` | SubMac | Okno DRX wywoływane przez `otPlatRadioReceiveAt()` |

**Relacja init:**

```
SetCslParams():
  mCslSampleTime ← GetNow()        // np. 1 234 567 890 µs
  mCslLastSync   ← mCslSampleTime  // to samo — brak wcześniejszej wiedzy o parentcie
  HandleCslTimer()                 // planuje pierwsze okno
```

Po init **`mCslSampleTime == mCslLastSync`**. Child nie ma jeszcze informacji o tym, kiedy
parent *naprawdę* nadaje — oba liczniki wskazują ten sam moment włączenia CSL.

---

## 3. Dokładnie co robi `SetCslParams()`

Wywoływane, gdy MLE skonfiguruje CSL (period, kanał, adres childa). Kod upstream OT:

```cpp
void SubMac::SetCslParams(uint16_t aPeriod, uint8_t aChannel,
                          ShortAddress aShortAddr, const ExtAddress &aExtAddr)
{
    mCslChannel = aChannel;
    mCslPeerShort = aShortAddr;
    Get().EnableCsl(aPeriod, aShortAddr, aExtAddr);  // → otPlatRadioEnableCsl na RCP

    mCslPeriod = aPeriod;
    mCslTimer.Stop();

    if (mCslPeriod > 0)
    {
        mCslSampleTime.SetToNow(Get());               // ← INIT FAZY
        mCslLastSync = mCslSampleTime.GetAsTime64();  // ← ten sam moment
        HandleCslTimer();                             // ← pierwsze okno CSL
    }
}
```

Na nRF52840 `Get().GetNow()` to:

```c
otPlatRadioGetNow() → otPlatTimeGet() → nrf5AlarmGetCurrentTime()   // RTC2
```

### Co to oznacza w praktyce

Przy okresie CSL = 500 ms (3125 × 160 µs):

- Child A włącza CSL o `GetNow() mod 500ms = 408 ms` → jego faza startowa to **408 ms**
- Child B włącza CSL o `GetNow() mod 500ms = 225 ms` → jego faza startowa to **225 ms**

To **nie jest RNG** — to deterministyczna konsekwencja momentu attach / konfiguracji CSL.
Dwa identyczne urządzenia attachujące się w różnych chwilach dostaną różną fazę init.

### Czego OpenThread **nie robi** przy init

- Nie czyta CSL IE z ramek parenta
- Nie kopiuje fazy parenta z MLE / Child Update Response
- Nie synchronizuje się do globalnego czasu sieci
- Nie czeka na pierwszą ramkę parenta przed startem okien CSL

Init jest **natychmiastowy** — po `SetCslParams()` od razu leci `HandleCslTimer()`.

---

## 4. Co znaczy „losowa faza” — precyzyjna definicja

| Aspekt | Opis |
|--------|------|
| **Losowa względem parenta** | Tak — offset w okresie zależy od momentu `SetCslParams()`, parent o tym nie decyduje |
| **Losowa w sensie RNG** | Nie — powtarzalna przy tym samym momencie włączenia |
| **Losowa między urządzeniami** | Tak — każdy child ma własną fazę init |
| **Niespójna wewnętrznie** | **Nie** — `mCslSampleTime`, `mCslLastSync` i raportowana faza startują z tego samego `GetNow()` |

**„Losowa” = arbitralna względem absolutnego zegara parenta**, nie chaotyczna i nie
oznaczająca braku planu. Child ma spójny plan — parent ma się do niego dostosować.

---

## 5. Mechanizm 1 — Parent nadaje wg fazy **zgłoszonej przez childa**

To najważniejszy punkt: **child nie musi znać fazy parenta**, bo to parent dostosowuje swój
harmonogram transmisji do childa.

### Jak child zgłasza fazę parentowi

1. SubMac co okres woła `UpdateCslSampleTime(mCslSampleTime)` → platforma zapisuje `sCslSampleTime`
2. Przy Enh-ACK / TX platforma woła `getCslPhase()` i wstawia CSL IE:

```c
// radio.c
static uint16_t getCslPhase(void)
{
    uint32_t curTime       = otPlatAlarmMicroGetNow();
    uint32_t cslPeriodInUs = sCslPeriod * OT_US_PER_TEN_SYMBOLS;
    uint32_t diff = (cslPeriodInUs - (curTime % cslPeriodInUs)
                   + (sCslSampleTime % cslPeriodInUs)) % cslPeriodInUs;
    return (uint16_t)(diff / OT_US_PER_TEN_SYMBOLS + 1);
}

otMacFrameSetCslIe(&ackFrame, sCslPeriod, getCslPhase());
```

3. Parent (FTD z CSL transmitter) odczytuje CSL IE i planuje transmisje do childa w tej fazie
   (± uncertainty z CSL IE)

### Self-consistency od pierwszej chwili

```
Init (T = GetNow()):
  mCslSampleTime = T
  sCslSampleTime = T  (po pierwszym HandleCslTimer)
  getCslPhase()    ≈ f(T)   // faza wynikająca z T

Parent dostaje phase ≈ f(T) i nadaje wg f(T)
Child otwiera okno wokół T

→ Para jest spójna wewnętrznie od startu,
  nawet jeśli T jest „losowe” względem innych urządzeń w sieci.
```

**Child nie zgaduje parenta — parent słucha childa.**

To dlatego init od `GetNow()` nie jest bugiem: wystarczy, że obie strony mówią tym samym
językiem (faza z CSL IE childa = okno childa = transmisja parenta).

---

## 6. Mechanizm 2 — Szerokie okna RX na początku (i stale)

SubMac nie otwiera okna w jednym punkcie — otwiera **przedział czasu** wokół sample point.

### Obliczanie marginesów — `GetCslWindowEdges()`

```cpp
void SubMac::GetCslWindowEdges(uint32_t &aAhead, uint32_t &aAfter)
{
    uint32_t semiPeriod = mCslPeriod * kUsPerTenSymbols / 2;
    uint32_t elapsed = mCslSampleTime - mCslLastSync;   // czas od ostatniego sync

    semiWindow = DetermineClockDrift(elapsed)             // dryft zegara
               + ConvertUncertainty(parent + child);     // ±200µs × 2

    aAhead = Min(semiPeriod, semiWindow + kMinReceiveOnAhead + kCslReceiveTimeAhead);
    aAfter = Min(semiPeriod, semiWindow + kMinReceiveOnAfter);
}
```

### Wartości na nRF52840

| Składnik | Wartość | Źródło |
|----------|---------|--------|
| `kCslReceiveTimeAhead` | 2000 µs | `openthread-core-nrf52840-config.h` |
| `kMinReceiveOnAhead` | 104 µs | preamble + SFD + PHR |
| `kMinReceiveOnAfter` | 0 µs | radio sam przedłuża po SHR |
| child uncertainty | ±200 µs | `CSL_UNCERT = 20` w `radio.c` |
| parent uncertainty | ±200 µs | z CSL IE parenta / domyślne |
| clock drift przy init | ≈ 0 µs | `elapsed ≈ 0` bo `mCslLastSync == mCslSampleTime` |

### Przy init (elapsed ≈ 0)

```
semiWindow ≈ 0 + 400 µs (uncertainty) = ~400 µs
aAhead     ≈ 400 + 104 + 2000 = ~2500 µs przed sample point
aAfter     ≈ 400 µs po sample point
winDuration ≈ 2900 µs
```

Okno jest **asymetryczne** — większość marginesu jest *przed* sample point (ramp-up radia
+ łapanie opóźnionego TX parenta). To zgodne z doświadczeniem OpenThread: parent nadaje
**trochę po** idealnym punkcie fazy (OT issue #5092).

### Diagram okna przy init

```
         winStart                          sample point              winEnd
            │                                    │                      │
  ──────────┼────────────────────────────────────┼──────────────────────┼──
            │←──── aAhead (~2500 µs) ────────────→│←── aAfter (~400 µs) ─→│
            │                                    │                      │
            │         parent TX zwykle tutaj ────┤                      │
            │         (po sample point, w oknie) │                      │
```

Platforma dodaje jeszcze `SAFE_DELTA = 1000 µs` — radio budzi się 1 ms przed `winStart`:

```c
nrf_802154_receive_at(aStart - SAFE_DELTA, SAFE_DELTA, aDuration, aChannel);
```

---

## 7. Mechanizm 3 — Korekta po trafionym RX (`sync_from_rx`)

Init + self-consistency + szerokie okna wystarczają na start. **Korekta** doprecyzowuje
timing w kolejnych cyklach.

### Kiedy następuje sync

SubMac woła `UpdateCslLastSyncTimestamp()` po każdym RX. Sync aktualizuje `mCslLastSync`
**tylko** gdy:

```cpp
if ((mCslPeriod > 0) && aFrame->IsAckedWithSecEnhAck())
{
    mCslLastSync = aFrame->GetTimestamp();
    RestartCslTimerAfterSyncUpdate();
}
```

Warunki:

1. CSL jest włączone (`mCslPeriod > 0`)
2. Ramka została potwierdzona **SecEnh-ACK** od parenta
3. Timestamp ramki pochodzi z platformy w tej samej skali co `GetNow()`

### Skąd bierze się timestamp

```c
// radio.c — callback nrf_802154_received_timestamp_raw
receivedFrame->mInfo.mRxInfo.mTimestamp = GetRxTimestamp(time, p_data[0]);

static uint64_t GetRxTimestamp(uint32_t aTime, uint8_t aLength)
{
    uint64_t now = nrf5AlarmGetCurrentTime();   // ten sam RTC co GetNow()
    aTime = nrf_802154_timestamp_end_to_phr_convert(aTime, aLength);
    return now + (uint64_t)(int32_t)(aTime - (uint32_t)now);
}
```

`mCslLastSync` = **rzeczywisty czas nadawania parenta** (SFD/MHR), nie `GetNow()` w momencie
odebrania ramki w software.

### Co robi `RestartCslTimerAfterSyncUpdate()`

```cpp
void SubMac::RestartCslTimerAfterSyncUpdate(void)
{
    if (RadioSupports(kCapReceiveTiming) && mCslTimer.IsRunning())
    {
        mCslTimer.Stop();
        mCslSampleTime -= periodUs;   // cofnij o jeden okres
        HandleCslTimer();           // przelicz bieżący okres z nowym mCslLastSync
    }
}
```

**Efekt:** bieżący cykl CSL jest **przeliczany od nowa** z zaktualizowanym `mCslLastSync`.
Kolejne okno (`winStart`, `winDuration`) uwzględnia rzeczywisty czas nadawania parenta.

### Co korekta naprawia

| Źródło błędu | Jak sync_from_rx pomaga |
|--------------|-------------------------|
| Dryft zegara child vs parent | `elapsed` w `GetCslWindowEdges()` rośnie kontrolowanie; okresowy sync resetuje bazę |
| Kwantyzacja fazy (160 µs) | Timestamp ma rozdzielczość µs; faza w CSL IE jest zaokrąglona |
| Różnica „zgłoszona faza” vs „faktyczny TX” | `mCslLastSync` = faktyczny TX, nie wyliczona faza |
| Opóźnienie parenta względem fazy | Okno już ma margines „po”; sync koryguje bazę na następne cykle |

### Alternatywna ścieżka sync (TX → Ack)

Gdy **child nadaje** i dostaje Ack z CSL IE:

```cpp
void SubMac::UpdateCslLastSyncTimestamp(TxFrame &aFrame, RxFrame *aAckFrame)
{
    if (aAckFrame != nullptr && aFrame.HasCslIe())
    {
        SetCslLastSyncToNow();   // mCslLastSync = GetNow(), nie timestamp ramki
        RestartCslTimerAfterSyncUpdate();
    }
}
```

To słabsza korekta (używa `GetNow()`, nie timestamp RX), ale też resetuje timer CSL.

---

## 8. Pełna sekwencja — pierwsze 3 okresy CSL

```
T=0: SetCslParams()
     mCslSampleTime = mCslLastSync = GetNow() = 1 000 000 µs
     phase zgłoszona parentowi ≈ f(1 000 000)

T=0+: HandleCslTimer() #1
     winStart = 1 000 000 - 2500 = 997 500 µs
     winDuration ≈ 2900 µs
     ReceiveAt(997 500, 2900)  →  nrf_802154_receive_at(996 500, 1000, 2900)
     UpdateCslSampleTime(1 500 000)   // następny sample point
     sCslSampleTime = 1 500 000

     Parent nadaje wg phase ≈ f(1 000 000) → trafia w okno ✓
     SecEnh-ACK → sync_from_rx:
       mCslLastSync = 1 000 047 µs  (timestamp ramki)
       RestartCslTimerAfterSyncUpdate()

T=0++: HandleCslTimer() #2 (przeliczony)
     elapsed = 1 500 000 - 1 000 047 = 499 953 µs
     semiWindow = drift(499ms) + uncertainty ≈ kilka µs + 400 µs
     winStart lepiej dopasowany do rzeczywistego TX parenta

T=500ms: HandleCslTimer() #3
     cykl się powtarza, co okres sync_from_rx doprecyzowuje
```

Jeśli **pierwsze okno missuje** (timeout):

```c
// radio.c — DELAYED_TIMEOUT nie jest błędem
sReceiveError = OT_ERROR_NONE;
setPendingEvent(kPendingEventSleep);
```

SubMac i tak planuje następne okno. Child **nie rezygnuje** — czeka na trafienie lub
kolejny sync path.

---

## 9. Rola platformy nRF52840 w tym procesie

Platforma **nie inituje fazy** i **nie robi sync**. Jej rola:

| API | Rola w init / wyrównaniu |
|-----|--------------------------|
| `otPlatRadioGetNow()` | SubMac bierze stąd początkowy `mCslSampleTime` |
| `otPlatAlarmMicroGetNow()` | `getCslPhase()` liczy fazę do CSL IE |
| `otPlatRadioUpdateCslSampleTime()` | Przechowuje `sCslSampleTime` — spójność z SubMac |
| `otPlatRadioReceiveAt()` | Otwiera okno DRX w czasie podanym przez SubMac |
| `GetRxTimestamp()` | Daje timestamp RX w **tej samej skali** co `GetNow()` |

**Warunek konieczny:** wszystkie te API muszą używać **jednego RTC** (na 52840: RTC2 w `alarm.c`).
Jeśli `GetNow()`, `ReceiveAt` i timestamp RX są w różnych skalach — self-consistency init
się rozpadnie i `sync_from_rx` nigdy nie pomoże.

---

## 10. Kiedy init od `GetNow()` wystarcza (52840 OK)

Wszystkie poniższe muszą być spełnione:

1. **Self-consistency** — okna i raportowana faza z tego samego `GetNow()` / `sCslSampleTime`
2. **Parent nadaje wg CSL IE childa** — parent ma włączony CSL transmitter i zna period+phase
3. **Okno wystarczająco szerokie** — uncertainty + ramp-up łapią pierwszy TX
4. **Spójny czas platformy** — `GetNow` = timestamp RX = `ReceiveAt` (jeden RTC)
5. **SecEnh-ACK od parenta** — `sync_from_rx` może doprecyzować w kolejnych cyklach

Przy spełnionych warunkach init „losowy” względem parenta **nie przeszkadza** — para
parent–child jest spójna od pierwszej wymiany CSL IE.

---

## 11. Kiedy init od `GetNow()` **nie wystarcza** — objawy i przyczyny

### Objawy (GDB / debug counters)

| Counter | Wartość | Interpretacja |
|---------|---------|---------------|
| `init_phase` | różne między uruchomieniami (408 vs 225 ms) | Normalne — init od `GetNow()` |
| `sync_from_rx` | 0 (nigdy nie rośnie) | Korekta nigdy nie zadziałała |
| `in_window` | 0 | Żaden RX parenta w zaplanowanym oknie |
| `likely_phase` | timeout (100%) | Okna lecą, parent nie trafia (zła faza / zły czas) |

### Przyczyny, gdy self-consistency się sypie

| Przyczyna | Skutek |
|-----------|--------|
| `GetNow()` ≠ skala timestamp RX | `sync_from_rx` ustawia zły `mCslLastSync` lub w ogóle nie ma sensu |
| `ReceiveAt` na innym zegarze niż `GetNow()` | Okna w złym miejscu mimo poprawnej fazy w CSL IE |
| Brak SecEnh-ACK od parenta | `UpdateCslLastSyncTimestamp(Rx)` nigdy nie aktualizuje sync |
| `sCslSampleTime == 0` w `getCslPhase()` | Faza raportowana parentowi = 0 / błędna |
| Parent nie używa CSL IE childa | Parent nadaje poza oknem childa |

**Kluczowy wniosek:** jeśli `sync_from_rx == 0` **i** `in_window == 0` w nieskończoność,
problem **nie leży w „losowym init”** — leży w tym, że pętla self-consistency + korekta
**nigdy nie zamknęła się**. Init od `GetNow()` sam tego nie naprawi.

---

## 12. Podsumowanie — jak system sobie radzi z init od `GetNow()`

```
┌─────────────────────────────────────────────────────────────────┐
│  INIT: mCslSampleTime = mCslLastSync = GetNow()                 │
│  Faza „losowa” względem parenta, spójna wewnętrznie            │
└──────────────────────────┬──────────────────────────────────────┘
                           │
         ┌─────────────────┼─────────────────┐
         ▼                 ▼                 ▼
  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
  │ Mechanizm 1  │  │ Mechanizm 2  │  │ Mechanizm 3      │
  │ Parent TX wg │  │ Szerokie     │  │ sync_from_rx     │
  │ fazy childa  │  │ okna RX      │  │ (SecEnh-ACK)     │
  │ (CSL IE)     │  │ (uncertainty │  │ timestamp ramki  │
  │              │  │  + ramp-up)  │  │ → mCslLastSync   │
  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘
         │                 │                    │
         └─────────────────┼────────────────────┘
                           ▼
              ┌────────────────────────┐
              │ Stabilna synchronizacja│
              │ fazy CSL w kolejnych   │
              │ okresach               │
              └────────────────────────┘
```

| Pytanie | Odpowiedź |
|---------|-----------|
| Skąd init fazy? | `GetNow()` w `SetCslParams()` — moment włączenia CSL |
| Czy to losowe? | Względem parenta — tak. W sensie RNG — nie |
| Czy parent podaje fazę? | Nie przy attach |
| Jak parent trafia do childa? | Parent nadaje wg fazy z CSL IE zgłoszonej przez childa |
| Co jeśli pierwsze okno missuje? | Timeout → sleep → następne okno; cykl trwa |
| Co doprecyzowuje timing? | `sync_from_rx`: timestamp ramki → `mCslLastSync` → przeliczenie okien |
| Co musi zrobić platforma? | Jeden spójny zegar dla GetNow, ReceiveAt i timestamp RX |

---

## Pliki referencyjne

| Plik | Zawartość |
|------|-----------|
| OpenThread `sub_mac_csl_receiver.cpp` | `SetCslParams`, `GetCslWindowEdges`, `UpdateCslLastSyncTimestamp`, `RestartCslTimerAfterSyncUpdate` |
| `src/src/radio.c` | `getCslPhase`, `otPlatRadioUpdateCslSampleTime`, `GetRxTimestamp`, `otPlatRadioReceiveAt` |
| `src/src/alarm.c` | `otPlatTimeGet`, `otPlatAlarmMicroGetNow` — RTC2 |
| `src/nrf52840/openthread-core-nrf52840-config.h` | `CSL_RECEIVE_TIME_AHEAD`, `MIN_RECEIVE_ON_*` |
| `docs/CSL_PHASE_SYNC_NRF52840.md` | Szerszy kontekst — pełna architektura CSL na nRF52840 |
