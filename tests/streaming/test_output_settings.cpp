#include "streamhub-output-settings.h"
#include "output-target-config.h"
#include <stdexcept>
#include <iostream>
#include <string>

// Deterministic host double: no OBS session, network, or real credentials are used.
static std::string fixtureCanvasName = "StreamHub Vertical vertical-fixture";
extern "C" {
void obs_enum_canvases(bool (*callback)(void *, obs_canvas_t *), void *data) { callback(data, reinterpret_cast<obs_canvas_t *>(1)); }
bool obs_canvas_removed(obs_canvas_t *) { return false; }
bool obs_canvas_get_video_info(const obs_canvas_t *, obs_video_info *info) { info->base_width = info->output_width = 1080; info->base_height = info->output_height = 1920; return true; }
const char *obs_canvas_get_name(const obs_canvas_t *) { return fixtureCanvasName.c_str(); }
void obs_canvas_set_name(obs_canvas_t *, const char *name) { fixtureCanvasName = name; }
const char *obs_canvas_get_uuid(const obs_canvas_t *) { return "canvas-fixture"; }
obs_canvas_t *obs_get_canvas_by_uuid(const char *) { return reinterpret_cast<obs_canvas_t *>(1); }
void obs_canvas_release(obs_canvas_t *) {}
void obs_frontend_get_scenes(obs_frontend_source_list *list) { *list = {}; }
void obs_frontend_save() {}
bool obs_get_video_info(obs_video_info *info) { *info={}; info->base_width=info->output_width=1920; info->base_height=info->output_height=1080; info->fps_num=60; info->fps_den=1; return true; }
obs_canvas_t *obs_frontend_add_canvas(const char *name, obs_video_info *, int) {
    fixtureCanvasName = name;
    return reinterpret_cast<obs_canvas_t *>(1);
}
obs_output_t *obs_get_output_by_name(const char *) { return nullptr; }
bool obs_output_active(const obs_output_t *) { return false; }
void obs_output_release(obs_output_t *) {}
}

MultiOutputConfig &GlobalMultiOutputConfig() { static MultiOutputConfig config; return config; }
static nlohmann::json saved;
static bool saveWorks = true;
bool SaveMultiOutputConfig() {
    if (!saveWorks) return false;
    saved = nlohmann::json::array();
    for (const auto &target : GlobalMultiOutputConfig().targets) saved.push_back(SaveTarget(*target));
    return true;
}
std::string GenerateId(MultiOutputConfig &) { return "vertical-fixture"; }

class FakePushWidget : public PushWidget {
public:
    bool running = false;
    bool ShowEditDlg() override { return false; }
    void StartStreaming() override {}
    void StopStreaming() override {}
    void OnOBSEvent(obs_frontend_event) override {}
    QPushButton *GetDeleteButton() override { return nullptr; }
    QPushButton *GetEditButton() override { return nullptr; }
    QPushButton *GetStreamButton() override { return nullptr; }
    void ReloadConfig() override {}
    bool IsEnabledForAll() const override { return false; }
    bool IsRunningForAggregate() const override { return running; }
    double CurrentBitrateBps() const override { return 0; }
};

static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> static T *field(QWidget &widget, const char *name) {
    auto *result = widget.findChild<T *>(name);
    if(!result) throw std::runtime_error(std::string("Missing settings control: ")+name);
    return result;
}

