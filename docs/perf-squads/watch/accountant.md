# Watch accountant — `TSFixedArray::ReallocData`

Static byte account of one grow. Campaign 0 has no frame log yet, so per-frame bytes and allocs stay blank until that line ranks this path.

## Flag

`ReallocData` always passes `SMEM_FLAG_NOREALLOC` (`Engine/Source/Storm/H/stpl.h:757`). The flag is `0x10` (`Engine/Source/Storm/H/storm.h:58`).

`SMemReAlloc` returns nullptr when the old pointer is live and the flag is set (`Engine/Source/Storm/Source/SMem.cpp:85-86`). `ReallocData` then takes the fallback:

| Step | What moves | Site |
| --- | --- | --- |
| Allocate | new block of `sizeof(T) * count` | `Engine/Source/Storm/H/stpl.h:760` |
| Copy-construct | each kept element, `new (&dst) T(src)` | `Engine/Source/Storm/H/stpl.h:766` |
| Destroy | the source element just copied | `Engine/Source/Storm/H/stpl.h:767` |
| Free | the old block | `Engine/Source/Storm/H/stpl.h:770` |

A null old pointer allocates and returns before the flag check (`Engine/Source/Storm/Source/SMem.cpp:79-80`). The first block is one `SMemAlloc` and zero copy-constructs.

This is original Storm behavior. The same function is every `TSFixedArray` and `TSGrowableArray` in the client.

## Bytes per live grow

`count` is the capacity argument. `oldAlloc` is `m_alloc` before the call. `oldCount` is `m_count` before the call. `SMemAlloc` and `SMemReAlloc` charge `(bytes + 7) & ~7` (`Engine/Source/Storm/Source/SMem.cpp:42`, `Engine/Source/Storm/Source/SMem.cpp:89`).

`copycount = count < oldCount ? count : oldCount` (`Engine/Source/Storm/H/stpl.h:763`). A grow copies `oldCount` elements. A shrink copies `count` elements. The dropped tail is destroyed first (`Engine/Source/Storm/H/stpl.h:752-754`) and is absent from `copycount`.

| Term | Formula | Site |
| --- | --- | --- |
| malloc | `(sizeof(T) * count + 7) & ~7` | `stpl.h:760` |
| free | `(sizeof(T) * oldAlloc + 7) & ~7` | `stpl.h:770` |
| copy-construct | `sizeof(T) * copycount` | `stpl.h:763-767` |
| peak | malloc + free | both blocks live until `stpl.h:770` |

**One live grow: 1 malloc, 1 free, and `sizeof(T) * copycount` bytes through `T`'s copy constructor.**

The old block cannot extend in place. `SMemReAlloc` returns before `realloc` (`Engine/Source/Storm/Source/SMem.cpp:85-86`). Peak container bytes are the old charged size plus the new charged size. If `T`'s copy constructor allocates, those bytes sit on top of `sizeof(T) * copycount` for the same window.

The tail past `copycount` is raw storage. Callers construct it after `ReallocData` returns (`TSFixedArray::SetCount` at `Engine/Source/Storm/H/stpl.h:796-798`). That construction is outside the copy term.

## How often `count` changes

`TSFixedArray::SetCount` calls `ReallocData(count)` on every non-zero size change (`Engine/Source/Storm/H/stpl.h:794`). Capacity becomes `count`. Each one-element step on a live array copies the whole prefix. `SetCount(0)` goes through `Clear` (`Engine/Source/Storm/H/stpl.h:802`), which frees and skips this formula.

`TSGrowableArray` calls `ReallocData` when the needed count exceeds `m_alloc`:

| Caller | Site |
| --- | --- |
| `Add` | `Engine/Source/Storm/H/stpl.h:829-836` |
| `GrowToFit` | `Engine/Source/Storm/H/stpl.h:907-915` |
| `New` | `Engine/Source/Storm/H/stpl.h:932-939` |
| `Reserve` | `Engine/Source/Storm/H/stpl.h:985` |
| `SetCount` | `Engine/Source/Storm/H/stpl.h:993-1001` |

`CalcChunkSize` (`Engine/Source/Storm/H/stpl.h:883-897`) picks the step. `maxchunk` is `256 / sizeof(T)` when that is greater than 8, otherwise 8. Below `maxchunk`, the chunk is the highest power of two less than or equal to the needed count. At `maxchunk` and above, `m_chunk` stays at `maxchunk`. Callers round the capacity up to a multiple of that chunk before `ReallocData`. The malloc uses that rounded `count`. The copy uses `oldCount`.

A growable shrink destroys the tail and leaves `m_alloc` in place (`Engine/Source/Storm/H/stpl.h:1007-1010`). The next `ReallocData` is the next time `m_count` would pass `m_alloc`.

## `ReallocAndClearData`

`Set` calls `ReallocAndClearData` (`Engine/Source/Storm/H/stpl.h:779`), which passes flags `0` (`Engine/Source/Storm/H/stpl.h:741`). `SMemReAlloc` may `realloc` the same block (`Engine/Source/Storm/Source/SMem.cpp:89-90`). Every element is destroyed first (`Engine/Source/Storm/H/stpl.h:733-735`). `Set` then copy-constructs from the source pointer (`Engine/Source/Storm/H/stpl.h:781-783`). That traffic is a fill from the caller’s buffer, and it is separate from the `ReallocData` grow formula.

## Why it waits

Per-frame cost is the sum of the live grows once a log counts them:

- allocs = one malloc and one free per live grow, plus one malloc per first block
- bytes allocated = sum of `(sizeof(T) * count + 7) & ~7`
- bytes copy-constructed = sum of `sizeof(T) * copycount`
- draws = 0

Watch-list items get a squad only when Campaign 0 places them above campaigns 1–6 (`docs/perf-agent-plan.md:50`, `docs/perf-agent-plan.md:163`). This row waits on that rank (`docs/perf-agent-plan.md:169`). The flag is every container in the client, so the sum is client-wide. A trivial-type `realloc` — pass flags `0` for trivial `T`, so `SMemReAlloc` can extend or move the block and the copy constructor stays on types that own resources — is a later, separate campaign with a type-trait gate (`docs/perf-agent-plan.md:169`).
