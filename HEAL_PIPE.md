# External healing pipe

## Nearby corpse loot

`lootnear` attempts one nearest lootable dead creature within 4.5 yards.
`lootnear all` attempts up to 20 unique corpses, bounded to 30 seconds.
Minimum spacing is 200ms, with the next attempt only after LOOT_CLOSED.
LOOT_OPENED triggers one LootSlot pass; an open/blocked window or missing server
response stops the batch after 3 seconds rather than skipping across corpses.
`lootnear status` returns running/attempted/reason; attempted is not successful loot count.
In-game macros: `/run TravelerLoot("all")`, `/run TravelerLoot("one")`,
`/run TravelerLoot("stop")`. Put the macro on an action-bar key binding.
The function installs asynchronously after main-thread initialization (allow a second).
`lootnear stop` stops the batch. Native creature interaction uses 0x60BEA0,
autoloot=1, on the main thread with a function signature guard. Creature type=3,
descriptor hp+0x58=0, dynamic flags+0x23C bit 0 are required.
Each GUID is attempted once per batch. This does not move, interact with living
NPCs, or harvest gameobjects. ok means interaction dispatched, not bag receipt.
Bag space, loot rules and server responses may prevent pickup; live verification
is required. It does not require Interact.dll. The original packed DLL was only
unpacked to a temporary analysis copy; the game file was not modified.

## Pet telemetry

healstate always includes top-level `pets` (empty when no roster pets).
Entries: guid (decimal string), name, owner (character name), unit, available,
hp/max_hp/dead/distance, and readable buffs/debuffs. Unavailable values are null;
unreadable aura arrays are omitted. GUIDs are resolved fresh from roster tokens,
with duplicate GUIDs removed and a second identity check after name reads.
`pet`, `partypet1..4`, `raidpet1..40` are statically confirmed in this client
resolver at 0x515970. Dismissal/resummoning is not cached; unavailable remote pets
can remain listed if the client's roster still advertises their GUID.
player.subgroup is 0 outside raids, 1..8 in raids (0 if own record unavailable).

Lua scripts using Traveler112BeginCast install/check the same observer before
execution. They share sequence and last_cast with ordinary cast. Use TargetUnit
with the returned token, not TargetByName. No separate pet cast command exists.
Live acceptance still required: warlock/hunter/self pets, party/raid tokens,
same-name pets, dismissal/resummon, death, distance, aura changes and cast results.

Windows pipe: `\\.\pipe\TravelerRouteControl_PID_<PID>`.
Send one UTF-8 text command (no NUL or newline required), read one UTF-8 response,
then close the connection. Each WoW process has its own pipe. Inject the DLL first.

Commands:

- `lua <one-line script>`: unchanged Lua dispatched on the game main thread.
  Maximum request size is 1023 UTF-8 bytes; empty/multiline/NUL scripts are rejected.
  No Lua result is returned. `ok` only confirms DoString dispatch, not Lua/game success.
  Example: `lua SitOrStand()` or `lua FollowUnit("party1")`.
  Static tracing of FollowUnit at 0x489E00 in this WoW.exe shows an invalid target
  enters an error branch, not a stop-follow branch. Do not rely on FollowUnit("none").
  Use the native `stopfollow` command to stop following (live verified).
  FollowUnit requires partyN/raidN/target unit tokens; character names are unsupported.

- `healstate`: one JSON object. No calibration required for this command.
- `spells`: one JSON object `{"spells":["治疗术(等级1)", ...]}`.
- `cast <spell including rank> <character name>`: main-thread Lua dispatch;
  verifies target name and group membership before casting.
- `stopcast`: main-thread SpellStopCasting dispatch.
- `jump`: native player jump, live verified.
- `stopfollow`: native backward press/release to interrupt following, live verified.

`ok` confirms dispatch only; it does not confirm target selection or successful healing.
`cast` dispatches Lua directly without checking or caching drinking, eating,
movement or stance. These decisions belong to the caller using `healstate`.
It does not automatically stand the player up. Target name and group validation remain.
`player.sitting` reports ordinary/chair sitting from attributes+0x210.
Failures return `error: ...` and must not be parsed as JSON.

`player` includes name, hp, max_hp, mana, max_mana, power_type, class, spirit,
intellect, plus_healing and plus_healing_verified. Non-mana classes report mana=0.
plus_healing=0 is a placeholder, not a measured value; use configuration fallback.

