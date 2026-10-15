"""Live cast observer test. Requires heal_pipe_client.py beside this file."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time

from heal_pipe_client import cast, healstate, stopcast


def main():
    parser = argparse.ArgumentParser(description="Cast once and poll healstate every 100ms")
    parser.add_argument("pid", type=int)
    parser.add_argument("target", help="Target character name")
    parser.add_argument("--spell", default="快速治疗(等级 2)")
    parser.add_argument("--duration", type=float, default=8)
    parser.add_argument("--stop-after", type=float, help="Send stopcast after this many seconds")
    parser.add_argument("--log-dir", default=".")
    args = parser.parse_args()
    if args.duration <= 0 or (args.stop_after is not None and args.stop_after < 0):
        parser.error("duration must be positive; stop-after must be nonnegative")
    directory = Path(args.log_dir)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / ("cast_test_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f") + ".jsonl")
    print("日志:", path.resolve())
    with path.open("w", encoding="utf-8") as output:
        start = time.perf_counter()

        def log(kind, **data):
            row = dict(time=datetime.now().isoformat(), elapsed_ms=round((time.perf_counter()-start)*1000),
                       pid=args.pid, kind=kind, **data)
            output.write(json.dumps(row, ensure_ascii=False) + "\n")
            output.flush()

        try:
            baseline = healstate(args.pid)
            old_seq = baseline.get("last_cast", {}).get("seq")
            log("baseline", state=baseline)
            start = time.perf_counter()
            reply = cast(args.pid, args.spell, args.target)
            log("cast", spell=args.spell, target=args.target, response=reply)
            print("cast:", reply, "（请按场景手动移动，或等待结果）")
            next_poll = start
            stopped = False
            saw_cast = False
            missing_remaining = 0
            new_results = {}
            while time.perf_counter() - start < args.duration:
                now = time.perf_counter()
                if args.stop_after is not None and not stopped and now-start >= args.stop_after:
                    stopped = True
                    reply = stopcast(args.pid)
                    log("stopcast", response=reply)
                    print("stopcast:", reply)
                state = healstate(args.pid)
                player = state.get("player", {})
                active = player.get("casting")
                remaining = player.get("cast_remaining_ms")
                if active:
                    saw_cast = True
                    if remaining is None:
                        missing_remaining += 1
                result = state.get("last_cast")
                fresh = isinstance(result, dict) and result.get("seq") is not None and result.get("seq") != old_seq
                if fresh:
                    new_results[result["seq"]] = result
                log("state", state=state, new_result=fresh)
                print(f"{time.perf_counter()-start:5.2f}s casting={active} remaining_ms={remaining} "
                      f"gcd_ms={player.get('gcd_remaining_ms')} spell={player.get('cast_spell')!r}")
                if fresh and result != getattr(main, "printed_result", None):
                    print("新施法结果:", json.dumps(result, ensure_ascii=False))
                    main.printed_result = result
                next_poll += 0.1
                if next_poll < time.perf_counter():
                    next_poll = time.perf_counter()
                time.sleep(max(0, next_poll-time.perf_counter()))
            summary = dict(saw_casting=saw_cast, casting_samples_missing_remaining=missing_remaining,
                           new_results=list(new_results.values()))
            log("summary", **summary)
            print("汇总:", json.dumps(summary, ensure_ascii=False))
            if not new_results:
                print("未观察到新 last_cast；旧结果不计为本次结果。")
        except (OSError, RuntimeError, ValueError) as exc:
            log("error", message=str(exc))
            raise SystemExit(str(exc))
        except KeyboardInterrupt:
            log("interrupted", message="Polling stopped; no automatic stopcast sent")
            print("已停止采样；未自动发送 stopcast。")


if __name__ == "__main__":
    main()
