#pragma once
#include <algorithm>
#include <vector>

template<class Target, class Targets>
bool StreamHubIncludedInStartAll(const Target &target, const Targets &targets)
{
    if (!target.enabled) return false;
    if (target.parentTargetId.empty()) return true;
    return std::any_of(targets.begin(), targets.end(), [&target](const auto &parent) {
        return parent && parent->id == target.parentTargetId && parent->enabled;
    });
}

template<class Target, class Targets>
bool StreamHubShouldSyncStartWithMain(const Target &target, const Targets &targets)
{
    if (!target.enabled || !target.syncStart) return false;
    if (target.parentTargetId.empty()) return true;
    if (!target.startWithParent) return false;
    return std::any_of(targets.begin(), targets.end(), [&target](const auto &parent) {
        return parent && parent->id == target.parentTargetId &&
               parent->enabled && parent->syncStart;
    });
}

template<class Output, class Enabled>
void StreamHubToggleLinkedOutputs(const std::vector<Output *> &outputs, Enabled enabled)
{
    const bool stopping=std::any_of(outputs.begin(),outputs.end(),[](auto *output){return output->IsBusy();});
    for(auto *output:outputs) {
        if(stopping) { if(output->IsBusy())output->StopStreaming(); }
        else if(enabled(output))output->StartStreaming();
    }
}

class OutputLifecycle {
public:
    enum class Phase { Idle, Starting, Live, Reconnecting, Stopping };
    Phase phase = Phase::Idle;
    bool busy() const { return phase != Phase::Idle; }
    bool start() {
        if (busy()) return false;
        phase = Phase::Starting;
        return true;
    }
    bool stop() {
        if (!busy() || phase == Phase::Stopping) return false;
        phase = Phase::Stopping;
        return true;
    }
    void connected() {
        if (phase == Phase::Starting || phase == Phase::Reconnecting) phase = Phase::Live;
    }
    void reconnect() {
        if (phase == Phase::Starting || phase == Phase::Live) phase = Phase::Reconnecting;
    }
    void stopped() { phase = Phase::Idle; }
};
