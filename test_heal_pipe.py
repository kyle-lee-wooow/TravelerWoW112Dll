"""Interactive live test. Default actions only read state; casts require menu selection."""
import argparse
import datetime
import json
import math
import time
from pathlib import Path
from heal_pipe_client import healstate, spells, cast, stopcast, lua


def validate(state):
    errors = []
    if state.get('scope') not in ('solo', 'group', 'raid'):
        errors.append('scope invalid')
    if not isinstance(state.get('roster_verified'), bool):
        errors.append('roster_verified missing')
    player = state.get('player', {})
    for key in ['cast_remaining_ms', 'gcd_remaining_ms']:
        if key in player and (type(player[key]) is not int or player[key] < 0):
            errors.append(f'{key} invalid')
    if 'casting' in player and type(player['casting']) is not bool:
        errors.append('casting invalid')
    result = state.get('last_cast')
    if result is not None:
        if (not isinstance(result, dict) or type(result.get('seq')) is not int
                or type(result.get('success')) is not bool):
            errors.append('last_cast invalid')
    members = state.get('raid', [])
    if not isinstance(members, list):
        return errors + ['raid is not an array']
    for unit in [player] + members:
        if unit is not player and not unit.get('available'):
            continue
        for current, maximum in [('hp', 'max_hp'), ('mana', 'max_mana')]:
            a, b = unit.get(current), unit.get(maximum)
            if type(a) is not int or type(b) is not int or not 0 <= a <= b:
                errors.append(f"{unit.get('name')}: {current}/{maximum} invalid")
        if unit is not player:
            distance = unit.get('distance')
            if not isinstance(distance, (int, float)) or not math.isfinite(distance) or distance < 0:
                errors.append(f"{unit.get('name')}: distance invalid")
            if type(unit.get('dead')) is not bool:
                errors.append(f"{unit.get('name')}: dead missing")
    return errors


def snapshot(pid):
    start = time.perf_counter()
    state = healstate(pid)
    elapsed = (time.perf_counter() - start) * 1000
    errors = validate(state)
    print(json.dumps(state, ensure_ascii=False, indent=2))
    print(f'耗时 {elapsed:.1f} ms；结构检查：' + ('；'.join(errors) if errors else '通过'))
    missing = [key for key in ['moving', 'drinking', 'sitting', 'buffs', 'debuffs']
               if key not in state.get('player', {})]
    if 'in_combat' not in state:
        missing.append('in_combat')
    if 'spells_count' not in state:
        missing.append('spells_count')
    missing.extend(key for key in ['casting', 'cast_spell', 'cast_remaining_ms', 'gcd_remaining_ms']
                   if key not in state.get('player', {}))
    if missing:
        print('未读取到的可选字段：' + ', '.join(missing))
    return state, elapsed, errors


