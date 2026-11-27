# Visible creature snapshots

`enemies` returns schema_version=1 from P0_PROTOCOL_DRAFT.md, plus offset,
next_offset (null on last page), and total. Up to 40 rows per page, 200 per scan.
When truncated=true there were more creatures than the scan cap; consumers must
not interpret absence as despawn. Use `enemies <sample_seq> <next_offset>` for
subsequent pages. If another request refreshed the cache or the world changed,
the continuation fails explicitly; restart with enemies. Initial requests reuse
snapshots younger than 100ms. Continuations pin that existing sample without
refreshing it. All operations run on the game main thread.

The command enumerates visible creature objects, including possibly friendly or
neutral ones. reaction/hostile/tapped/tapped_by_player are null pending live
client validation; do not assume these are enemies or untapped. Names use the
same creature-name pointer chain as the existing boss scanner; unknown is null.
Health and target GUID use the established +0x110 attribute table. Distances
include both raw center separation and the current fitted distance.

The local executable establishes enumeration as fastcall (ECX callback, EDX
filter), guarded by an instruction signature and SEH. An unavailable player,
map or failed enumeration produces available=false and no rows. Character,
object manager or map changes invalidate the cache. healstate members gain z.

Server output buffer hint and bundled client receive buffer are 256KiB. A page
of 40 bounded names/rows fits below 128KiB, independently of the scan count.
WriteFile sends one complete message and checks its byte count; client errors
are surfaced rather than decoding an incomplete message as successful JSON.
Command input remains limited to 1023 UTF-8 bytes. Runtime/game validation still
required for field values, zoning, and pagination under concurrent consumers.
