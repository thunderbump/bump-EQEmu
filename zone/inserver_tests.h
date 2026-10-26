#pragma once

namespace InserverTests {

// Opt in only through the VM test environment. Invalid opt-in fails startup.
bool Configure();

// Called after normal native processing, including a test-only pending boot
// while unloaded. Never drives entity processing itself.
void Process();

#ifdef EQEMU_RUST_BOOTSTRAP_BRIDGE
// Passive assertions, armed only by the opt-in live-actor shutdown scenario.
// These never perform teardown on behalf of the production path.
enum class ActorShutdownStage { AdmissionClosed, EntitiesCleared, RuntimeClosed };
void ObserveActorShutdown(ActorShutdownStage stage);
// Bounded passive diagnostics, enabled only during the Qeynos fixture visit.
void ObserveMeleeBootStage(const char *stage);
// Test-only command interposition after native movement, before fact delivery.
void BeforeActorFacts();
#endif

} // namespace InserverTests