def poll(pid, seconds, interval):
    path = Path(__file__).parent / ('heal_test_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    deadline = time.monotonic() + seconds
    success = failures = 0
    timings = []
    spell_identity = None
    last_result = None
    print('轮询期间可移动、喝水、进出战斗、加 buff、组队，对照输出。Ctrl+C 停止轮询。')
    with path.open('w', encoding='utf-8') as out:
        try:
            while time.monotonic() < deadline:
                start = time.perf_counter()
                entry = {'time': datetime.datetime.now().isoformat(), 'pid': pid}
                try:
                    state = healstate(pid)
                    ms = (time.perf_counter() - start) * 1000
                    entry.update(state=state, elapsed_ms=ms, validation=validate(state))
                    identity = (state['player'].get('name'), state['player'].get('class'), state.get('spells_count'))
                    if identity != spell_identity:
                        try:
                            names = spells(pid)
                            entry['spells'] = names
                            print(f'技能接口返回 {len(names)} 条，状态计数 {state.get("spells_count")}')
                            if state.get('spells_count') == len(names):
                                spell_identity = identity
                            else:
                                entry['spells_warning'] = 'Count mismatch; retry next poll'
                        except (OSError, RuntimeError, ValueError) as exc:
                            entry['spells_error'] = str(exc)
                            print('技能读取失败：', exc)
                    success += 1
                    timings.append(ms)
                    p = state['player']
                    cast_result = state.get('last_cast')
                    if cast_result and cast_result != last_result:
                        print('施法结果：', json.dumps(cast_result, ensure_ascii=False))
                        last_result = cast_result.copy()
                    print(f"{p.get('name')} HP={p.get('hp')}/{p.get('max_hp')} MP={p.get('mana')}/{p.get('max_mana')} "
                          f"scope={state.get('scope')} combat={state.get('in_combat', '?')} "
                          f"moving={p.get('moving', '?')} drinking={p.get('drinking', '?')} "
                          f"sitting={p.get('sitting', '?')} "
                          f"casting={p.get('casting', '?')} spell={p.get('cast_spell', '?')} "
                          f"remaining={p.get('cast_remaining_ms', '?')}ms gcd={p.get('gcd_remaining_ms', '?')}ms "
                          f"buffs={p.get('buffs', '?')} debuffs={p.get('debuffs', '?')} {ms:.1f}ms")
                except (OSError, RuntimeError, ValueError, KeyError, TypeError) as exc:
                    failures += 1
                    entry['error'] = str(exc)
                    print('读取失败：', exc)
                out.write(json.dumps(entry, ensure_ascii=False) + '\n')
                out.flush()
                time.sleep(max(0, interval - (time.perf_counter() - start)))
        except KeyboardInterrupt:
            pass
    print(f'成功 {success}，失败 {failures}；日志：{path}')
    if timings:
        print(f'平均 {sum(timings)/len(timings):.1f} ms，最大 {max(timings):.1f} ms')


def main():
    parser = argparse.ArgumentParser(description='DLL 治疗管道测试（默认只读取）')
    parser.add_argument('pid', nargs='?', type=int)
    parser.add_argument('--poll', type=float, help='持续读取秒数，完成后退出')
    parser.add_argument('--interval', type=float, default=0.2)
    args = parser.parse_args()
    pid = args.pid or int(input('输入已注入 DLL 的 WoW PID：').strip())
    if pid <= 0 or args.interval < 0.1 or (args.poll is not None and args.poll <= 0):
        parser.error('PID/秒数必须大于零，间隔至少 0.1 秒')
    if args.poll is not None:
        poll(pid, args.poll, args.interval)
        return
    print('先替换新 DLL，重启游戏，再通过注入器注入。ok 只代表命令执行，不保证游戏施法成功。')
    while True:
        choice = input('\n1 状态  2 轮询60秒  3 手动施法  4 停止施法  5 已学法术  6 Lua  7 测试停跟随none  8 移动打断跟随  0 退出：').strip()
        try:
            if choice == '0':
                return
            if choice == '1':
                snapshot(pid)
            elif choice == '2':
                poll(pid, 60, args.interval)
            elif choice == '3':
                spell = input('法术名（含等级）：').strip()
                target = input('目标人物名字：').strip()
                print(cast(pid, spell, target))
            elif choice == '4':
                print(stopcast(pid))
            elif choice == '5':
                names = spells(pid)
                print('\n'.join(names))
                print(f'共 {len(names)} 条技能')
            elif choice == '6':
                print(lua(pid, input('输入一行 Lua：')))
            elif choice == '7':
                print(lua(pid, 'FollowUnit("none")'))
                print('ok 不代表跟随已停止，请观察角色。此客户端静态分析显示无效目标不会主动停止。')
            elif choice == '8':
                try:
                    print(lua(pid, 'MoveForwardStart()'))
                    time.sleep(0.1)
                finally:
                    print(lua(pid, 'MoveForwardStop()'))
        except (OSError, RuntimeError, ValueError, KeyError, TypeError) as exc:
            print('测试失败：', exc)


if __name__ == '__main__':
    main()
