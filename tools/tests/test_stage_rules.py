# SPDX-License-Identifier: MIT
"""Checks stage-script defaults, ordering and compact table generation."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
from stage_rules import parse, generate


class StageRulesTest(unittest.TestCase):
    def test_entries_and_branch(self):
        script = '''<stage> id: 0
          <phase> bound: 900 music: bgm\\stage1.wma
            id: 30 hp: 50 times: 2 ratio: .7 <boss> # ignored id: 99
            id: 31 x: -1000 <soldier>
            when_clear_goto_phase: 7
          <phase_end>
        <stage_end>'''
        stages = parse(script)
        phase = stages[0][0]
        self.assertEqual((phase['bound'], phase['goto'], phase['music']), (900, 7, 'bgm/stage1.wma'))
        self.assertEqual((phase['entries'][0]['id'], phase['entries'][0]['x'], phase['entries'][0]['ratio'], phase['entries'][0]['kind']), (30, 980, .7, 2))
        self.assertEqual((phase['entries'][1]['x'], phase['entries'][1]['times'], phase['entries'][1]['kind']), (-1000, 50, 1))
        generated = generate(stages)
        self.assertIn('{30, 980, 50, 2, 0, 0, 0, 9, 0, 70, 2}', generated)
        self.assertIn('{900, 7, 0, 2, 1}', generated)

    def test_bound_resets_existing_entry_positions(self):
        stages = parse('<stage> id: 1 <phase> id: 30 x: 100 bound: 1200 id: 31 <phase_end> <stage_end>')
        self.assertEqual([e['x'] for e in stages[1][0]['entries']], [1280, 1280])

    def test_repeated_stage_replaces_old_definition(self):
        stages = parse('<stage> id: 1 <phase> id: 30 <phase_end> <stage_end> '
                       '<stage> id: 1 <phase> id: 31 <phase_end> <stage_end>')
        self.assertEqual(stages[1][0]['entries'][0]['id'], 31)


if __name__ == '__main__':
    unittest.main()
