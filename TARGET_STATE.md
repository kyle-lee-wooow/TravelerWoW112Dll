# Current target state and distance display

`healstate.target` is null without a resolvable current target. Otherwise:

```json
{"target":{"guid":"12345","name":"Target","distance":23.4,"hp":800000,"max_hp":1000000,"cast_spell_id":18173}}
```

GUID is a decimal string. Distance starts with three-dimensional coordinate
separation d, then applies the user-selected empirical fit:
`max(0, min(d, 0.911538*d + 0.584615))`, rounded to one decimal place.
This fits 10 -> 9.7 and 36 -> 33.4 yards and preserves the original value below
approximately 6.61 yards. It is not guaranteed spell reach. JSON, the label and
its color threshold all use this same corrected distance.
Unavailable distance, health or cast reads produce null, not invented zeroes.
Health uses the existing client attribute table (object +0x110, HP +0x40,
maximum HP +0x58). Cast ID uses object +0xC8C, as the existing boss output does.
Zero cast ID means the memory field is zero; this alone does not prove that an
instant or triggered spell did not occur. Target GUID is checked again after
reading to discard snapshots spanning a target switch.

The DLL main-thread maintenance tick updates an independent draggable label
below TargetFrame at most twice per second. It displays target name and distance,
green at <=40 corrected yards and red above. No target or unavailable position
hides the label. Drag with the left mouse button. Position persists for the UI
session only. Updates are sent only when the target or displayed tenth changes;
this does not depend on a Python client requesting healstate.

## Live validation still required

Restart the client and reinject the matching Release DLL. Check target switches,
clear-target hiding, dragging, fullscreen visibility, and health against the UI.
Compare distance near a spell boundary while accounting for collision radii.

The requested Burning Adrenaline (18173) and Locust Swarm (28785) boss encounters
have not been observed in this implementation session. The cast field is exposed
for validation, not certified against those samples. Sample healstate at 50-100ms
and timestamp responses with a monotonic clock; record nonzero transitions and
return to zero or a different ID, then compare against an independently observed
cast bar and the applicable BigWigs version. Account for request latency and
interrupted casts. A constant zero for a spell with no confirmed cast bar does
not invalidate the memory field. The 500ms display refresh is not a cast timer.
