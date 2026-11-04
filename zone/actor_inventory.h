#pragma once
#include <string>
class NPC;
namespace ActorInventory {
// Serialized values only. ItemInstance::Serialize contains a pointer and is not
// a checkpoint format. Errors throw; callers must retire rather than drop items.
std::string Snapshot(NPC &body);
void Restore(NPC &body, const std::string &snapshot);
}