`raid` contains actual raid members, or party members when not in a raid, excluding
the local player. Unit IDs retain roster indexes (raidN or partyN).
For unavailable objects, available=false, hp/max_hp/mana/max_mana/dead=null and
class=UNKNOWN. Never cast on an unavailable member or infer offline status merely
from invisibility. An empty name must also be skipped. Online and shield are not measured.
GUID is a decimal string to avoid JSON integer precision loss.
available currently means attributes and both positions can be read and validated;
it does not independently verify map equality, line of sight, or interactability.
Distance is geometric, not a guarantee a spell can reach the target.

`scope` distinguishes solo/group/raid. `distance` is the 3D coordinate distance,
or null for unavailable members. `moving` compares consecutive position samples;
the first sample and samples more than 2 seconds apart cannot establish movement.
`in_combat` reads attributes+0xA0 bit 19 (UnitAffectingCombat).
`buffs` and `debuffs` read 32 positive and 16 negative aura slots, including
Grace/勇士之赐 when present. `drinking` checks Drink/喝水 in the player's buffs.
The separate `spells` command enumerates the player spellbook and formats ranks
without whitespace: 治疗术(等级1). `healstate` does not include the spell list;
its top-level `spells_count` reads tab counts without resolving spell names.
Fetch `spells` once on startup, then again when the count or character changes.
Count alone cannot detect replacements that leave the total unchanged; explicitly
refresh after a talent reset if needed.
Optional fields are omitted if their memory reads fail; omission is not an empty list.
Buff/debuff name arrays are cached per GUID for 1000 ms on the game main thread.
Object replacement, character change, unavailable members and roster removal invalidate
the corresponding cache. A failed refresh omits that field instead of returning stale
names; retry occurs at the next refresh. `drinking` uses the same cached player buffs,
so aura/drinking changes can lag by up to one second (plus polling interval).
HP, mana, coordinates, combat and sitting remain freshly read on each healstate.
`cast` does not consult this cache or make drinking/stance decisions.

## Casting telemetry (chapter 10)

The player may include `casting`, `cast_spell`, `cast_remaining_ms`, and
`gcd_remaining_ms`. Command spell names are returned unchanged, including rank
spaces, in cast_spell and last_cast.spell. The separate spells list stays compact.

Client references traced in this WoW.exe:

- Unit+0xC8C active cast spell ID: setter 0x60D020, stop/failure clear 0x60D040.
- Attributes+0x228 channel spell ID.
- Cooldown list head 0xCECAF4, next+4, start+0x10, recovery category+0x28,
  GCD duration+0x2C; category 133, native traversal at 0x6E13E0.
- Client millisecond clock 0x42C010, used by GetTime and cooldown code.

Cast remaining time is a hybrid implementation: a Lua event observer records
SPELLCAST_START/DELAYED and CHANNEL_START/UPDATE times, read back through the
native Lua runtime on the main thread. It is NOT yet a direct cast-end memory
offset. When casting is emitted, remaining-time is always emitted. Missing timing
uses the last known remaining milliseconds, or zero if none; idle/character/spell
changes clear the fallback. This fallback does not assert fresh timing accuracy.
The observer installs on the first healstate/cast and also sees manual casting.

Native Lua globals are read with runtime state 0xCEEF74, pushstring 0x6F3890,
rawget 0x6F3B00,
GLOBALSINDEX=-10001 and settop 0x6F3080; every read restores the original stack.
This adds a native bridge; DoString itself still has no return value.
No binary detours or DLL function pointers are installed into the Lua observer.

Top-level `last_cast` contains seq/spell/success/reason, plus `verified` and `source`.
It appears only after a terminal result; pending attempts do not claim success.
Seq increments for every syntactically valid cast dispatched to the main thread.
STOP completion is allowed a 100 ms settlement window for accompanying failure
events. Failures/UI errors and cancellation map to the documented stable codes.
No start/result after 2 seconds, or superseding an unresolved command, produces
success=false, reason=unknown, verified=false, source=unconfirmed.
Success=true means the client's completion event, not proof of positive healing:
overheal, absorbs, resists and target effects require combat-event telemetry.
Player world transitions clear pending/result data but keep sequence monotonic
within the Lua runtime. FAILED waits 100 ms for the accompanying UI_ERROR_MESSAGE
before publishing unknown, so each result seq is published once with its reason.

