# Ground speed hold

`speed <1-100>` writes and remembers the requested ground speed for this injected
DLL session. The existing game main-thread maintenance tick checks the current
player speed and reapplies the value when overwritten, including by slows.
Player pointers are resolved anew on each check; the request survives temporary
player unavailability during instance transitions. Restoration occurs on the
next maintenance tick after a valid player is available, not atomically with a
server speed update.

`speed reset` disables maintenance even during a loading screen and attempts the
existing default-speed reset to 7. It does not continuously enforce 7. The next
normal game speed update may therefore change it. Reinjecting/restarting clears
the remembered request. Only ground speed is affected; movement prohibition
flags (root/stun), swimming and flight are not changed.

Release build verified; live instance-transition and slow-debuff tests remain
required after restarting/reinjecting. Check a selected speed, apply a slow,
change instances, then reset and confirm subsequent slows remain effective.
