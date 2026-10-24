"""Read chat only: python watch_chat.py PID. Ctrl+C stops the reader."""
import argparse
import time
from heal_pipe_client import chat_poll


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    args = parser.parse_args()
    cursor = 0
    session = None
    try:
        while True:
            page = chat_poll(args.pid, cursor)
            if page['reset'] or (session is not None and page['session'] != session):
                print('[聊天会话变化]')
            session = page['session']
            if page['dropped']:
                print(f"[队列已覆盖/清理 {page['dropped']} 条消息]")
            for message in page['messages']:
                print(f"[{message['type']}] {message['sender']}: {message['text']}")
            cursor = page['next_seq']
            if cursor >= page['latest_seq']:
                time.sleep(0.5)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
