"""Windows pipe client: python heal_pipe_client.py PID [command]."""
import ctypes
from ctypes import wintypes
import json
import sys
import math

_call = ctypes.WinDLL("kernel32", use_last_error=True).CallNamedPipeW
_call.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD,
                  ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                  wintypes.DWORD]
_call.restype = wintypes.BOOL


def command(pid: int, text: str, timeout_ms: int = 2000) -> str:
    """One connection per command; returned ok means dispatch, not spell success."""
    if pid <= 0 or not text or len(text.encode("utf-8")) > 1023:
        raise ValueError("Invalid PID or command length")
    data = text.encode("utf-8")
    request = ctypes.create_string_buffer(data)
    response = ctypes.create_string_buffer(65536)
    received = wintypes.DWORD()
    pipe = rf"\\.\pipe\TravelerRouteControl_PID_{pid}"
    if not _call(pipe, request, len(data), response, len(response),
                 ctypes.byref(received), timeout_ms):
        raise ctypes.WinError(ctypes.get_last_error())
    result = response.raw[:received.value].decode("utf-8")
    if result.startswith("error:"):
        raise RuntimeError(result)
    return result


def healstate(pid: int) -> dict:
    return json.loads(command(pid, "healstate"))


def spells(pid: int) -> list[str]:
    result = json.loads(command(pid, "spells"))
    names = result.get("spells")
    if not isinstance(names, list) or not all(isinstance(name, str) for name in names):
        raise ValueError("Invalid spells response")
    return names


def cast(pid: int, spell: str, target_name: str) -> str:
    if not target_name or any(c.isspace() for c in target_name):
        raise ValueError("Target must be a single character-name token")
    return command(pid, f"cast {spell} {target_name}")


def stopcast(pid: int) -> str:
    return command(pid, "stopcast")


def moveto(pid: int, x: float, y: float) -> str:
    """One-shot world-coordinate CTM, using the player's current Z."""
    if not math.isfinite(x) or not math.isfinite(y):
        raise ValueError("Coordinates must be finite")
    return command(pid, f"moveto {x:.9g} {y:.9g}")


def stopmove(pid: int) -> str:
    return command(pid, "stopmove")


def jump(pid: int) -> str:
    """Dispatch native Jump on this client's main thread, independent of focus."""
    return command(pid, "jump")


def stopfollow(pid: int) -> str:
    """Tap native left turn on the target client's main thread."""
    return command(pid, "stopfollow")


def lootnear(pid: int, mode: str = "") -> str:
    """Interact with nearby lootable corpses; all starts a bounded batch."""
    if mode not in ("", "all", "stop", "status"):
        raise ValueError("mode must be empty, all, stop or status")
    return command(pid, "lootnear" + (" " + mode if mode else ""))


def lua(pid: int, script: str) -> str:
    """Execute one-line Lua on the game main thread; no Lua return value."""
    if not script.strip() or any(c in script for c in '\r\n\0'):
        raise ValueError('Lua must be a nonempty single line without NUL')
    return command(pid, 'lua ' + script)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python heal_pipe_client.py PID [command]")
    pid = int(sys.argv[1])
    text = " ".join(sys.argv[2:]) or "healstate"
    result = command(pid, text)
    print(json.dumps(json.loads(result), ensure_ascii=False, indent=2)
          if text in ("healstate", "spells") else result)
