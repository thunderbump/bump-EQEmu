"""Adversarial comparator controls; no native execution."""
import copy
import unittest
from evidence import parse_queries, compare, verdict


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
