#pragma once
#include "actor_hunt_state.h"

#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>

namespace ActorDemo {
constexpr uint32_t DefaultSeconds = 1800;
constexpr uint32_t EncounterCap = 100;

struct Command {
    bool start = false;
    std::string session;
    uint64_t request = 0;
    uint32_t seconds = DefaultSeconds;
};

// A small, bounded local operator protocol. No signs, overflow or extra fields.
inline bool ParseCommand(const std::string &text, Command &command)
{
    if (text.size() > 128) { return false; }
    auto line = text;
    if (!line.empty() && line.back() == '\n') { line.pop_back(); }
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    for (const auto c : line) {
        if ((c < ' ' && c != '\t') || c > '~') { return false; }
    }
    std::istringstream input(line);
    std::string verb, request, seconds, extra;
    Command parsed;
    if (!(input >> verb >> parsed.session >> request) ||
        (verb != "start" && verb != "stop")) { return false; }
    auto number = [](const std::string &s, auto &out) {
        const auto result = std::from_chars(s.data(), s.data() + s.size(), out);
        return result.ec == std::errc{} && result.ptr == s.data() + s.size();
    };
    if (!number(request, parsed.request) || !parsed.request) { return false; }
    parsed.start = verb == "start";
    if (input >> seconds) {
        if (!parsed.start || !number(seconds, parsed.seconds)) { return false; }
    }
    if (input >> extra || parsed.seconds < 10 || parsed.seconds > DefaultSeconds) { return false; }
    command = parsed;
    return true;
}

inline const char *AdmitRequest(const Command &command, const std::string &session, uint64_t &last_request)
{
    if (command.session != session) { return "stale_session"; }
    if (command.request <= last_request) { return "replayed_request"; }
    last_request = command.request;
    return nullptr;
}

enum class Phase { Idle, Waiting, Spawning, Fighting, Draining, Completed, Stopped, Failed, Searching, Releasing, Resting };
inline const char *PhaseName(Phase phase)
{
    switch (phase) {
    case Phase::Idle: return "idle";
    case Phase::Waiting: return "waiting";
    case Phase::Spawning: return "spawning";
    case Phase::Fighting: return "fighting";
    case Phase::Draining: return "draining";
    case Phase::Completed: return "completed";
    case Phase::Stopped: return "stopped";
    case Phase::Failed: return "failed";
    case Phase::Searching: return "searching";
    case Phase::Releasing: return "releasing";
    case Phase::Resting: return "resting";
    }
    return "failed";
}
enum class Action { None, Spawn, Engage, Retire, Release, Rest, Stand };
struct Facts {
    bool ready = false, victory = false, drained = true, progress = false;
    const char *failure = nullptr;
    const char *yield = nullptr;
    bool target_available = false, goal_idle = false, rested = false;
};

// Time/budget policy is separate from native facts. It never advances Rust or
// manufactures combat outcomes. One native encounter is owned at a time.
class Run {
public:
    Phase phase = Phase::Idle;
    std::string reason;
    uint32_t attempted = 0, completed = 0, failed = 0;
    uint32_t yielded = 0;
    std::string last_outcome;
    uint64_t began = 0, ended = 0;

