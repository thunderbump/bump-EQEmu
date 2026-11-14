"""Harmless JSON/evidence regressions; actual client runs only in the worker."""
import copy
import unittest
from controls import admit, parse_queries, verdicts, MAP, WORLD

class Controls(unittest.TestCase):
    def setUp(self):
        self.state = dict(session='own',zone_epoch=1,phase='ready',zone=dict(name='qeynos2',id=2),geometry=dict(digest=MAP,status='ready'),freshness=dict(link_age_ms=0,network_tick_age_ms=10,inbound_age_ms=20),self=dict(id=75,name='Clientab',position=[288,344,0],on_ground=True,in_water=False,attack_on=False))
        self.server=dict(name='Clientab',entity=75,account=1,character=1,aggro=0,observed=10)
    def check(self,state=None,world=None,now=10):
        admit(state or self.state,('own',1,75),world or WORLD,now,12,self.server)
    def test_admission_and_refusals(self):
        self.check()
        for path,value in [('session','foreign'),('zone_epoch',2),('phase','loading'),('geometry.digest','wrong'),('self.on_ground',False),('self.in_water',True),('self.attack_on',True),('self.position',[306,344,0]),('freshness.inbound_age_ms',6000)]:
            s=copy.deepcopy(self.state); parts=path.split('.'); d=s
            for p in parts[:-1]: d=d[p]
            d[parts[-1]]=value
            with self.subTest(path=path),self.assertRaises(AssertionError): self.check(s)
        with self.assertRaises(AssertionError): self.check(world=dict(name='BumpStaging',host='127.0.0.1'))
        with self.assertRaises(AssertionError): self.check(now=12)
        self.server['aggro']=1
        with self.assertRaises(AssertionError): self.check()
    def test_query_id_positive_control_stability(self):
        def line(name,los):
            return f'CLIENT_AB_QUERY id={name} time=1 from=297.75,344,3.75 size=6 eyes=297.75,344,6.45 to=288,344,3.75 target_size=6 target_eyes=288,344,5.25 ground=0 los={los}'
        log=line('clear',1)+'\n'+line('blocked',0)
        self.assertEqual(set(parse_queries(log)),{'clear','blocked'})
        for bad in [log.replace('id=blocked','id=unknown'),log.replace('los=0','los=1'),log.replace('ground=0','ground=nan'),log+'\n'+line('clear',1).replace('ground=0','ground=1')]:
            with self.assertRaises(AssertionError): parse_queries(bad)
    def test_shutdown_separate(self):
        v=verdicts(dict(canonical_queries=True,movement_complete=True,server_pose_observed=True,exits=dict(client=0,world=1,login=-15)))
        self.assertTrue(v['backend_direct_query']);self.assertTrue(v['client_command_smoke']);self.assertFalse(v['orderly_shutdown'])

if __name__=='__main__': unittest.main()
