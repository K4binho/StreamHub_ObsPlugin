#include "output-lifecycle.h"
#include <stdexcept>
#include <string>

static void check(bool condition) { if (!condition) throw std::runtime_error("Invalid output lifecycle transition"); }

int main()
{
    struct Target {
        std::string id, parentTargetId;
        bool enabled = true, syncStart = true, startWithParent = true;
    } parent{"youtube", ""}, child{"vertical", "youtube"};
    std::vector<Target *> targets{&parent, &child};
    check(StreamHubIncludedInStartAll(parent, targets));
    check(StreamHubIncludedInStartAll(child, targets));
    parent.enabled = false;
    check(!StreamHubIncludedInStartAll(child, targets));
    check(!StreamHubIncludedInStartAll(parent, targets));
    parent.enabled = true;
    child.startWithParent = false;
    child.syncStart = false;
    parent.syncStart = false;
    check(StreamHubIncludedInStartAll(child, targets));
    child.parentTargetId = "missing";
    check(!StreamHubIncludedInStartAll(child, targets));
    child.parentTargetId.clear();
    check(StreamHubIncludedInStartAll(child, targets));
    child.parentTargetId = "youtube";
    child.enabled = false;
    check(!StreamHubIncludedInStartAll(child, targets));
    child.enabled = true;
    child.startWithParent = true;
    child.syncStart = true;
    parent.syncStart = true;
    targets.insert(targets.begin(), nullptr);
    check(StreamHubShouldSyncStartWithMain(parent, targets));
    check(StreamHubShouldSyncStartWithMain(child, targets));
    parent.enabled = false;
    check(!StreamHubShouldSyncStartWithMain(child, targets));
    parent.enabled = true;
    parent.syncStart = false;
    check(!StreamHubShouldSyncStartWithMain(child, targets));
    parent.syncStart = true;
    child.startWithParent = false;
    check(!StreamHubShouldSyncStartWithMain(child, targets));
    child.startWithParent = true;
    child.parentTargetId = "missing";
    check(!StreamHubShouldSyncStartWithMain(child, targets));
    child.parentTargetId = "youtube";
    child.enabled = false;
    check(!StreamHubShouldSyncStartWithMain(child, targets));
    struct FakeOutput {
        bool enabled=true,busy=false,fail=false;
        int starts=0,stops=0;
        bool IsBusy()const{return busy;}
        void StartStreaming(){++starts;busy=!fail;}
        void StopStreaming(){++stops;busy=false;}
    } horizontal,vertical;
    std::vector<FakeOutput *> group{&horizontal,&vertical};
    auto enabled=[](auto *output){return output->enabled;};
    StreamHubToggleLinkedOutputs(group,enabled);
    check(horizontal.starts==1&&vertical.starts==1&&horizontal.busy&&vertical.busy);
    StreamHubToggleLinkedOutputs(group,enabled);
    check(horizontal.stops==1&&vertical.stops==1&&!horizontal.busy&&!vertical.busy);
    vertical.enabled=false;
    StreamHubToggleLinkedOutputs(group,enabled);
    check(horizontal.starts==2&&vertical.starts==1);
    StreamHubToggleLinkedOutputs(group,enabled);
    vertical.enabled=true;vertical.fail=true;
    StreamHubToggleLinkedOutputs(group,enabled);
    check(horizontal.busy&&!vertical.busy&&vertical.starts==2);
    StreamHubToggleLinkedOutputs(group,enabled);
    check(!horizontal.busy&&horizontal.stops==3&&vertical.stops==1);
    vertical.fail=false;vertical.busy=true;vertical.enabled=false;
    StreamHubToggleLinkedOutputs(group,enabled);
    check(vertical.stops==2&&horizontal.starts==3);
    std::vector<FakeOutput *> emptyGroup;
    StreamHubToggleLinkedOutputs(emptyGroup,enabled);
    horizontal.enabled=false;vertical.enabled=false;
    StreamHubToggleLinkedOutputs(group,enabled);
    check(!horizontal.busy&&!vertical.busy&&horizontal.starts==3&&vertical.starts==2);
    OutputLifecycle output;
    check(output.start());
    check(!output.start());
    check(output.stop());
    output.connected();
    output.reconnect();
    check(output.phase == OutputLifecycle::Phase::Stopping);
    check(!output.start());
    check(!output.stop());
    output.stopped();
    check(!output.busy());
    check(output.start());
    output.connected();
    check(output.phase == OutputLifecycle::Phase::Live);
    output.reconnect();
    check(output.phase == OutputLifecycle::Phase::Reconnecting);
    output.connected();
    check(output.phase == OutputLifecycle::Phase::Live);
    check(output.stop());
    output.stopped();
    output.connected();
    check(!output.busy());
}
