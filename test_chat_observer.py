"""Queue, escaping and pagination checks with a Lua runtime (requires lupa)."""
import json
from pathlib import Path
import unittest
from lupa import LuaRuntime


class ChatTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute('''
        table.getn=function(t) return #t end
        function GetTime() return 12.5 end
        function CreateFrame()
          return {SetScript=function(s,k,v) s[k]=v end,RegisterEvent=function() end}
        end
        ''')
        self.source = (Path(__file__).parent/'TravelerWoW112Dll'/'chat_observer.h').read_text().split('R"lua(',1)[1].split(')lua"',1)[0]
        self.lua.execute(self.source)
        self.g = self.lua.globals()

    def event(self, text='hello', event='CHAT_MSG_WHISPER'):
        self.g.event = event
        self.g.arg1 = text
        self.g.arg2 = '玩家'
        self.g.arg9 = '综合'
        self.g.arg8 = 1
        self.g.Traveler112ChatFrame.OnEvent()

    def poll(self, after=0):
        self.g.Traveler112ChatPoll(after)
        return json.loads(self.g.Traveler112ChatResult)

    def test_json_escaping_and_chinese(self):
        text = '中文 "quote" \\ newline\n tab\t nul\0 |Hitem:1|h[物品]|h'
        self.event(text)
        message = self.poll()['messages'][0]
        self.assertEqual(message['text'], text)
        self.assertEqual(message['type'], 'WHISPER')
        self.assertEqual(message['sender'], '玩家')
        self.assertEqual(message['channel'], '综合')

    def test_pagination_and_independent_readers(self):
        for i in range(25):
            self.event(str(i))
        first = self.poll()
        self.assertEqual(len(first['messages']), 20)
        self.assertEqual(first['next_seq'], 20)
        self.assertEqual(self.poll(), first)
        second = self.poll(20)
        self.assertEqual(len(second['messages']), 5)
        self.assertEqual(self.poll(25)['messages'], [])

    def test_overflow_reports_loss(self):
        for i in range(205):
            self.event(str(i))
        page = self.poll()
        self.assertEqual(page['dropped'], 5)
        self.assertEqual(page['messages'][0]['seq'], 6)

    def test_world_reset_and_reinitialization(self):
        self.event()
        self.event(event='PLAYER_ENTERING_WORLD')
        self.assertEqual(self.poll()['messages'], [])
        self.event()
        self.lua.execute(self.source)
        self.assertEqual(self.poll()['messages'][0]['seq'], 2)
        self.assertTrue(self.poll(100)['reset'])
        self.assertEqual(self.poll(100)['next_seq'], 2)

    def test_utf8_truncation_and_response_budget(self):
        for _ in range(40):
            self.event('中'*1000)
        page = self.poll()
        self.assertLessEqual(len(self.g.Traveler112ChatResult.encode()), 50000)
        self.assertEqual(page['messages'][0]['text'], '中'*682)


if __name__ == '__main__':
    unittest.main()
