/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "zone/cli/tests/actor_lifecycle_scenario.h"
#include <iostream>
#include <stdexcept>
void Check(bool ok) { if(!ok) throw std::runtime_error("actor command control failed"); }
int main() {
	try {
		// Scope success, assertion unwinding and cancellation all restore the false baseline.
		for (int mode=0; mode<3; ++mode) {
			bool enabled=false, restored=false;
			try {
				ActorScenario::StateSavingScope scope([&](bool value) { enabled=value; return true; }, enabled, restored);
				Check(enabled && !restored);
				if (mode==1) throw std::runtime_error("forced assertion");
				if (mode==2) throw std::runtime_error("observed cancellation");
			} catch (const std::runtime_error &) { Check(mode!=0); }
			Check(!enabled && restored);
		}
		ActorScenario::Control c;
		char executable[]="zone", command[]="tests:actor-lifecycle", assertion[]="--force-failure-after-create", cancel[]="--wait-for-cancellation-after-create", unknown[]="--zone=other";
		char *args[]={executable,command,assertion,cancel};
		Check(ActorScenario::Selected(2,args) && ActorScenario::Arguments(2,args,c) && c==ActorScenario::Control::None);
		Check(ActorScenario::Arguments(3,args,c) && c==ActorScenario::Control::Assertion);
		Check(!ActorScenario::Arguments(4,args,c));args[2]=cancel;
		Check(ActorScenario::Arguments(3,args,c) && c==ActorScenario::Control::Cancel);args[2]=unknown;
		Check(!ActorScenario::Arguments(3,args,c));
		ActorScenario::Result r;Check(r.ExitCode()==2);r.status="passed";r.native_cleanup=true;Check(r.ExitCode()==2);
		r.completed_cases={"create-duplicate","name-collision","native-processing","retire-recreate","external-removal-id-reuse","save-fresh-zone"};
		r.cycles=3;r.ticks=3;r.id_reuse=true;r.save_restore=true;Check(r.ExitCode()==0);
		auto late_success=r; late_success.Finalize(true);
		Check(late_success.status=="cancelled" && late_success.native_cleanup && late_success.ExitCode()==2);
		std::cout<<"late-success "<<late_success.ExitCode()<<" "<<late_success.Json()<<"\n";
		std::cout<<"positive "<<r.ExitCode()<<" "<<r.Json()<<"\n";
		r.control=ActorScenario::Control::Assertion;Check(r.ExitCode()==2);r.status="assertion-failed";Check(r.ExitCode()==1);
		auto late_assertion=r; late_assertion.Finalize(true);
		Check(late_assertion.status=="cancelled" && late_assertion.native_cleanup && late_assertion.ExitCode()==2);
		std::cout<<"late-assertion "<<late_assertion.ExitCode()<<" "<<late_assertion.Json()<<"\n";
		std::cout<<"assertion "<<r.ExitCode()<<" "<<r.Json()<<"\n";
		r.control=ActorScenario::Control::Cancel;r.status="cancelled";Check(r.ExitCode()==2);
		std::cout<<"cancel "<<r.ExitCode()<<" "<<r.Json()<<"\n";
		r.control=ActorScenario::Control::None;r.status="refused";Check(r.ExitCode()==2);
		std::cout<<"refused "<<r.ExitCode()<<" "<<r.Json()<<"\n";
		r.status="assertion-failed";r.native_cleanup=false;Check(r.ExitCode()==2);
		auto late_unclean=r;late_unclean.Finalize(true);Check(late_unclean.status=="cancelled" && !late_unclean.native_cleanup && late_unclean.ExitCode()==2);
		std::cout<<"unclean "<<r.ExitCode()<<" "<<r.Json()<<"\n";
		r.native_cleanup=true;r.status="passed";r.completed_cases[0]=r.completed_cases[1];Check(r.ExitCode()==2);
		return 0;
	} catch(const std::exception &e) {std::cerr<<e.what()<<"\n";return 1;}
}
