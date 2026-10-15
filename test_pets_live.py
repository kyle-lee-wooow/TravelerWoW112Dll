"""Read-only pet telemetry acceptance capture."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time
from heal_pipe_client import healstate


def main():
    p = argparse.ArgumentParser()
    p.add_argument('pid', type=int)
    p.add_argument('--duration', type=float, default=60)
    p.add_argument('--log-dir', default='.')
    a = p.parse_args()
    if a.duration <= 0:
        p.error('duration must be positive')
    directory = Path(a.log_dir)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / ('pets_test_' + datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.jsonl')
    print(path.resolve())
    previous = None
    end = time.monotonic() + a.duration
    with path.open('w', encoding='utf-8') as f:
        while time.monotonic() < end:
            state = healstate(a.pid)
            pets = state.get('pets')
            errors = []
            if not isinstance(pets, list):
                errors.append('pets is missing or not an array')
            else:
                seen = set()
                for pet in pets:
                    for field in ('guid', 'name', 'owner', 'unit', 'available', 'hp', 'max_hp', 'dead', 'distance'):
                        if field not in pet:
                            errors.append('missing ' + field)
                    guid = pet.get('guid')
                    if not isinstance(guid, str) or not guid.isdecimal() or guid == '0' or guid in seen:
                        errors.append('invalid/duplicate GUID: ' + str(guid))
                    seen.add(guid)
                    token = pet.get('unit')
                    allowed = ['pet'] + ['partypet'+str(i) for i in range(1,5)] + ['raidpet'+str(i) for i in range(1,41)]
                    if token not in allowed:
                        errors.append('invalid token: ' + str(token))
            row = dict(time=datetime.now().isoformat(), pid=a.pid, state=state, validation=errors)
            f.write(json.dumps(row, ensure_ascii=False)+'\n')
            f.flush()
            signature = [(x.get('unit'), x.get('guid'), x.get('name'), x.get('available')) for x in pets or []]
            if signature != previous or errors:
                print(json.dumps(dict(pets=pets, validation=errors), ensure_ascii=False))
                previous = signature
            time.sleep(.1)


if __name__ == '__main__':
    main()