static void dismissWarning() {
    QTimer::singleShot(0, []() {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget)) message->accept();
    });
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    try {
        auto horizontal = std::make_shared<OutputTargetConfig>();
        horizontal->id = "horizontal-fixture";
        StreamHubApplyPlatformPreset(*horizontal, "youtube");
        horizontal->serviceParam["key"] = "horizontal-fixture-key";
        horizontal->enabled = false;
        GlobalMultiOutputConfig().targets.push_back(horizontal);
        FakePushWidget push;
        int savedCallbacks = 0;
        {
            StreamHubInlineSettings panel(horizontal->id, &push, [&] { ++savedCallbacks; }, {}, {});
            panel.setAttribute(Qt::WA_DontShowOnScreen);
            panel.resize(560, 900);
            panel.show();
            app.processEvents();
            check(field<QLabel>(panel, "outputServerLabel")->text().contains("horizontal"), "Horizontal endpoint is not identified");
            field<QLineEdit>(panel, "youtubeVerticalServer")->setText("rtmps://fixture.example/live2");
            field<QLineEdit>(panel, "youtubeVerticalKey")->setText("vertical-fixture-key");
            field<QCheckBox>(panel, "youtubeVerticalEnabled")->setChecked(true);
            field<QCheckBox>(panel, "youtubeVerticalStartWithHorizontal")->setChecked(false);
            field<QSpinBox>(panel, "outputVideoBitrate")->setValue(6000);
            field<QSpinBox>(panel, "youtubeVerticalBitrate")->setValue(4500);
            field<QPushButton>(panel, "inlinePrimary")->click();
            check(savedCallbacks == 1 && saved.size() == 2, "Saving dual output failed");
            check(!horizontal->enabled, "Saving settings changed a disabled horizontal output");
            auto vertical = FindById(GlobalMultiOutputConfig().targets, std::string("vertical-fixture"));
            check(vertical && vertical->enabled && vertical->orientation == "vertical", "Vertical output was not created");
            check(!vertical->startWithParent,"Independent start preference was not saved");
            check(vertical->canvasUuid == "canvas-fixture" && vertical->manualCredentials, "Vertical source or manual key lost");
            check(fixtureCanvasName == "StreamHub", "New canvas did not use the clean display name");
            check(vertical->serviceParam["key"] != horizontal->serviceParam["key"], "Keys were mixed");
            check(horizontal->videoBitrateKbps == 6000 && vertical->videoBitrateKbps == 4500, "Independent bitrates were lost");
            field<QLineEdit>(panel, "youtubeVerticalKey")->setText("horizontal-fixture-key");
            dismissWarning();
            field<QPushButton>(panel, "inlinePrimary")->click();
            check(savedCallbacks == 1 && vertical->serviceParam["key"] == "vertical-fixture-key", "Duplicate key was accepted");
            field<QLineEdit>(panel, "youtubeVerticalKey")->setText("vertical-fixture-key");
            field<QLineEdit>(panel, "outputServerInput")->setText("https://invalid.example");
            dismissWarning();
            field<QPushButton>(panel, "inlinePrimary")->click();
            check(savedCallbacks == 1, "HTTPS server was accepted");
            panel.Reload();
            push.running = true;
            dismissWarning();
            field<QPushButton>(panel, "inlinePrimary")->click();
            check(savedCallbacks == 1, "Editing a running output was accepted");
            push.running = false;
            saveWorks = false;
            fixtureCanvasName = "StreamHub Vertical vertical-fixture";
            field<QLineEdit>(panel, "outputKeyInput")->setText("unsaved-fixture-key");
            dismissWarning();
            field<QPushButton>(panel, "inlinePrimary")->click();
            check(savedCallbacks == 1, "A failed disk save reported success");
            check(fixtureCanvasName == "StreamHub", "Legacy canvas was not renamed through the host double");
            check(horizontal->serviceParam["key"] == "horizontal-fixture-key", "Failed save changed the live configuration in memory");
        }
        GlobalMultiOutputConfig().targets.clear();
        for (auto item : saved) GlobalMultiOutputConfig().targets.push_back(LoadTargetConfig(item));
        StreamHubInlineSettings reopened("horizontal-fixture", &push, {}, {}, {});
        check(field<QLineEdit>(reopened, "youtubeVerticalKey")->text() == "vertical-fixture-key", "Vertical key did not survive reload");
        check(field<QCheckBox>(reopened, "youtubeVerticalEnabled")->isChecked(), "Enabled state did not survive reload");
        check(!field<QCheckBox>(reopened,"youtubeVerticalStartWithHorizontal")->isChecked(),"Linked start preference did not survive reload");
        check(field<QSpinBox>(reopened, "outputVideoBitrate")->value() == 6000 &&
              field<QSpinBox>(reopened, "youtubeVerticalBitrate")->value() == 4500, "Bitrates did not survive reload");
        std::cout << "Dual-output settings, validation, error handling and reload passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
