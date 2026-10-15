"""Developer event tests; requires lupa."""
from pathlib import Path
import unittest
from lupa import LuaRuntime


class LootTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime()
        self.lua.execute('''
        slots=2 calls=0 closed=0
        function CreateFrame() return {SetScript=function(s,k,v) s[k]=v end,RegisterEvent=function() end} end
        function GetNumLootItems() return slots end
        function LootSlot(i) calls=calls+1 end
        function CloseLoot() closed=closed+1 end
        ''')
        source=(Path(__file__).parent/'TravelerWoW112Dll'/'loot_observer.h').read_text()
        self.lua.execute(source.split('R"lua(',1)[1].split(')lua"',1)[0])
        self.g=self.lua.globals()

    def event(self, name):
        self.g.event=name
        self.g.Traveler112LootFrame.OnEvent()

    def test_manual_window_untouched(self):
        self.event('LOOT_OPENED')
        self.assertEqual(self.g.calls,0)

    def test_batch_loots_once_then_waits(self):
        self.g.TravelerLoot('all')
        self.assertEqual(self.g.Traveler112LootRequest,2)
        self.g.Traveler112LootActive=1
        self.event('LOOT_OPENED')
        self.assertEqual(self.g.calls,2)
        self.assertEqual(self.g.closed,0)
        self.g.slots=0
        self.event('LOOT_SLOT_CLEARED')
        self.assertEqual(self.g.closed,1)
        self.event('LOOT_CLOSED')
        self.assertEqual(self.g.Traveler112LootState,3)

    def test_stop_and_world_reset(self):
        self.g.TravelerLoot('stop')
        self.assertEqual(self.g.Traveler112LootRequest,3)
        self.event('PLAYER_ENTERING_WORLD')
        self.assertEqual(self.g.Traveler112LootActive,0)
        self.assertEqual(self.g.Traveler112LootRequest,0)


if __name__=='__main__': unittest.main()
