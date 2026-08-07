# Binarki Nordic SL (libnrf-802154-sl.a)

**Nie commitujemy** `.a` do gita (patrz `.gitignore`). Binarki są w kopii referencyjnej drivera:

```
third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/
└── nrf54l15_cpuapp/
    ├── hard-float/libnrf-802154-sl.a    ← użyj tej (toolchain ot-nrf54xx)
    ├── soft-float/libnrf-802154-sl.a
    └── softfp-float/libnrf-802154-sl.a
```

Aktywny driver (`nrf_802154/`, bez `_nowy`) ma **tylko źródła** — bez katalogu `sl/sl/lib/`.

## Spike wariantu C

```bash
# Analiza zależności (bez buildu)
./script/analyze-sl-binary.sh \
  third_party/nrf54/nordic/drivers/nrf_802154_nowy/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a

# Build + gate G1 (link)
./script/spike-c-sl-binary.sh
```

Skrypt kopiuje binarkę do `third_party/nrf54/bin/libnrf-802154-sl.a` na czas buildu.

## Wersjonowanie

Driver źródłowy + binarka SL muszą być z **tego samego tagu** nrfxlib/NCS.
Kopia `_nowy` = pełny pakiet z GitHub [sdk-nrfxlib/nrf_802154](https://github.com/nrfconnect/sdk-nrfxlib/tree/main/nrf_802154).

## MPSL (wariant A)

`libmpsl.a` nie leży w `_nowy` — bierzesz z NCS:

```
~/NCS_workspace/nrfxlib/mpsl/lib/nrf54l/soft-float/libmpsl.a
```

(dla wariantu C **nie linkujesz** MPSL — tylko SL + własne stuby).
