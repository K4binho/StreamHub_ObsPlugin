#include "vertical-editor-core.h"
#include "vertical-canvas-name.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <atomic>
#include <thread>
#include <chrono>
#include <graphics/graphics.h>
static void check(bool value,const char *message);

struct VideoProbe {std::atomic<int> top{-1},inset{-1},stride{0};};
struct ProbeOutput {obs_output_t *output=nullptr;VideoProbe probe;};
static ProbeOutput *activeProbe=nullptr;
static void probeFrame(void *p,video_data *frame) {
    auto &probe=*static_cast<VideoProbe *>(p);
    probe.top=frame->data[0][10*frame->linesize[0]+10];
    probe.inset=frame->data[0][250*frame->linesize[0]+250];
    probe.stride=int(frame->linesize[0]);
}
template<class Predicate>static bool awaitFrame(Predicate ready) {
    for(int i=0;i<100;++i){if(ready())return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    return false;
}
static void testLiveOverlay() {
    obs_source_info input{};input.id="streamhub-test-overlay";input.type=OBS_SOURCE_TYPE_INPUT;input.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
    input.get_name=[](void *){return "Overlay fixture";};
    input.create=[](obs_data_t *,obs_source_t *)->void *{return reinterpret_cast<void *>(1);};input.destroy=[](void *){};
    input.get_width=[](void *)->uint32_t{return 1080;};input.get_height=[](void *)->uint32_t{return 1920;};
    input.video_render=[](void *,gs_effect_t *){
        auto *effect=obs_get_base_effect(OBS_EFFECT_SOLID);vec4 color;vec4_set(&color,1,1,1,1);
        gs_effect_set_vec4(gs_effect_get_param_by_name(effect,"color"),&color);
        while(gs_effect_loop(effect,"Solid"))gs_draw_sprite(nullptr,0,1080,1920);
    };
    obs_register_source(&input);
    obs_video_info info{};obs_get_video_info(&info);
    OBSCanvasAutoRelease canvas=obs_canvas_create("StreamHub Vertical fixture",&info,PROGRAM);
    const std::string canvasUuid=obs_canvas_get_uuid(canvas);
    check(StreamHubNormalizeVerticalCanvasName(canvas,"fixture"),"Managed canvas rename failed");
    check(std::string(obs_canvas_get_name(canvas))=="StreamHub","Canvas display name is incorrect");
    check(std::string(obs_canvas_get_uuid(canvas))==canvasUuid,"Canvas rename changed identity");
    check(StreamHubNormalizeVerticalCanvasName(canvas,"fixture"),"Canvas rename is not idempotent");
    obs_canvas_set_name(canvas,"External canvas");
    check(!StreamHubNormalizeVerticalCanvasName(canvas,"fixture"),"External canvas was treated as managed");
    check(std::string(obs_canvas_get_name(canvas))=="External canvas","External canvas was renamed");
    OBSSceneAutoRelease scene=obs_canvas_scene_create(canvas,"Live overlay scene");
    obs_canvas_set_channel(canvas,0,obs_scene_get_source(scene));
    auto *video=obs_canvas_get_video(canvas);ProbeOutput context;activeProbe=&context;
    obs_output_info outputInfo{};outputInfo.id="streamhub-test-canvas-output";outputInfo.flags=OBS_OUTPUT_VIDEO;
    outputInfo.get_name=[](void *){return "Canvas frame probe";};
    outputInfo.create=[](obs_data_t *,obs_output_t *output)->void *{activeProbe->output=output;return activeProbe;};
    outputInfo.destroy=[](void *){};
    outputInfo.start=[](void *p){return obs_output_begin_data_capture(static_cast<ProbeOutput *>(p)->output,0);};
    outputInfo.stop=[](void *p,uint64_t){obs_output_end_data_capture(static_cast<ProbeOutput *>(p)->output);};
    outputInfo.raw_video=[](void *p,video_data *frame){probeFrame(&static_cast<ProbeOutput *>(p)->probe,frame);};
    obs_register_output(&outputInfo);
    OBSOutputAutoRelease output=obs_output_create(outputInfo.id,"Canvas output fixture",nullptr,nullptr);
    obs_output_set_media(output,video,nullptr);
    video_scale_info conversion{};conversion.format=VIDEO_FORMAT_NV12;conversion.width=1080;conversion.height=1920;
    conversion.colorspace=VIDEO_CS_709;conversion.range=VIDEO_RANGE_PARTIAL;obs_output_set_video_conversion(output,&conversion);
    check(obs_output_start(output),"Cannot start canvas frame output");
    auto &probe=context.probe;
    bool black=false,white=false,moved=false;
    black=awaitFrame([&](){return probe.top>=0&&probe.top<40;});
    OBSSourceAutoRelease overlay=obs_source_create(input.id,"Live overlay input",nullptr,nullptr);
    auto *item=obs_scene_add(scene,overlay);
    white=awaitFrame([&](){return probe.top>200&&probe.inset>200;});
    vec2 position;vec2_set(&position,200,200);obs_sceneitem_set_pos(item,&position);
    moved=awaitFrame([&](){return probe.top>=0&&probe.top<40&&probe.inset>200;});
    obs_output_stop(output);
    check(awaitFrame([&](){return !obs_output_active(output);}),"Canvas probe did not stop");
    std::cerr<<"Overlay frames: black="<<black<<" white="<<white<<" moved="<<moved<<" top="<<probe.top.load()<<" inset="<<probe.inset.load()<<" stride="<<probe.stride.load()<<"\n";
    obs_canvas_set_channel(canvas,0,nullptr);obs_canvas_scene_remove(scene);
    check(black&&white&&moved,"An overlay added/moved while canvas video is active did not reach output frames");
}

extern "C" void obs_frontend_save() {}
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static size_t count(obs_scene_t *scene){size_t n=0;obs_scene_enum_items(scene,[](obs_scene_t *,obs_sceneitem_t *,void *p){++*static_cast<size_t *>(p);return true;},&n);return n;}
static obs_sceneitem_t *first(obs_scene_t *scene){obs_sceneitem_t *found=nullptr;obs_scene_enum_items(scene,[](obs_scene_t *,obs_sceneitem_t *item,void *p){*static_cast<obs_sceneitem_t **>(p)=item;return false;},&found);return found;}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    if(!obs_startup("en-US",nullptr,nullptr)){std::cerr<<"OBS startup failed\n";return 1;}
    int result=0;
    try {
        if(argc>2) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
            const auto dataPath=QByteArray(argv[2])+"/";
            obs_add_data_path(dataPath.constData());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
            obs_video_info video{};video.graphics_module=argv[1];video.fps_num=30;video.fps_den=1;
            video.base_width=video.output_width=1080;video.base_height=video.output_height=1920;
            video.output_format=VIDEO_FORMAT_NV12;video.colorspace=VIDEO_CS_709;video.range=VIDEO_RANGE_PARTIAL;video.gpu_conversion=true;
            check(obs_reset_video(&video)==OBS_VIDEO_SUCCESS,"OBS graphics initialization failed");
        }
        OBSDataAutoRelease host=obs_get_private_data();obs_data_set_bool(host,"AbsoluteCoordinates",true);
        if(argc>2)testLiveOverlay();
        OBSSceneAutoRelease root=obs_scene_create("Editor test");
        OBSSceneAutoRelease input=obs_scene_create("Input fixture");
        auto *item=obs_scene_add(root,obs_scene_get_source(input));
        const auto key=StreamHubEditorKey(item);
        check(StreamHubFindEditorItem(root,key)==item,"Item lookup failed");
        check(StreamHubNudgeItem(root,item,QPointF(1,0)),"1px nudge rejected");
        vec2 nudged;obs_sceneitem_get_pos(item,&nudged);check(nudged.x==1&&nudged.y==0,"Arrow did not move exactly 1px");
        check(StreamHubNudgeItem(root,item,QPointF(0,10)),"Shift nudge rejected");
        obs_sceneitem_get_pos(item,&nudged);check(nudged.x==1&&nudged.y==10,"Shift arrow did not move exactly 10px");
        vec2_set(&nudged,0,0);obs_sceneitem_set_pos(item,&nudged);
        const auto before=StreamHubSceneState(root);
        vec2 position;vec2_set(&position,140,220);obs_sceneitem_set_pos(item,&position);
        obs_sceneitem_set_locked(item,true);
        const auto edited=StreamHubSceneState(root);
        StreamHubRestoreSceneState(before.constData());
        item=first(root);obs_sceneitem_get_pos(item,&position);
        check(!obs_sceneitem_locked(item)&&position.x==0&&position.y==0,"Undo transform/lock failed");
        StreamHubRestoreSceneState(edited.constData());
        item=first(root);obs_sceneitem_get_pos(item,&position);
        check(obs_sceneitem_locked(item)&&position.x==140&&position.y==220,"Redo transform/lock failed");
        obs_sceneitem_remove(item);check(count(root)==0,"Removal failed");
        StreamHubRestoreSceneState(edited.constData());
        check(count(root)==1,"Undo removal failed");
        item=first(root);obs_sceneitem_set_locked(item,false);
        auto *second=obs_scene_add(root,obs_scene_get_source(input));
        obs_sceneitem_t *members[]={item,second};
        auto *group=obs_scene_insert_group(root,"Test group",members,2);
        check(group&&count(root)==1&&count(obs_sceneitem_group_get_scene(group))==2,"Grouping failed");
        check(StreamHubFindEditorItem(root,StreamHubEditorKey(second))==second,"Nested lookup failed");
        obs_sceneitem_set_locked(group,true);
        check(!StreamHubEditable(root,second),"Parent lock not respected");
        obs_sceneitem_set_locked(group,false);
        check(StreamHubEditable(root,second),"Child not editable after unlock");
        obs_sceneitem_select(group,true);check(StreamHubSelectedAncestor(root,second),"Selected ancestor not detected");
        const auto grouped=StreamHubSceneState(root);
        obs_sceneitem_group_ungroup(group);check(count(root)==2,"Ungroup failed");
        StreamHubRestoreSceneState(grouped.constData());
        check(count(root)==1&&obs_sceneitem_is_group(first(root)),"Undo grouping failed");
        check(count(obs_sceneitem_group_get_scene(first(root)))==2,"Group children not restored");
        const auto privateBefore=StreamHubSceneState(root);
        { OBSDataAutoRelease settings=obs_source_get_private_settings(obs_scene_get_source(root));obs_data_set_bool(settings,"streamhub-vertical-hidden",true); }
        obs_source_set_name(obs_scene_get_source(root),"Renamed fixture");
        StreamHubRestoreSceneState(privateBefore.constData());
        check(QString::fromUtf8(obs_source_get_name(obs_scene_get_source(root)))=="Editor test","Undo rename failed");
        {OBSDataAutoRelease settings=obs_source_get_private_settings(obs_scene_get_source(root));check(!obs_data_get_bool(settings,"streamhub-vertical-hidden"),"Undo hidden state failed");}
        item=obs_scene_add(root,obs_scene_get_source(input));
        obs_transform_info transform{};transform.pos.x=100;transform.pos.y=200;transform.scale.x=-2;transform.scale.y=3;
        transform.rot=37;transform.alignment=OBS_ALIGN_RIGHT|OBS_ALIGN_BOTTOM;transform.bounds_type=OBS_BOUNDS_SCALE_INNER;transform.bounds.x=450;transform.bounds.y=700;
        obs_sceneitem_set_info2(item,&transform);obs_sceneitem_crop crop{10,20,30,40};obs_sceneitem_set_crop(item,&crop);
        OBSDataArrayAutoRelease clipboard=obs_data_array_create();obs_sceneitem_save(item,clipboard);
        OBSDataAutoRelease saved=obs_data_array_item(clipboard,0);
        auto *pasted=obs_scene_add(root,obs_scene_get_source(input));StreamHubApplySavedItemTransform(pasted,saved);
        obs_transform_info actual{};obs_sceneitem_get_info2(pasted,&actual);obs_sceneitem_get_crop(pasted,&crop);
        check(actual.pos.x==100&&actual.pos.y==200&&actual.scale.x==-2&&actual.rot==37&&actual.bounds.x==450&&crop.right==30,"Clipboard transform lost bounds/crop/mirror/rotation");
        OBSSourceAutoRelease duplicate=StreamHubDuplicateSource(obs_scene_get_source(input),"Independent fixture");
        check(duplicate&&duplicate.Get()!=obs_scene_get_source(input),"Independent duplicate failed");
        check(QString::fromUtf8(obs_source_get_uuid(duplicate))!=QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(input))),"Duplicate reused source identity");
        OBSCanvasAutoRelease canvas=obs_canvas_create("History fixture",nullptr,SCENE_REF);
        OBSSceneAutoRelease vertical=obs_canvas_scene_create(canvas,"Vertical fixture");
        obs_scene_add(vertical,obs_scene_get_source(input));obs_canvas_set_channel(canvas,0,obs_scene_get_source(vertical));
        const auto canvasBefore=StreamHubCanvasState(canvas);
        const auto verticalUuid=QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(vertical)));
        obs_canvas_scene_remove(vertical);vertical=nullptr;obs_wait_for_destroy_queue();
        StreamHubRestoreCanvasState(canvasBefore.constData());
        OBSSourceAutoRelease restored=obs_get_source_by_uuid(verticalUuid.toUtf8().constData());
        check(restored&&count(obs_scene_from_source(restored))==1,"Undo deleted canvas scene failed");
        OBSSceneAutoRelease extra=obs_canvas_scene_create(canvas,"Extra fixture");
        StreamHubRestoreCanvasState(canvasBefore.constData());
        OBSSourceAutoRelease absent=obs_canvas_get_source_by_name(canvas,"Extra fixture");
        check(!absent,"Undo added canvas scene failed");
        obs_canvas_set_channel(canvas,0,nullptr);
        // Detach fixture scenes before releasing the host, matching frontend shutdown order.
        obs_canvas_scene_remove(obs_scene_from_source(restored));
        obs_canvas_scene_remove(obs_scene_from_source(duplicate));
        obs_canvas_scene_remove(root);obs_canvas_scene_remove(input);
        std::cout<<"PASS: history, removal, groups, parent locks, hierarchy and independent identity\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<"\n";result=1;}
    obs_wait_for_destroy_queue();obs_shutdown();return result;
}
