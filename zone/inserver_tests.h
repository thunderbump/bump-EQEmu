#pragma once

namespace InserverTests {

// Opt in only through the VM test environment. Invalid opt-in fails startup.
bool Configure();

// Called after normal native processing; never drives entity processing itself.
void Process();

} // namespace InserverTests
