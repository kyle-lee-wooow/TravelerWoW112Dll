"""Exercise the actual C++-assembled query chunk against Lua 5.1 (5.0-style APIs)."""
import json
from pathlib import Path
import re
import unittest
from lupa.lua51 import LuaRuntime


class LuaQueryTests(unittest.TestCase):
    def run_query(self, expression):
        cpp = (Path(__file__).parent / 'TravelerWoW112Dll/dllmain.cpp').read_text(encoding='utf-8')
        body = cpp.split('std::string ExecuteHealingLuaQuery(', 1)[1]
        builder = body.split('const std::string script =', 1)[1].split('if (!ExecuteLuaOnMainThread', 1)[0]
        pieces = re.findall(r'"(?:\\.|[^"\\])*"|\bsource\b|\bkey\b', builder)
        substitutions = {'source': json.dumps('return (' + expression + ')'), 'key': '"TestQuery"'}
        script = ''.join(substitutions[p] if p in substitutions else json.loads(p) for p in pieces)
        lua = LuaRuntime()
        lua.execute('function setglobal(k,v) _G[k]=v end')
        lua.execute(script)
        return lua.globals().TestQuery

    def test_types(self):
        for expression, expected in [('nil', 'Z:'), ('true', 'B:true'), ('false', 'B:false'),
                                     ('1+2', 'N:3'), ('"hello"', 'S:hello')]:
            self.assertEqual(self.run_query(expression), expected)

    def test_expression_errors(self):
        for expression in ['1+', '(function() error("bad") end)()', '{}', 'function() end', '0/0', 'math.huge']:
            self.assertTrue(self.run_query(expression).startswith('E:'), expression)

    def test_string_limits(self):
        self.assertEqual(self.run_query('string.rep("x",16000)'), 'S:' + 'x' * 16000)
        self.assertTrue(self.run_query('string.rep("x",16001)').startswith('E:'))
        self.assertTrue(self.run_query('string.char(0)').startswith('E:'))
        self.assertEqual(self.run_query('"a\\n\\\"b"'), 'S:a\n"b')


if __name__ == '__main__':
    unittest.main()
