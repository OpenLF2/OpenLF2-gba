# SPDX-License-Identifier: MIT
"""Parse the original stage script and generate compact read-only encounter tables."""
import re

TOKEN = re.compile(r'<[^>]+>|[^\s]+')


def parse(text):
    tokens = TOKEN.findall('\n'.join(line.split('#', 1)[0] for line in text.splitlines()))
    stages = {}
    stage = phase = entry = None
    unopened_x = 500
    i = 0
    while i < len(tokens):
        token = tokens[i]
        i += 1
        if token == '<stage>':
            stage = phase = entry = None
        elif token == '<stage_end>':
            stage = phase = entry = None
        elif token == '<phase>':
            if stage is None:
                raise ValueError('phase before stage id')
            phase = {'bound': -1, 'goto': -1, 'music': '', 'entries': []}
            stage.append(phase)
            entry = None
            unopened_x = 500
        elif token == '<phase_end>':
            phase = entry = None
        elif token in ('<boss>', '<soldier>'):
            if entry is None:
                raise ValueError('stage entry kind before id')
            entry['kind'] = 2 if token == '<boss>' else 1
            if token == '<soldier>':
                entry['times'] = 50
        elif token.endswith(':') and i < len(tokens):
            key, value = token[:-1], tokens[i]
            i += 1
            if key == 'id' and phase is None:
                stage_id = int(value)
                if not 0 <= stage_id < 60:
                    raise ValueError('stage id out of range')
                stage = stages[stage_id] = []
            elif phase is not None and key == 'id':
                entry = {'id': int(value), 'x': unopened_x, 'hp': 500, 'times': 1,
                         'reserve': 0, 'join': 0, 'join_reserve': 0, 'act': 9, 'y': 0,
                         'ratio': 0.0, 'kind': 0}
                phase['entries'].append(entry)
            elif phase is not None and key == 'bound':
                phase['bound'] = int(value)
                unopened_x = phase['bound'] + 80
                for existing in phase['entries']:
                    existing['x'] = unopened_x
            elif phase is not None and key == 'music':
                phase['music'] = value.replace('\\', '/').lower()
            elif phase is not None and key == 'when_clear_goto_phase':
                phase['goto'] = int(value)
            elif entry is not None and key in entry:
                entry[key] = float(value) if key == 'ratio' else int(value)
    return stages


def generate(stages):
    entries, phases, table = [], [], []
    music = {'bgm/stage1.wma': 1, 'bgm/stage2.wma': 2, 'bgm/stage3.wma': 3,
             'bgm/stage4.wma': 4, 'bgm/stage5.wma': 5, 'bgm/boss1.wma': 6, 'bgm/boss2.wma': 7}
    for stage_id in range(60):
        if stage_id not in stages:
            table.append((-1, 0))
            continue
        start = len(phases)
        for phase in stages[stage_id]:
            first = len(entries)
            for e in phase['entries']:
                entries.append((e['id'], e['x'], e['hp'], e['times'], e['reserve'], e['join'],
                                e['join_reserve'], e['act'], e['y'], round(e['ratio'] * 100), e['kind']))
            phases.append((phase['bound'], phase['goto'], first, len(phase['entries']), music.get(phase['music'], 0)))
        table.append((start, len(stages[stage_id])))
    def rows(values):
        return ',\n'.join('    {' + ', '.join(str(x) for x in row) + '}' for row in values)
    return ('// Generated from data/stage.dat. Do not edit.\n#pragma once\n#include "stage_data.hpp"\n'
            'namespace lf2 {\n'
            'inline constexpr StageEntry STAGE_ENTRIES[] = {\n' + rows(entries) + '\n};\n'
            'inline constexpr StagePhase STAGE_PHASES[] = {\n' + rows(phases) + '\n};\n'
            'inline constexpr StageScript STAGE_SCRIPTS[60] = {\n' + rows(table) + '\n};\n'
            '}\n')
