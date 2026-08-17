# SSED + leader — problem z pingiem (CSL sync)

**Data:** 2026-08-17  
**Źródło:** analiza w tej sesji (pcap `crash/nrf54-leader/ssed_54.pcapng`, `crash/dziwne.pcapng` + logi UART/sniffer opisane przez użytkownika)  
**Setup:** nRF54 = SSED child, nRF52840 = leader, kanał 24

---

## Problem (1 zdanie)

**Parent i SSED child są rozjechani w synchronizacji CSL** — ping pada nie dlatego, że sieć Thread nie działa, lecz dlatego, że transmisje **parent → child** (indirect / CSL TX) nie trafiają w okno RX childa.

---

## Oczekiwane zachowanie

Przy poprawnym CSL ping **powinien działać w obie strony**, ale niesymetrycznie:

| Kierunek | Co jest trudne |
|----------|----------------|
| **Leader → child** | Cały echo request — parent musi trafić w okno CSL childa |
| **Child → leader** | Echo request w górę jest łatwiejszy; **echo reply w dół** znowu wymaga CSL/indirect |

---

## Obserwacje — ping child → leader (`ssed_54.pcapng`)

- **Child** `0x3404` (nRF54 SSED): ramki z **CSL IE**, period **3125** (~500 ms)
- **Leader** `0x3400` (nRF52840)
- **Uplink (child → leader):** ramka Data od childa + ACK od leadera (Frame Pending = 1) — wygląda OK
- **Downlink (reply):** leader retransmituje te same seq (**171**, potem **176**), **brak ACK od childa**
- Po ~33 ms child przestaje pollować; leader retry co ~500 ms
- Payload zaszyfrowany — wniosek z warstwy MAC, nie z ICMP

**Wniosek:** request w górę przechodzi, **odpowiedź w dół nie jest dostarczana/ACK-owana**.

---

## Obserwacje — ping leader → child (logi na żywo + `dziwne.pcapng`)

### Faza „cisza” (~2 min)

- **Sniffer:** prawie pusty przy pingach (poza sporadycznym MLE Advertisement od leadera)
- **Leader:** `Sent echo request (seq = 10)`, **100% packet loss**
- **Child:** tylko pętla debug:
  ```
  CSL window start 280533235, duration 12762
  CSL window start 281033215, duration 12802
  ...
  ```
  - okno co **~500 ms**
  - **duration rośnie** o ~40 µs co okno → dryf zegara względem parenta
- **Leader (epizodycznie):** `CslTxScheduler: CSL tx to 3404 failed`, `TransmitDataCsl`, **`NoAck`**, `seqnum:15`, `dst:0x3404`

### Faza „wpadnięcie w okno” (burst w snifferze)

Po **~48 s** nagły burst ruchu w `dziwne.pcapng`:

1. **MLE Child Update Request** od childa (z CSL IE, phase 1171)
2. **Child Update Response** od leadera (ACK z CSL phase 3062)
3. Child zaczyna **Data Request** (~co 100 ms)
4. Pojawiają się **ICMP ping** (seq 7, 8, 9) z **reply**
5. Po **~51 s** child przestaje pollować; leader retry seq=9 co **500 ms** bez odpowiedzi

**Wniosek:** krótkotrwała resynchronizacja CSL przez MLE Child Update — potem sync znowu się rozjeżdża.

---

## Wspólny wzorzec (oba kierunki)

```
[Desync CSL]
  child: tylko CSL window start (słucha we własnym rytmie)
  parent: TransmitDataCsl → NoAck
  sniffer: brak wymiany unicast child↔parent
  ping: 100% loss

[Child Update / chwilowy resync]
  child: Child Update + poll
  parent: trafia w okno, pingi przechodzą (seq 7/8/9)
  sniffer: nagły burst ramek

[Ponowny desync]
  child: koniec poll
  parent: retry co 500 ms (CSL period)
  ping: znowu fail
```

---

## Hipoteza główna

**Dryf / błąd synchronizacji CSL** między parentem a SSED childem — parent nie trafia w okno RX childa przy indirect transmission. Objawy:

- `NoAck` na `TransmitDataCsl`
- rosnące `CSL window duration` na childzie
- pusty sniffer (child nie nadaje; parent missuje okno)
- intermittent PASS po **MLE Child Update Request**

To **nie wygląda** na problem join Thread ani samego ICMP — to warstwa **MAC/CSL delivery parent → child**.

---

## Co jeszcze nie wiemy (poza tym capture)

- Dokładna konfiguracja CLI (`csl period`, `pollperiod`, `childtimeout`) na obu urządzeniach w tych próbach
- Wersja firmware / patch timestampera na leaderze
- Pełny log leadera z momentu burst (użytkownik nie zdążyła odczytać szczegółów)
