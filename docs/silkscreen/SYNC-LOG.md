# Silkscreen sync log

One entry per sync, written by `scripts/silkscreen/sync.sh --accept`: what moved, what the census listed, what the builds took.
The procedure is [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md); older, hand-written records are in [PORTING.md](PORTING.md#upstream-merges).

## 2026-10-20  SDK +9 (e67e889), CrossPoint +10 (upstream/develop 51089beb)

- Merged: SDK +9 (e67e889), CrossPoint +10 (upstream/develop 51089beb). Total 2m25s; census items: 4.
- Reviewed: Preserved Silkscreen touch-driver selection and long-press key paths; BLE discovery remains in its existing connection flow and clipping deletion stays key-accessible. All three CrossPoint images passed and were frozen (12m08s, 16m52s, 18m38s); this 2m25s record-only validation recovers a runner interruption after freezing, with two documented Windows host-test skips.