These offsets/events are statically traced and require live testing on the exact
client. Optional fields are omitted when unavailable; do not interpret omission
as zero GCD or an idle player.

Live test: poll every 100 ms; manually cast a heal, complete it, cancel another,
move during a cast, cast an instant spell, test channeling, then provoke out-of-range,
insufficient-mana, invalid-target and sitting failures. Compare each result with
the game. Re-login and verify old results do not persist.

Run polling in one terminal and issue bot commands in another:

```powershell
python test_heal_pipe.py 12345 --poll 120 --interval 0.1
python heal_pipe_client.py 12345 "cast 治疗术(等级 3) 人物名字"
python heal_pipe_client.py 12345 stopcast
```

Live test tools need only Python's standard library. Developer observer regression
tests additionally use lupa (`python -m unittest test_cast_observer -v`).
Live verified: casting, GCD, completed and stopcast-interrupted last_cast,
rank spaces, member aura fields. Remaining-time fallback still needs live retest.
MP5 and plus_healing are not measured. Calibration commands save diagnostic
descriptor offsets but do not replace the verified attributes-based healstate reader.

## Interactive live test

```powershell
python test_heal_pipe.py
python test_heal_pipe.py 12345
python test_heal_pipe.py 12345 --poll 60 --interval 0.1
```

The interactive menu offers state display, 60-second polling, explicitly requested
cast/stopcast, and spellbook display. Polling saves UTF-8 JSONL beside the script,
including timestamps, PID, latency, validation results and read errors.
Compare standing/moving, drinking/stopped drinking, combat/out of combat, buffs,
debuffs and spell ranks with the game. Test solo, party and raid separately.
Replacing a DLL on disk does not update an already injected process: restart WoW
and inject the new Release DLL before testing.

Offsets are statically traced from `D:\code\csv\pihusuowow\WoW.exe`:
object+0x110 points to attributes; hp+0x40, maxhp+0x58, mana+0x44,
maxmana+0x5C, class byte+0x79, power type byte+0x7B, intellect+0x24C,
spirit+0x250. Party GUID array=0xBC6F48; raid count=0xB713E0;
raid record pointer array=0xB712A8, record GUID+0, subgroup+8.
Runtime validation against the game UI is still required. Client patches may change layout.

## Python (standard library only)

Run with the same permissions as WoW/injector:

```powershell
python heal_pipe_client.py 12345
python heal_pipe_client.py 12345 spells
python heal_pipe_client.py 12345 "cast 快速治疗 人物名字"
python heal_pipe_client.py 12345 stopcast
```

```python
from heal_pipe_client import healstate, spells, cast, stopcast
known_spells = spells(12345)
state = healstate(12345)
for member in state["raid"]:
    if member["available"] and member["name"] and not member["dead"]:
        print(member["name"], member["hp"], member["max_hp"])
```

CallNamedPipe uses a 64 KiB response buffer so a 40-member response is not truncated
at 4096 bytes. Its timeout controls pipe availability, not a strict end-to-end deadline;
use a background worker in external UIs. DLL main-thread dispatch timeout is 1 second.
Do not issue overlapping commands to the same PID; poll every 100-200 ms initially
and compare returned values with the game before enabling automated decisions.
# Jump command (chapter 11)

## Stop following

Request `stopfollow`, response `ok` / `error: ...`; Python: `stopfollow(pid)`.
Uses the native movement controller at `0x515090` to press and immediately
release backward (mask `0x20`) on the game main thread. No Lua or foreground
keyboard input is used. Caller chooses a safe time; this can interrupt casting.
`ok` confirms dispatch; follow termination has been live verified.
Test: `python heal_pipe_client.py PID stopfollow`, then move the followed character.

Request: `jump`; response: `ok` or `error: ...`. Calls the native Jump action
method at `0x0060DEA0` on the game main thread, with a client signature guard.
This bypasses the keyboard/UI gates in the outer Jump action `0x00513BD0`.
No foreground keyboard input is sent. `ok` means the handler ran, not that
the character necessarily jumped (the client can reject the action).
Python decides when it is safe to jump; the DLL does not schedule jumps or
check drinking/combat. AFK timer reset is not implemented or verified.

After restarting the client and injecting the updated DLL, test a background
client with `python heal_pipe_client.py PID jump`. The Python API is `jump(pid)`.

