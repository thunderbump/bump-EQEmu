#include "../zone/actor_population_native.h"
#include "actor-bridge/src/lib.rs.h"
#include "actor-bridge/src/population.rs.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace eqemu_proof;
using Key = ActorDemo::NativeIdentity;
void Require(bool value, const char *why) {
  if (!value) {
    throw std::runtime_error(why);
  }
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/eqemu-population-cxx-XXXXXX";
    auto *p = mkdtemp(name);
    Require(p, "test directory");
    path = p;
  }
  ~Directory() { std::filesystem::remove_all(path); }
};
struct NativeFixture {
  rust::Box<Runtime> runtime;
  ActorPopulation::Permit permit;
  Key probe;
  bool probe_ready = false, sitting = false, pending_kill = false;
  double x = -917, y = 1224, z = 3.75;
  int64_t hp = 100;
  uint64_t incoming = 0;
  explicit NativeFixture(unsigned i, uint64_t epoch_base = 100)
      : runtime(construct_population(epoch_base + i)) {
    permit.actor = {uint16_t(10 + i), epoch_base * 10 + i};
  }
};
Report Send(NativeFixture &m, Kind kind) {
  const auto s = m.runtime->inspect();
  rust::Vec<Input> v;
  v.push_back({kind, s.epoch, s.generation, 0, 0, 0, 0});
  return m.runtime->advance(s.sequence + 1, s.tick + 1, std::move(v));
}
Report Fact(NativeFixture &m, Kind kind, uint32_t damage = 0) {
  const auto s = m.runtime->inspect();
  return m.runtime->combat(
      s.sequence + 1, s.tick + 1,
      {s.epoch, s.generation, s.goal, s.operation, s.target, kind, damage});
}
rust::Vec<eqemu_population::Body> Frame(std::vector<NativeFixture> &models) {
  rust::Vec<eqemu_population::Body> result;
  for (unsigned i = 0; i < 3; ++i) {
    auto &m = models[i];
    const auto s = m.runtime->inspect();
    eqemu_population::Body b{};
    b.actor = i;
    b.id = m.permit.actor.id;
    b.incarnation = m.permit.actor.incarnation;
    b.epoch = s.epoch;
    b.generation = s.generation;
    b.goal = s.goal;
    b.operation = s.operation;
    b.alive = s.alive;
    b.present = s.body == Body::Present;
    b.drained = !s.alive;
    b.x = m.x;
    b.y = m.y;
    b.z = m.z;
    b.hp = m.hp;
    b.idle = s.phase == Phase::Idle && s.approach == Approach::Disabled;
    b.sitting = m.sitting;
    b.target_viable = true;
    b.contested = m.permit.contested;
    m.permit.contested = false;
    b.swings = s.swings;
    b.hits = s.hits;
    b.damage = s.damage;
    b.incoming_swings = m.incoming;
    b.incoming_hits = m.incoming;
    b.incoming_damage = m.incoming * 4;
    b.probe_id = m.probe.id;
    b.probe_incarnation = m.probe.incarnation;
    b.probe_ready = m.probe_ready;
    b.probe_ok = true;
    b.outcome = ActorPopulation::NativeOutcome(s, false);
    result.push_back(b);
  }
  return result;
}
rust::Vec<eqemu_population::Prey> Prey() {
  rust::Vec<eqemu_population::Prey> result;
  for (unsigned i = 0; i < 3; ++i) {
    eqemu_population::Prey p{};
    p.id = 20 + i;
    p.incarnation = 2000 + i;
    p.x = -917;
    p.y = 1224;
    p.z = 3.75;
    p.type_id = i == 1 ? 2002 : 2011;
    p.level = 1;
    p.name = i == 1 ? "a_gnoll_pup" : "a_large_rat";
    p.safe = p.idle = true;
    result.push_back(std::move(p));
  }
  return result;
}
void Apply(std::vector<NativeFixture> &models,
           const rust::Vec<eqemu_population::Action> &actions) {
  using Pop = eqemu_population::Kind;
  for (const auto &a : actions) {
    Require(a.actor < 3, "unbounded actor action");
    auto &m = models[a.actor];
    if (a.kind == Pop::Spawn) {
      m.x = a.x;
      m.y = a.y;
      m.z = a.z;
      m.hp = a.hp;
      Require(Send(m, Kind::CreateMeleeBody).event == Event::BodyRequested,
              "per-actor runtime creation");
      Require(Send(m, Kind::BodyPresent).event == Event::BodyPresent,
              "fixture body acknowledgement");
      const auto s = m.runtime->inspect();
      m.permit.epoch = s.epoch;
      m.permit.generation = s.generation;
    } else if (a.kind == Pop::Probe) {
      m.probe = {a.target_id, a.target_incarnation};
      m.probe_ready = true;
      m.permit.target = m.probe;
    } else if (a.kind == Pop::Rest) {
      m.sitting = true;
      m.permit.Close();
      m.permit.target = {};
    } else if (a.kind == Pop::Stand) {
      m.sitting = false;
    } else if (a.kind == Pop::Engage) {
      Require(Send(m, Kind::StartPublicHunt).status == Status::Ok,
              "per-actor public goal");
      auto s = m.runtime->inspect();
      Observation o{};
      o.epoch = s.epoch;
      o.generation = s.generation;
      o.goal = s.goal;
      o.x = std::lround(m.x * 1000);
      o.y = std::lround(m.y * 1000);
      o.z = std::lround(m.z * 1000);
      Candidate p{};
      p.incarnation = a.target_incarnation;
      p.x = -917000;
      p.y = 1224000;
      p.z = 3750;
      p.present = p.alive = p.targetable = true;
      p.type_id = a.target_id == 21 ? 2002 : 2011;
      p.level = 1;
      o.candidates.push_back(p);
      Require(
          m.runtime->observe(s.sequence + 1, s.tick + 1, std::move(o)).event ==
              Event::AttackRequested,
          "expanded profile at actual runtime seam");
      Require(Fact(m, Kind::AttackAdmitted).event == Event::AttackAdmitted,
              "fixture native admission");
      m.permit.target = {a.target_id, a.target_incarnation};
      m.permit.goal = m.runtime->inspect().goal;
      m.permit.enabled = true;
      m.permit.inhibited = false;
    } else if (a.kind == Pop::Release || a.kind == Pop::Retire) {
      m.permit.Close();
      Send(m, a.kind == Pop::Release ? Kind::ReleasePublicHunt : Kind::Retire);
      auto s = m.runtime->inspect();
      if (s.attacking) {
        Fact(m, m.pending_kill ? Kind::TargetKilled : Kind::AttackInterrupted);
      }
      if (a.kind == Pop::Retire) {
        s = m.runtime->inspect();
        if (s.body == Body::Present) {
          Send(m, Kind::Retire);
        }
        Require(Send(m, Kind::BodyAbsent).event == Event::BodyAbsent,
                "actor-only native absence");
      }
    }
  }
}
std::string Read(const std::filesystem::path &p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void Command(const Directory &d, const char *text) {
  std::ofstream(d.path / "command") << text;
}

// Actual CXX boundary and Rust runtimes, with explicit native fixture facts.
// These controls do not execute zone navigation, NPC AI or client packets.
void ThreeActorsClaimsCallbacksAndRestore() {
  Directory d;
  std::vector<NativeFixture> m;
  for (unsigned i = 0; i < 3; ++i) {
    m.emplace_back(i);
  }
  {
    auto pop = eqemu_population::population_open(d.path.string(), "one");
    bool refused = false;
    try {
      auto duplicate =
          eqemu_population::population_open(d.path.string(), "duplicate");
    } catch (const std::exception &) {
      refused = true;
    }
    Require(refused, "duplicate checkpoint owner accepted");
    Command(d, "start one 1 all\n");
    Apply(m, pop->step(1, Frame(m), Prey(), true, false));
    Apply(m, pop->step(2, Frame(m), Prey(), false, false));
    Apply(m, pop->step(10002, Frame(m), Prey(), false, false));
    for (uint64_t now = 11000; now < 13000; now += 201) {
      Apply(m, pop->step(now, Frame(m), Prey(), false, false));
    }
    for (unsigned i = 0; i < 3; ++i) {
      Require(m[i].permit.Allows(m[i].permit.actor, m[i].permit.target),
              "missing distinct admitted actor");
      const auto s = m[i].runtime->inspect();
      Require(m[i].permit.Lifetime(s.epoch, s.generation, s.goal) &&
                  !m[i].permit.Lifetime(s.epoch, s.generation, s.goal + 1),
              "stale goal inherited callback authority");
      for (unsigned j = 0; j < 3; ++j) {
        const auto owner = m[j].runtime->inspect();
        Require(m[j].permit.MatchesIncoming(
                    m[i].permit.target, m[i].permit.actor, owner.epoch,
                    owner.generation, owner.goal) == (i == j),
                "incoming native callback attributed to wrong runtime");
      }
      Require(m[i].permit.MatchesIncoming(m[i].permit.target,
                                          {0, m[i].permit.actor.incarnation},
                                          s.epoch, s.generation, s.goal),
              "fatal native callback lost cleared body ID");
      auto reused = m[i].permit.actor;
      ++reused.incarnation;
      Require(!m[i].permit.Allows(reused, m[i].permit.target),
              "native ID reuse inherited pair");
      for (unsigned j = 0; j < i; ++j) {
        Require(!m[i].permit.target.Matches(m[j].permit.target),
                "two actors own one target");
      }
    }
    for (auto &model : m) {
      model.permit.PublicContact({}, m[0].permit.target);
    }
    Require(!m[0].permit.Allows(m[0].permit.actor, m[0].permit.target),
            "native contention waits for later Rust tick");
    Require(m[1].permit.Allows(m[1].permit.actor, m[1].permit.target),
            "contention attributed to wrong actor");
    m[0].hp = 47;
    m[0].incoming = 1;
    Apply(m, pop->step(14000, Frame(m), Prey(), false, false));
    Apply(m, pop->step(14001, Frame(m), Prey(), false, false));
    Require(m[1].runtime->inspect().attacking &&
                m[2].runtime->inspect().attacking,
            "release cancelled unrelated runtime");
    Fact(m[1], Kind::SwingHit, 6);
    Fact(m[1], Kind::SwingHit, 5);
    Fact(m[1], Kind::TargetKilled);
    m[1].hp = 73;
    for (auto &model : m) {
      model.permit.Death({0, m[1].permit.target.incarnation});
    }
    Require(!m[1].permit.enabled && m[2].permit.enabled,
            "native death callback closed wrong actor");
    Apply(m, pop->step(15000, Frame(m), Prey(), false, false));
    Apply(m, pop->step(15001, Frame(m), Prey(), false, false));
    Command(d, "stop one 2 hunter-3\n");
    Apply(m, pop->step(16000, Frame(m), Prey(), false, false));
    Apply(m, pop->step(16001, Frame(m), Prey(), false, false));
    Require(m[0].runtime->inspect().alive && m[1].runtime->inspect().alive &&
                !m[2].runtime->inspect().alive,
            "one-stop retired wrong bodies");
    Command(d, "drain one 3\n");
    auto draining = pop->step(17000, Frame(m), Prey(), false, false);
    Require(Read(d.path / "actors-v3/actor-1.json").find("\"clean\":false") !=
                std::string::npos,
            "checkpoint clean before native drain");
    Apply(m, draining);
    Apply(m, pop->step(17001, Frame(m), Prey(), false, false));
    Require(Read(d.path / "status.json").find("\"restart_safe\":true") !=
                std::string::npos,
            "clean acknowledgement before/after actual drain incorrect");
  }
  auto restored = eqemu_population::population_open(d.path.string(), "two");
  std::vector<NativeFixture> fresh;
  for (unsigned i = 0; i < 3; ++i) {
    fresh.emplace_back(i, 200);
  }
  auto actions = restored->step(1, Frame(fresh), Prey(), false, false);
  Require(actions.size() == 2,
          "paused actor restored or enabled actor missing");
  for (const auto &a : actions) {
    Require(a.kind == eqemu_population::Kind::Spawn && a.actor < 2 &&
                a.hp == (a.actor == 0 ? 47 : 73),
            "clean restore silently healed or replayed action");
  }
  Apply(fresh, actions);
  Require(fresh[0].runtime->inspect().epoch != m[0].runtime->inspect().epoch,
          "restore reused native runtime lifetime");
}
void ActiveCancelAndPendingKillDrain() {
  Directory d;
  std::vector<NativeFixture> m;
  for (unsigned i = 0; i < 3; ++i) {
    m.emplace_back(i);
  }
  auto pop = eqemu_population::population_open(d.path.string(), "pending");
  Command(d, "start pending 1 all\n");
  for (uint64_t now : {1, 2, 10002, 11000, 11201, 11402, 11603, 11804, 12005}) {
    Apply(m, pop->step(now, Frame(m), Prey(), false, false));
  }
  const auto body = m[0].permit.actor;
  m[0].permit.Interfere();
  Apply(m, pop->step(13000, Frame(m), Prey(), false, false));
  Require(m[0].runtime->inspect().approach == Approach::Paused,
          "fixture hid the later native cancellation terminal");
  Apply(m, pop->step(13001, Frame(m), Prey(), false, false));
  Apply(m, pop->step(13002, Frame(m), Prey(), false, false));
  Require(m[0].runtime->inspect().alive &&
              m[0].runtime->inspect().approach == Approach::Disabled &&
              m[0].permit.actor.Matches(body),
          "active release never reset persistent body");
  Fact(m[1], Kind::SwingHit, 11);
  m[1].pending_kill = true;
  Command(d, "drain pending 2\n");
  Apply(m, pop->step(14000, Frame(m), Prey(), false, false));
  Require(m[1].runtime->inspect().approach == Approach::Retiring &&
              m[1].runtime->inspect().last_terminal == Event::TargetKilled,
          "pending native kill did not race retirement");
  Apply(m, pop->step(14001, Frame(m), Prey(), false, false));
  const auto status = Read(d.path / "status.json");
  Require(status.find("\"completed\":1") != std::string::npos &&
              status.find("\"restart_safe\":true") != std::string::npos,
          "clean drain lost correlated pending kill");
}

} // namespace
// Shared by the utility suite and the standalone control executable.
int RunActorPopulationBridgeControls() {
  try {
    ThreeActorsClaimsCallbacksAndRestore();
    ActiveCancelAndPendingKillDrain();
    std::cout << "population CXX/Rust bridge: three actors, claims, callback "
                 "isolation, one/all drain and wounded restore passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &e) {
    std::cerr << "population CXX/Rust bridge: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}

#ifndef EQEMU_ACTOR_CONTROLS_IN_UTILITY_SUITE
int main()
{
    return RunActorPopulationBridgeControls();
}
#endif
