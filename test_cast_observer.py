"""Developer regression tests: pip install lupa; python -m unittest test_cast_observer."""
from pathlib import Path
import unittest
try:
    from lupa import LuaRuntime
except ImportError:
    LuaRuntime = None


@unittest.skipIf(LuaRuntime is None, 'Developer test requires lupa')
class ObserverTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
            clock=100
            function GetTime() return clock end
            function CreateFrame()
                return {SetScript=function(self,k,v) self[k]=v end,
                        RegisterEvent=function() end}
            end
            SPELL_FAILED_OUT_OF_RANGE='range'
            SPELL_FAILED_NO_POWER='mana'
            SPELL_FAILED_NOT_STANDING='stand'
        ''')
        source = (Path(__file__).parent / 'TravelerWoW112Dll' / 'healing_observer.h').read_text(encoding='utf-8')
        self.lua.execute(source.split('R"lua(', 1)[1].split(')lua"', 1)[0])
        self.g = self.lua.globals()

    def event(self, event, arg1=None, arg2=None):
        self.g.event, self.g.arg1, self.g.arg2 = event, arg1, arg2
        self.g.Traveler112CastObserver.OnEvent()

    def tick(self, seconds):
        self.g.clock += seconds
        self.g.Traveler112CastObserver.OnUpdate()

    def result(self):
        return self.g.Traveler112CastResult.split('\x1f')

    def test_complete_preserves_base_name_and_compacts_rank(self):
        self.g.Traveler112BeginCast('Healing Touch(Rank 4)')
        self.event('SPELLCAST_START', 'Healing Touch', 3000)
        self.assertEqual(self.g.Traveler112CastEnd, 103)
        self.event('SPELLCAST_DELAYED', 500)
        self.assertEqual(self.g.Traveler112CastEnd, 103.5)
        self.tick(3.5)
        self.event('SPELLCAST_STOP')
        self.assertEqual(self.g.Traveler112CastResult, '')
        self.tick(.11)
        self.assertEqual(self.result(), ['1', 'Healing Touch(Rank 4)', '1', '', '1'])

    def test_failure_after_stop_wins(self):
        self.g.Traveler112BeginCast('治疗术(等级 3)')
        self.event('SPELLCAST_STOP')
        self.event('SPELLCAST_FAILED')
        self.event('UI_ERROR_MESSAGE', 'range')
        self.tick(.2)
        self.assertEqual(self.result(), ['1', '治疗术(等级 3)', '0', 'out_of_range', '1'])

    def test_cancel_and_channel(self):
        self.g.Traveler112BeginCast('治疗术')
        self.event('SPELLCAST_START', '治疗术', 3000)
        self.g.Traveler112CancelCast()
        self.event('SPELLCAST_STOP')
        self.assertEqual(self.result()[2:4], ['0', 'interrupted'])
        self.g.Traveler112BeginCast('宁静')
        self.event('SPELLCAST_CHANNEL_START', 5000, '宁静')
        self.event('SPELLCAST_CHANNEL_UPDATE', 2000)
        self.assertEqual(self.g.Traveler112CastEnd, 102)
        self.event('SPELLCAST_CHANNEL_STOP')
        self.assertEqual(self.result()[2:4], ['0', 'interrupted'])

    def test_manual_cast_is_observed_without_result(self):
        self.event('SPELLCAST_START', '治疗术', 3000)
        self.assertEqual(self.g.Traveler112CastName, '治疗术')
        self.event('SPELLCAST_STOP')
        self.tick(.2)
        self.assertEqual(self.g.Traveler112CastResult, '')

    def test_missing_events_are_unconfirmed_and_world_reset_clears(self):
        self.g.Traveler112BeginCast('治疗术')
        self.tick(2.1)
        self.assertEqual(self.result()[2:], ['0', 'unknown', '0'])
        self.event('PLAYER_ENTERING_WORLD')
        self.assertEqual(self.g.Traveler112CastResult, '')
        self.g.Traveler112BeginCast('治疗术')
        self.event('UI_ERROR_MESSAGE', 'mana')
        self.assertEqual(self.result()[0], '2')
        self.assertEqual(self.result()[3], 'not_enough_mana')

    def test_stand_and_invalid_target(self):
        self.g.Traveler112BeginCast('治疗术')
        self.event('UI_ERROR_MESSAGE', 'stand')
        self.assertEqual(self.result()[3], 'must_stand')
        self.g.Traveler112BeginCast('治疗术')
        self.g.Traveler112RejectCast('invalid_target')
        self.assertEqual(self.result()[3], 'invalid_target')


if __name__ == '__main__':
    unittest.main()