    bool Active() const {
        return phase == Phase::Waiting || phase == Phase::Spawning ||
            phase == Phase::Fighting || phase == Phase::Draining || phase == Phase::Searching ||
            phase == Phase::Releasing || phase == Phase::Resting;
    }
    void Start(uint64_t now, uint32_t seconds, bool public_hunt = false) {
        *this = Run{};
        began = stage_at_ = progress_at_ = now;
        expires_ = now + uint64_t(seconds) * 1000;
        phase = Phase::Waiting;
        public_hunt_ = public_hunt;
    }
    uint64_t RestElapsed(uint64_t now) const { return phase == Phase::Resting ? now - stage_at_ : 0; }
    Action Yield(uint64_t now, const char *why) {
        ++yielded;
        last_outcome = why;
        phase = Phase::Releasing;
        stage_at_ = now;
        return Action::Release;
    }
    Action Stop(uint64_t now, const char *why, Phase terminal = Phase::Stopped) {
        if (!Active() || !reason.empty()) { return Action::None; }
        const bool draining = phase == Phase::Draining;
        if (terminal == Phase::Failed && attempted > completed + failed) { ++failed; }
        reason = why;
        terminal_ = terminal;
        phase = Phase::Draining;
        if (!draining) { stage_at_ = now; }
        return draining ? Action::None : Action::Retire;
    }
    Action Advance(uint64_t now, const Facts &facts) {
        if (!Active()) { return Action::None; }
        if (phase == Phase::Draining) {
            if (facts.drained) {
                if (!reason.empty()) { phase = terminal_; ended = now; }
                else { phase = Phase::Waiting; stage_at_ = now + 5000; }
            } else if (now - stage_at_ >= 10000) {
                if (attempted > completed + failed) { ++failed; }
                reason = "cleanup_stalled"; phase = Phase::Failed; ended = now;
            }
            return Action::None;
        }
        if (facts.failure) { return Stop(now, facts.failure, Phase::Failed); }
        if (now >= expires_) { return Stop(now, "duration_expired", Phase::Completed); }
        if (public_hunt_) { return AdvanceHunt(now, facts); }
        if (phase == Phase::Waiting) {
            if (attempted >= EncounterCap) { return Stop(now, "encounter_cap", Phase::Completed); }
            if (now < stage_at_) { return Action::None; }
            ++attempted;
            stage_at_ = progress_at_ = now;
            phase = Phase::Spawning;
            return Action::Spawn;
        }
        if (facts.progress) { progress_at_ = now; }
        if (now - progress_at_ >= 30000 || now - stage_at_ >= 60000) {
            return Stop(now, "encounter_stalled", Phase::Failed);
        }
        if (phase == Phase::Spawning && facts.ready) {
            phase = Phase::Fighting;
            return Action::Engage;
        }
        if (phase == Phase::Fighting && facts.victory) {
            ++completed;
            phase = Phase::Draining;
            stage_at_ = now;
            return Action::Retire;
        }
        return Action::None;
    }
private:
    Action AdvanceHunt(uint64_t now, const Facts &facts) {
        switch (phase) {
        case Phase::Waiting:
            phase = Phase::Spawning; stage_at_ = now;
            return Action::Spawn;
        case Phase::Spawning:
            if (facts.ready) { phase = Phase::Searching; return Action::Stand; }
            if (now - stage_at_ >= 10000) { return Stop(now, "body_spawn_stalled", Phase::Failed); }
            break;
        case Phase::Searching:
            if (attempted >= EncounterCap) { return Stop(now, "encounter_cap", Phase::Completed); }
            if (facts.yield) { return Yield(now, facts.yield); }
            if (facts.target_available) {
                ++attempted; phase = Phase::Fighting; stage_at_ = progress_at_ = now;
                return Action::Engage;
            }
            break;
        case Phase::Fighting:
            if (facts.victory) {
                ++completed; last_outcome = "target_killed";
                phase = Phase::Releasing; stage_at_ = now;
                return Action::Release;
            }
            if (facts.yield) { return Yield(now, facts.yield); }
            if (facts.progress) { progress_at_ = now; }
            if (now - progress_at_ >= 30000 || now - stage_at_ >= 60000) {
                return Yield(now, "encounter_stalled");
            }
            break;
        case Phase::Releasing:
            if (facts.goal_idle) { phase = Phase::Resting; stage_at_ = now; return Action::Rest; }
            if (now - stage_at_ >= 10000) { return Stop(now, "goal_release_stalled", Phase::Failed); }
            break;
        case Phase::Resting:
            if (facts.yield) {
                last_outcome = facts.yield; stage_at_ = now;
                return Action::Rest; // another body-directed attempt restarts the quiet interval
            }
            if (now - stage_at_ >= HuntRestMinimumMs && facts.rested) { phase = Phase::Searching; return Action::Stand; }
            break;
        default: break;
        }
        return Action::None;
    }
    uint64_t expires_ = 0, stage_at_ = 0, progress_at_ = 0;
    Phase terminal_ = Phase::Completed;
    bool public_hunt_ = false;
};
} // namespace ActorDemo
