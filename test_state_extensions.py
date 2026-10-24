"""Read-only live checks: python test_state_extensions.py PID --seconds 30."""
import argparse
from datetime import datetime
import json
import time

from heal_pipe_client import healstate


def validate(state):
    controls = ('in_combat', 'silenced', 'stunned', 'fleeing', 'confused',
                'movement_disabled', 'crowd_controlled', 'los_estimated')
    for unit in [state['player'], *state['raid'], *state.get('pets', [])]:
        for key in controls:
            assert key in unit, f"{unit.get('name')}: missing {key}"
            assert unit[key] is None or type(unit[key]) is bool, key
    for key in ('map_id', 'movement_flags', 'ctm_action', 'ctm_active',
                'ctm_destination', 'current_loot_guid'):
        assert key in state['player'], f"missing player.{key}"
    boss = state.get('boss')
    if boss:
        for key in ('npc_id', 'target_guid', 'cast_spell_id', 'channel_spell_id'):
            assert key in boss, f"missing boss.{key}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--seconds', type=float, default=30)
    args = parser.parse_args()
    path = 'state_extensions_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl'
    deadline = time.monotonic() + args.seconds
    count = errors = 0
    with open(path, 'w', encoding='utf-8') as log:
        while time.monotonic() < deadline:
            started = time.monotonic()
            row = {'time': datetime.now().isoformat(), 'pid': args.pid}
            try:
                row['state'] = healstate(args.pid)
                validate(row['state'])
                count += 1
            except Exception as exc:
                row['error'] = str(exc)
                errors += 1
                print('ERROR:', exc)
            row['latency_ms'] = round((time.monotonic() - started) * 1000, 2)
            log.write(json.dumps(row, ensure_ascii=False) + '\n')
            log.flush()
            time.sleep(max(0, 0.1 - (time.monotonic() - started)))
    print(f'{path}: valid={count}, errors={errors}; field checks do not verify in-game accuracy')


if __name__ == '__main__':
    main()
