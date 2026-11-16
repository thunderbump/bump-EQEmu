"""Adversarial comparator controls; no native execution."""
import copy
import unittest
from evidence import parse_queries, parse_route, compare_route, compare, verdict
import json
from pathlib import Path


def fixture():
    rows = []
    for name, x, los in [('start',297.75,1),('clear',288,1),('blocked',320,0)]:
        rows.append(f'SERVER_COLLISION_QUERY id={name} time=1 npc=2011 entity=7 '
                    f'from=297.75,344,3.75 size=6 origin={x},344,3.75 '
                    f'adjusted={x},344,4.75 eyes=297.75,344,6.44999980926514 '
                    f'to={x},344,3.75 target_size=6 target_eyes={x},344,5.25 ground=0 los={los}')
    return '\n'.join(rows)


class Controls(unittest.TestCase):
    def test_exact_queries(self):
        rows = parse_queries(fixture())
        self.assertTrue(compare(rows, copy.deepcopy(rows)))

    def test_input_decision_numeric_mutations_rejected(self):
        rows = parse_queries(fixture())
        for field, value in [('from','298,344,3.75'), ('adjusted','288,344,104.75'),
                             ('size','5'), ('los','0'), ('ground','0.000001'),
                             ('ground','nan'), ('ground','inf')]:
            with self.subTest(field=field, value=value):
                changed = copy.deepcopy(rows)
                changed['clear'][field] = value
                with self.assertRaises(ValueError):
                    compare(rows, changed)

    def test_incomplete_duplicate_nonfinite_and_failed_controls(self):
        text = fixture()
        for broken in [text.splitlines()[0], text+'\n'+text.splitlines()[0],
                       text.replace('ground=0', 'ground=nan'),
                       text.replace('ground=0', 'ground=-99999'),
                       text.replace('los=0', 'los=1')]:
            with self.assertRaises(ValueError):
                parse_queries(broken)

    def test_missing_callers_and_shutdown_cannot_pass(self):
        receipt = {'fixed_queries_complete':True,'exits':{'world':1,'zone':0}}
        self.assertEqual(verdict(receipt), {'fixed_queries':True,'collision':False,'shutdown':False})
        receipt['exits']['world']=0
        self.assertFalse(verdict(receipt)['collision'])
        self.assertFalse(verdict({})['shutdown'])


if __name__ == '__main__':
    unittest.main()


class NativeControls(unittest.TestCase):
    def trace(self):
        config=json.loads(Path(__file__).with_name('route.json').read_text())
        lines=[fixture()]
        def native(caller,kind,origin,target,result):
            lines.append(f'SERVER_COLLISION_NATIVE caller={caller} kind={kind} entity=7 backend=OFF from={origin} to={target} result={result} remaining=4000')
        for row in parse_queries(fixture()).values():
            native('FindGroundZ','FindBestZ',row['adjusted'],'0,0,-99999',row['ground'])
            native('CheckLosFN','CheckLoS',row['eyes'],row['target_eyes'],row['los'])
        for i,(name,point) in enumerate(zip(('route_start','transition','destination'),config['points'])):
            xyz=','.join(map(str,point))
            lines.append(f'SERVER_COLLISION_FLOOR id={name} xyz={xyz} ground={i/2}')
            native('FindGroundZ','FindBestZ',xyz,'0,0,-99999',i/2)
        for caller,kind,result in [('FindDestGroundZ','FindBestZ',0),('AdjustRoute','FindBestZ',0),('AdjustRoute','node',1.2),('UpdatePathGround','admission',3),('MoveToCommand','dispatch',20),('FixZ','pose',1.2)]:
            native(caller,kind,'0,0,0','0,0,0',result)
        start,end=config['points'][0],config['points'][-1]
        for i,t in enumerate((0.2,0.5,1,1,1)):
            x,y=[start[j]+t*(end[j]-start[j]) for j in (0,1)]
            lines.append(f'SERVER_COLLISION_POSE tick={i+1} xyz={x},{y},1.2 ground=0 moving=0')
        lines.append('SERVER_COLLISION_ROUTE_DONE')
        return '\n'.join(lines),config

    def test_native_route_and_adversarial_controls(self):
        text,config=self.trace()
        before=parse_route(text,'OFF',config)
        self.assertTrue(compare_route(before,copy.deepcopy(before)))
        for broken in (text.replace('caller=FixZ','caller=Wrong'),text.replace('entity=7 backend','entity=8 backend'),text.replace('backend=OFF','backend=ON'),text.replace('moving=0','moving=1'),text.replace('kind=dispatch','kind=teleport'),text.replace('ground=0 moving','ground=nan moving'),text.replace('xyz=263.037598','xyz=263.1'),text.replace('caller=CheckLosFN kind=CheckLoS','caller=Wrong kind=CheckLoS')):
            with self.assertRaises(ValueError): parse_route(broken,'OFF',config)
        after=copy.deepcopy(before)
        after['fixed_native'][1]['to']='1,2,3'
        with self.assertRaises(ValueError): compare_route(before,after)
