#include "pch.h"
#include "helpers.h"
#include <regex>
#include <optional>
#include <tuple>
#include "push-widget.h"
#include "edit-widget.h"
#include "output-config.h"
#include "output-lifecycle.h"
#include "protocols.h"
#include "streamhub-platforms.h"

#include "obs.hpp"
#include <util/platform.h>
#include <QPainter>
#include <QUrl>
#include <atomic>

class IOBSOutputEventHanlder
{
public:
    virtual void OnStarting() {}
    static void OnOutputStarting(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStarting();
    }

    virtual void OnStarted() {}
    static void OnOutputStarted(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStarted();
    }

    virtual void OnStopping() {}
    static void OnOutputStopping(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStopping();
    }
   
    virtual void OnStopped(int) {}
    static void OnOutputStopped(void* x, calldata_t* param)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnStopped(calldata_int(param, "code"));
    }

    virtual void OnReconnect() {}
    static void OnOutputReconnect(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnReconnect();
    }

    virtual void OnReconnected() {}
    static void OnOutputReconnected(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->OnReconnected();
    }

    virtual void onDeactive() {}
    static void OnOutputDeactive(void* x, calldata_t*)
    {
        auto thiz = static_cast<IOBSOutputEventHanlder*>(x);
        thiz->onDeactive();
    }

    void SetMeAsHandler(obs_output_t* output)
    {
        auto outputSignal = obs_output_get_signal_handler(output);
        if (outputSignal)
        {
            signal_handler_connect(outputSignal, "starting", &IOBSOutputEventHanlder::OnOutputStarting, this);
            signal_handler_connect(outputSignal, "start", &IOBSOutputEventHanlder::OnOutputStarted, this);
            signal_handler_connect(outputSignal, "reconnect", &IOBSOutputEventHanlder::OnOutputReconnect, this);
            signal_handler_connect(outputSignal, "reconnect_success", &IOBSOutputEventHanlder::OnOutputReconnected, this);
            signal_handler_connect(outputSignal, "stopping", &IOBSOutputEventHanlder::OnOutputStopping, this);
            signal_handler_connect(outputSignal, "deactivate", &IOBSOutputEventHanlder::OnOutputDeactive, this);
            signal_handler_connect(outputSignal, "stop", &IOBSOutputEventHanlder::OnOutputStopped, this);
        }
    }

    void DisconnectSignals(obs_output_t* output)
    {
        auto outputSignal = obs_output_get_signal_handler(output);
        if (outputSignal)
        {
            signal_handler_disconnect(outputSignal, "starting", &IOBSOutputEventHanlder::OnOutputStarting, this);
            signal_handler_disconnect(outputSignal, "start", &IOBSOutputEventHanlder::OnOutputStarted, this);
            signal_handler_disconnect(outputSignal, "reconnect", &IOBSOutputEventHanlder::OnOutputReconnect, this);
            signal_handler_disconnect(outputSignal, "reconnect_success", &IOBSOutputEventHanlder::OnOutputReconnected, this);
            signal_handler_disconnect(outputSignal, "stopping", &IOBSOutputEventHanlder::OnOutputStopping, this);
            signal_handler_disconnect(outputSignal, "deactivate", &IOBSOutputEventHanlder::OnOutputDeactive, this);
            signal_handler_disconnect(outputSignal, "stop", &IOBSOutputEventHanlder::OnOutputStopped, this);
        }
    }
};


class StreamToggleButton : public QPushButton
{
public:
    explicit StreamToggleButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(50, 28);
        setToolTip(QObject::tr("Incluir este destino em Iniciar tudo (preferência salva)"));
    }

    void SetAccentColor(const QColor &color)
    {
        accent_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor track = !isEnabled() ? QColor("#253348")
                             : isChecked() ? accent_ : QColor("#3A4A62");
        painter.setPen(Qt::NoPen);
        painter.setBrush(track);
        painter.drawRoundedRect(rect().adjusted(1, 3, -1, -3), 11, 11);
        painter.setBrush(QColor("#F2F7FF"));
        painter.drawEllipse(QRect(isChecked() ? width() - 23 : 5, 6, 17, 17));
    }

private:
    QColor accent_ = QColor("#16D86A");
};

class PushWidgetImpl : public PushWidget, public IOBSOutputEventHanlder
{
    std::string targetid_;
    OutputTargetConfigPtr config_;

    QPushButton* btn_ = 0;
    QPushButton* stream_btn_ = 0;
    QLabel* name_ = 0;
    QLabel* msg_ = 0;
    QLabel* icon_ = 0;

    using clock = std::chrono::steady_clock;
    clock::time_point begin_time_;
    clock::time_point last_info_time_;
    uint64_t total_frames_ = 0;
    uint64_t total_bytes_ = 0;
    double current_bps_ = 0.0;
    QTimer* timer_ = 0;

    QPushButton* edit_btn_ = 0;
    QPushButton* remove_btn_ = 0;

    obs_output_t* output_ = 0;
    bool using_main_video_encoder_ = false;
    bool using_main_audio_encoder_ = false;
    obs_view_t* scene_view_ = 0;
    OutputLifecycle lifecycle_;
    bool forceStopAvailable_ = false;
    std::function<void()> streamAction_;
    QLabel *linkedStatus_ = nullptr;
    bool linkedBusy_ = false;
    obs_canvas_t *canvas_ = nullptr;
    std::atomic<uint64_t> outputGeneration_{0};

    void InUi(std::function<void()> callback)
    {
        const auto generation = outputGeneration_.load();
        QMetaObject::invokeMethod(this, [this, generation, callback = std::move(callback)]() {
            if (generation == outputGeneration_.load()) callback();
        }, Qt::QueuedConnection);
    }

    QPushButton* GetDeleteButton() {
        return remove_btn_;
    }

    QPushButton* GetEditButton() override {
        return edit_btn_;
    }

    bool HasStreamConfig() const
    {
        if (!config_ || !config_->serviceParam.is_object()) return false;
        const QUrl server(QString::fromStdString(config_->serviceParam.value("server", std::string{})).trimmed());
        return server.isValid() && !server.host().isEmpty() &&
               (server.scheme().compare("rtmp", Qt::CaseInsensitive) == 0 || server.scheme().compare("rtmps", Qt::CaseInsensitive) == 0) &&
               !QString::fromStdString(config_->serviceParam.value("key", std::string{})).trimmed().isEmpty() &&
               (config_->orientation != "vertical" || !config_->canvasUuid.empty());
    }

    bool PrepareOutputService()
    {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output service before output object is created.");
            return false;
        }
        
        ReleaseOutputService();
        
        auto conf = obs_data_create_from_json(config_->serviceParam.dump().c_str());

        auto protocolInfo = GetProtocolInfos()->GetInfo(config_->protocol.c_str());
        assert(protocolInfo);
        if (!protocolInfo) {
        	blog(LOG_ERROR, TAG "Invalid protocol \"%s\", maybe broken config file.", config_->protocol.c_str());
        	return false;
        }
        auto service_id = protocolInfo->serviceId;

        if (!conf)
            return false;
        
        auto service = obs_service_create(service_id, "multi-output-service", conf, nullptr);
        obs_data_release(conf);
        if (!service)
            return false;
        obs_output_set_service(output_, service);

        return true;
    }


    bool ReleaseOutputService()
    {
        if (!output_)
            return true;

        if (obs_output_active(output_)) {
            return false;
        }
        
        auto service = obs_output_get_service(output_);
        if (service)
        {
            obs_output_set_service(output_, nullptr);
            obs_service_release(service);
        }
        return true;
    }


    bool PrepareEncoderSource() {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output scene before output object is created.");
            return false;
        }

        if (!using_main_video_encoder_) {
            auto venc = obs_output_get_video_encoder(output_);
            if (!venc) {
                blog(LOG_ERROR, TAG "Prepare output scene before encoder is created.");
                return false;
            }
            auto videoConfig = FindById(GlobalMultiOutputConfig().videoConfig, config_->videoConfig.value_or(""));
            if (config_->orientation == "vertical") {
                canvas_ = obs_get_canvas_by_uuid(config_->canvasUuid.c_str());
                obs_video_info info{};
                if (!canvas_ || obs_canvas_removed(canvas_) ||
                    !obs_canvas_get_video_info(canvas_, &info) || info.output_height <= info.output_width ||
                    !obs_canvas_get_video(canvas_)) {
                    if (canvas_) obs_canvas_release(canvas_);
                    canvas_ = nullptr;
                    return false;
                }
                obs_encoder_set_scaled_size(venc, info.output_width, info.output_height);
                obs_encoder_set_video(venc, obs_canvas_get_video(canvas_));
                OBSSourceAutoRelease active=obs_canvas_get_channel(canvas_,0);
                blog(LOG_INFO,"[streamhub-output] vertical canvas bound: output=%s canvas=%s scene=%s size=%ux%u",targetid_.c_str(),obs_canvas_get_uuid(canvas_),active?obs_source_get_uuid(active):"none",info.output_width,info.output_height);
            } else if (!videoConfig || !videoConfig->outputScene.has_value()) {
                if (config_->copiedEncoding.is_object()) {
                    const auto width = config_->copiedEncoding.value("width", 0u);
                    const auto height = config_->copiedEncoding.value("height", 0u);
                    if (width > 0 && height > 0 && width <= 16384 && height <= 16384)
                        obs_encoder_set_scaled_size(venc, width, height);
                }
                obs_encoder_set_video(venc, obs_get_video());
            } else {
                auto sceneName = *videoConfig->outputScene;
                OBSSourceAutoRelease scene = obs_get_source_by_name(sceneName.c_str());
                if (scene == nullptr) {
                    blog(LOG_ERROR, TAG "Output scene is not found.");
                    return false;
                }
                ReleaseOutputSceneView();

                scene_view_ = obs_view_create();
                obs_view_set_source(scene_view_, 0, scene);
                obs_source_inc_active(scene);
                auto scene_video = obs_view_add(scene_view_);
                obs_encoder_set_video(venc, scene_video);
            }
        }

        if (!using_main_video_encoder_ && config_->copiedEncoding.is_object()) {
            obs_video_info info{};
            const bool available = canvas_ ? obs_canvas_get_video_info(canvas_, &info) : obs_get_video_info(&info);
            const uint64_t numerator = config_->copiedEncoding.value("fpsNum", 0u);
            const uint64_t denominator = config_->copiedEncoding.value("fpsDen", 0u);
            const uint64_t sourceRate = uint64_t(info.fps_num) * denominator;
            const uint64_t targetRate = numerator * info.fps_den;
            if (!available || !targetRate || sourceRate < targetRate || sourceRate % targetRate != 0 || sourceRate / targetRate > 60) {
                SetMsg(tr("FPS copiado incompatível com a área atual. Copie novamente as configurações da principal."));
                return false;
            }
            obs_encoder_set_frame_rate_divisor(obs_output_get_video_encoder(output_), uint32_t(sourceRate / targetRate));
        }
        if (!using_main_audio_encoder_) {
            auto aenc = obs_output_get_audio_encoder(output_, 0);
            if (!aenc) {
                blog(LOG_ERROR, TAG "Prepare output scene before encoder is created.");
                return false;
            }
            obs_encoder_set_audio(aenc, obs_get_audio());

            // Set other audio tracks
            auto audioConfig = FindById(GlobalMultiOutputConfig().audioConfig, config_->audioConfig.value_or(""));
            if (audioConfig) {
                for (auto& track : audioConfig->audioTracks) {
                    auto enc = obs_output_get_audio_encoder(output_, track->output_track);
                    if (enc) {
                        obs_encoder_set_audio(enc, obs_get_audio());
                    }
                }
            }
        }
        
        return true;
    }


    bool ReleaseOutputSceneView() {
        if (canvas_) {
            obs_canvas_release(canvas_);
            canvas_ = nullptr;
        }
        if (!scene_view_)
            return true;

        obs_view_remove(scene_view_);
        OBSSourceAutoRelease source = obs_view_get_source(scene_view_, 0);
        if (source) {
            obs_source_dec_active(source);
        }
        obs_view_set_source(scene_view_, 0, nullptr);
        obs_view_destroy(scene_view_);
        scene_view_ = nullptr;

        return true;
    }


    std::string VideoEncoderName() {
        return "multi-rtmp-venc-" + targetid_;
    }

    std::string AudioEncoderName(int track) {
        return "multi-rtmp-aenc-" + targetid_ + "-track-idx-" + std::to_string(track);
    }

    OBSEncoderAutoRelease CreateProfileStreamVideoEncoder()
    {
        config_t *profile = obs_frontend_get_profile_config();
        const char *mode = profile ? config_get_string(profile, "Output", "Mode") : nullptr;
        if (!profile || !mode || std::strcmp(mode, "Advanced") != 0) {
            blog(LOG_ERROR, TAG "Cannot clone the OBS streaming encoder from a non-advanced profile.");
            return nullptr;
        }

        const char *encoderId = config_get_string(profile, "AdvOut", "Encoder");
        if (!encoderId || !*encoderId)
            return nullptr;

        obs_data_t *settings = obs_encoder_defaults(encoderId);
        if (!settings)
            return nullptr;

        char *profilePath = obs_frontend_get_current_profile_path();
        if (profilePath) {
            const std::string settingsPath = std::string(profilePath) + "/streamEncoder.json";
            bfree(profilePath);
            char *savedJson = os_quick_read_utf8_file(settingsPath.c_str());
            if (savedJson) {
                obs_data_t *savedSettings = obs_data_create_from_json(savedJson);
                if (savedSettings) {
                    obs_data_apply(settings, savedSettings);
                    obs_data_release(savedSettings);
                }
                bfree(savedJson);
            }
        }

        const std::string encoderName = "streamhub-profile-video-" + targetid_;
        std::string copiedId;
        if (config_->copiedEncoding.is_object() && config_->copiedEncoding.contains("video")) {
            copiedId = config_->copiedEncoding.value("videoId", std::string(encoderId));
            encoderId = copiedId.c_str();
            obs_data_release(settings);
            settings = obs_data_create_from_json(config_->copiedEncoding["video"].dump().c_str());
        }
        if (config_->videoBitrateKbps > 0) {
            obs_data_set_string(settings, "rate_control", "CBR");
            obs_data_set_int(settings, "bitrate", config_->videoBitrateKbps);
        }
        obs_encoder_t *encoder = obs_video_encoder_create(
            encoderId, encoderName.c_str(), settings, nullptr);
        obs_data_release(settings);
        return encoder;
    }

    OBSEncoderAutoRelease CreateProfileStreamAudioEncoder(int trackIndex)
    {
        config_t *profile = obs_frontend_get_profile_config();
        const char *mode = profile ? config_get_string(profile, "Output", "Mode") : nullptr;
        if (!profile || !mode || std::strcmp(mode, "Advanced") != 0)
            return nullptr;

        const char *encoderId = config_get_string(profile, "AdvOut", "AudioEncoder");
        if (!encoderId || !*encoderId)
            return nullptr;

        obs_data_t *settings = obs_encoder_defaults(encoderId);
        if (!settings)
            return nullptr;

        const int configuredTrack = std::max(
            0, static_cast<int>(config_get_int(profile, "AdvOut", "TrackIndex")) - 1);
        char bitrateKey[32] = {};
        std::snprintf(bitrateKey, sizeof(bitrateKey), "Track%dBitrate", configuredTrack + 1);
        const int bitrate = config_get_int(profile, "AdvOut", bitrateKey);
        if (bitrate > 0)
            obs_data_set_int(settings, "bitrate", bitrate);

        const int mixer = trackIndex == 0 ? configuredTrack : trackIndex;
        std::string copiedId;
        int selectedMixer = mixer;
        if (config_->copiedEncoding.is_object() && config_->copiedEncoding.contains("audio")) {
            copiedId = config_->copiedEncoding.value("audioId", std::string(encoderId));
            encoderId = copiedId.c_str();
            obs_data_release(settings);
            settings = obs_data_create_from_json(config_->copiedEncoding["audio"].dump().c_str());
            selectedMixer = config_->copiedEncoding.value("mixer", mixer);
        }
        const std::string encoderName = "streamhub-profile-audio-" + targetid_ + "-" + std::to_string(trackIndex);
        obs_encoder_t *encoder = obs_audio_encoder_create(
            encoderId, encoderName.c_str(), settings, selectedMixer, nullptr);
        obs_data_release(settings);
        return encoder;
    }

    std::optional<std::tuple<int, int>> ParseResolution(const std::optional<std::string>& res) {
        if (!res.has_value())
            return std::nullopt;
        std::regex res_pattern(R"__(\s*(\d{1,5})\s*x\s*(\d{1,5})\s*)__");
        std::smatch match;
        if (std::regex_match(*res, match, res_pattern))
        {
            auto width = std::stoi(match[1].str());
            auto height = std::stoi(match[2].str());
            return {{ width, height }};
        }

        return std::nullopt;
    }

    OBSEncoder GetVideoEncoder() {
        auto config_id = config_->videoConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        if ((config_->orientation == "vertical" || config_->videoBitrateKbps > 0) && config_id == OBS_RECORDING_ENC_PLACEHOLDER)
            config_id = OBS_STREAMING_ENC_PLACEHOLDER;
        if (config_id == "" || config_id == OBS_STREAMING_ENC_PLACEHOLDER) {
            auto clonedEncoder = CreateProfileStreamVideoEncoder();
            using_main_video_encoder_ = false;
            return clonedEncoder.Get();
        } else if (config_id == OBS_RECORDING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_recording_output();
            OBSEncoder enc = obs_output_get_video_encoder(stream_output);
            using_main_video_encoder_ = true;
            return enc.Get();
        } else {
            OBSEncoderAutoRelease enc = obs_get_encoder_by_name(VideoEncoderName().c_str());
            if (!enc) {
                auto& global = GlobalMultiOutputConfig();
                auto videoConfig = FindById(global.videoConfig, config_id);
                if (videoConfig) {
                    OBSDataAutoRelease settings = obs_data_create_from_json(videoConfig->encoderParams.dump().c_str());
                    if (config_->videoBitrateKbps > 0) {
                        obs_data_set_string(settings, "rate_control", "CBR");
                        obs_data_set_int(settings, "bitrate", config_->videoBitrateKbps);
                    }
                    enc = obs_video_encoder_create(videoConfig->encoderId.c_str(), VideoEncoderName().c_str(), settings, nullptr);
                    if (enc) {
                        auto wh = ParseResolution(videoConfig->resolution);
                        if (wh.has_value()) {
                            obs_encoder_set_gpu_scale_type(enc, obs_scale_type::OBS_SCALE_BICUBIC);
                            auto [w, h] = *wh;
                            obs_encoder_set_scaled_size(enc, w, h);
                        }
                        obs_encoder_set_frame_rate_divisor(enc, videoConfig->fpsDenumerator);
                    }
                } else {
                    assert(false && "No video encoder config found with specified id.");
                    blog(LOG_ERROR, TAG "Load video encoder config failed for %s. Sharing with main output.", config_->name.c_str());
                    config_->videoConfig = OBS_STREAMING_ENC_PLACEHOLDER;
                    return GetVideoEncoder();
                }
            }

            using_main_video_encoder_ = false;
            return enc.Get();
        }
    }

    OBSEncoder GetAudioEncoder(int trackIdx = 0, std::optional<int> mixerId = std::nullopt) {
        auto config_id = config_->audioConfig.value_or(OBS_STREAMING_ENC_PLACEHOLDER);
        if (config_id == "" || config_id == OBS_STREAMING_ENC_PLACEHOLDER) {
            auto clonedEncoder = CreateProfileStreamAudioEncoder(trackIdx);
            using_main_audio_encoder_ = false;
            return clonedEncoder.Get();
        } else if (config_id == OBS_RECORDING_ENC_PLACEHOLDER) {
            OBSOutputAutoRelease stream_output = obs_frontend_get_recording_output();
            OBSEncoder enc = obs_output_get_audio_encoder(stream_output, 0);
            using_main_audio_encoder_ = true;
            return enc.Get();
        } else {
            OBSEncoderAutoRelease enc = obs_get_encoder_by_name(AudioEncoderName(trackIdx).c_str());
            if (!enc) {
                auto& global = GlobalMultiOutputConfig();
                auto audioConfigId = *config_->audioConfig;
                auto audioConfig = FindById(global.audioConfig, audioConfigId);
                if (audioConfig) {
                    OBSDataAutoRelease settings = obs_data_create_from_json(audioConfig->encoderParams.dump().c_str());

                    // If we were provided with a mixerId, override the audioConfig's mixerId with it
                    int defaultMixerId = audioConfig->mixerId;
                    if (auto overrideMixerId = mixerId) {
                        defaultMixerId = *overrideMixerId;
                    }

                    enc = obs_audio_encoder_create(audioConfig->encoderId.c_str(), AudioEncoderName(trackIdx).c_str(), settings, defaultMixerId, nullptr);
                } else {
                    assert(false && "No audio encoder config found with specified id.");
                    blog(LOG_ERROR, TAG "Load audio encoder config failed for %s. Sharing with main output.", config_->name.c_str());
                    config_->audioConfig = OBS_STREAMING_ENC_PLACEHOLDER;
                    return GetAudioEncoder();
                }
            }
            
            using_main_audio_encoder_ = false;
            return enc.Get();
        }
    }

    bool PrepareOutputEncoders()
    {
        if (!output_) {
            blog(LOG_ERROR, TAG "Prepare output encoder before output object is created.");
            return false;
        }
        
        ReleaseOutputEncoder();

        auto& global = GlobalMultiOutputConfig();

        // main output
        OBSOutput mainOutput = obs_frontend_get_streaming_output();
        OBSOutput recordingOutput =  obs_frontend_get_recording_output();

        obs_output_release(mainOutput);
        obs_output_release(recordingOutput);

        OBSEncoder venc = GetVideoEncoder();
        OBSEncoder aenc = GetAudioEncoder();

        std::vector<std::tuple<int, OBSEncoder>> additionalTracks;
        if (auto audioConfigId = config_->audioConfig; audioConfigId &&
            *audioConfigId != OBS_STREAMING_ENC_PLACEHOLDER && *audioConfigId != OBS_RECORDING_ENC_PLACEHOLDER) {
            auto audioConfig = FindById(global.audioConfig, *audioConfigId);

            if (!audioConfig) {
                blog(LOG_ERROR, TAG "Load audio encoder config failed for %s. Could not determine additional tracks.", config_->name.c_str());
            } else {
                additionalTracks.reserve(audioConfig->audioTracks.size());
                for (auto& track : audioConfig->audioTracks) {
                    OBSEncoder enc = GetAudioEncoder(track->output_track, track->mixer_track);
                    if (enc) {
                        // Record the output track index and the encoder for later when we set the encoders on the output
                        additionalTracks.push_back({ track->output_track, enc });
                    }
                }
            }
        }

        if (!aenc || !venc) {
            // If we don't have a valid encoder, we're likely using a special encoder type that
            // needs to be started by the user (i.e. start streaming or start recording)
            ReleaseOutputEncoder();

            auto msgbox = new QMessageBox(QMessageBox::Icon::Critical, 
                obs_module_text("Notice.Title"), 
                obs_module_text("Notice.GetEncoder"),
                QMessageBox::StandardButton::Ok,
                this
                );
            msgbox->exec();
            return false;
        }

        obs_output_set_audio_encoder(output_, obs_encoder_get_ref(aenc), 0);
        for (auto& track : additionalTracks) {
            auto trackIdx = std::get<0>(track);
            auto enc = std::get<1>(track);
            obs_output_set_audio_encoder(output_, obs_encoder_get_ref(enc), trackIdx);
        }
        obs_output_set_video_encoder(output_, obs_encoder_get_ref(venc));

        return true;
    }


    bool ReleaseOutputEncoder()
    {
        if (!output_)
            return true;
        else if (obs_output_active(output_) == false)
        {
            auto venc = obs_output_get_video_encoder(output_);
            if (venc)
            {
                obs_output_set_video_encoder(output_, nullptr);
                obs_encoder_release(venc);
            }
            
            for (size_t track = 0; track < MAX_AUDIO_MIXES; ++track) {
                auto aenc = obs_output_get_audio_encoder(output_, track);
                if (aenc) {
                    obs_output_set_audio_encoder(output_, nullptr, track);
                    obs_encoder_release(aenc);
                }
            }

            return true;
        }
        else {
            blog(LOG_ERROR, TAG "Release output while it is active.");
            return false;
        }
    }


    bool ReleaseOutput()
    {
        if (output_ && obs_output_active(output_)) {
            // A forced stop completes asynchronously. Keep the output and its
            // signal handlers alive until OBS emits "stop"; dropping the last
            // plugin reference here can leave callbacks or encoders dangling.
            obs_output_force_stop(output_);
            return false;
        }

        ++outputGeneration_;
        if (output_) {
            DisconnectSignals(output_);
        }

        if (output_)
        {
            bool ret = ReleaseOutputService();
            ret = ReleaseOutputEncoder() && ret;

            obs_output_release(output_);
            output_ = nullptr;

            ret = ReleaseOutputSceneView() && ret;

            return ret;
        }
        return ReleaseOutputSceneView();
    }


    void UpdateStreamStatus() {
        using namespace std::chrono;

        if (stream_btn_) {
            const bool running = IsRunning() || linkedBusy_;
            const bool stopping = lifecycle_.phase == OutputLifecycle::Phase::Stopping;
            stream_btn_->setIcon(QIcon(running ? ":/streamhub-ui/icons/stop.svg"
                                                : ":/streamhub-ui/icons/play.svg"));
            stream_btn_->setToolTip(running ? tr("Parar somente esta saída")
                                            : tr("Iniciar somente esta saída"));
            stream_btn_->setAccessibleName(stream_btn_->toolTip());
            if(streamAction_&&linkedStatus_&&!linkedStatus_->text().isEmpty()) {
                stream_btn_->setToolTip(running?tr("Parar horizontal e vertical vinculada"):tr("Iniciar horizontal e vertical vinculada habilitada"));
                stream_btn_->setAccessibleName(stream_btn_->toolTip());
            }
            stream_btn_->setEnabled(!stopping || forceStopAvailable_);
            if (stopping)
                stream_btn_->setToolTip(forceStopAvailable_ ? tr("Forçar parada desta saída") : tr("Encerrando transmissão..."));
            edit_btn_->setEnabled(!running);
        }

        if (!output_ || lifecycle_.phase != OutputLifecycle::Phase::Live)
            return;

        static const char* units[] = {
            "bps", "Kbps", "Mbps", "Gbps", "Tbps", "Pbps", "Ebps", "Zbps", "Ybps"
        };

        auto new_bytes = obs_output_get_total_bytes(output_);
        auto new_frames = obs_output_get_total_frames(output_);
        auto now = clock::now();

        auto interval = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_info_time_).count();
        if (interval > 0)
        {
            auto duration = now - begin_time_;
            auto hh = duration_cast<hours>(duration);
            duration -= hh;
            auto mm = duration_cast<minutes>(duration);
            duration -= mm;
            auto ss = duration_cast<seconds>(duration);
            duration -= ss;

            char strDuration[64] = { 0 };
            snprintf(strDuration, sizeof(strDuration), "%02d:%02d:%02d", (int)hh.count(), (int)mm.count(), (int)ss.count());

            char strFps[32] = { 0 };
            snprintf(strFps, sizeof(strFps), "%d FPS", static_cast<int>(std::round((new_frames - total_frames_) / interval)));

            auto bps = (new_bytes - total_bytes_) * 8 / interval;
            current_bps_ = bps;
            auto strBps = [&]()-> std::string {
                if (bps > 0)
                {
                    int unitMaxIndex = sizeof(units) / sizeof(*units);
                    int unitIndex = static_cast<int>(log10(bps) / 3);
                    if (unitIndex >= unitMaxIndex)
                        unitIndex = unitMaxIndex - 1;
                    auto strVal = std::to_string(bps / pow(1000, unitIndex)).substr(0, 4);
                    if (!strVal.empty() && strVal.back() == '.')
                        strVal.pop_back();
                    return strVal + " " + units[unitIndex];
                }
                else
                {
                    return "0 bps";
                }
            }();
            
            msg_->setText((std::string(strDuration) + "  " + strBps + "  " + strFps).c_str());
        }

        total_frames_ = new_frames;
        total_bytes_ = new_bytes;
        last_info_time_ = now;
    }

public:
    PushWidgetImpl(const std::string& targetid, QWidget* parent = 0)
        : QWidget(parent)
        , targetid_(targetid)
    {
        QObject::setObjectName("outputCard");
        setMinimumHeight(64);

        auto& global = GlobalMultiOutputConfig();
        config_ = FindById(global.targets, targetid_);
        if (!config_)
            return;

        timer_ = new QTimer(this);
        timer_->setInterval(std::chrono::milliseconds(1000));
        QObject::connect(timer_, &QTimer::timeout, [this]() {
            UpdateStreamStatus();
        });

        auto layout = new QGridLayout(this);
        layout->setContentsMargins(10, 7, 10, 7);
        layout->setHorizontalSpacing(8);
        layout->setVerticalSpacing(1);
        layout->addWidget(icon_ = new QLabel(this), 0, 0, 2, 1);
        icon_->setObjectName("outputPlatformIcon");
        icon_->setAlignment(Qt::AlignCenter);
        icon_->setFixedSize(36, 36);
        layout->addWidget(name_ = new QLabel(obs_module_text("NewStreaming"), this), 0, 1);
        name_->setObjectName("outputName");
        layout->addWidget(msg_ = new QLabel(this), 1, 1);
        msg_->setObjectName("outputStatus");
        msg_->setWordWrap(false);
        linkedStatus_=new QLabel(this);
        linkedStatus_->setObjectName("outputLinkedStatus");
        linkedStatus_->setWordWrap(true);
        linkedStatus_->setStyleSheet("color:#dbe7ef;font-size:11px;");
        linkedStatus_->hide();
        layout->addWidget(linkedStatus_,2,1,1,4);
        layout->setColumnStretch(1, 1);

        layout->addWidget(btn_ = new StreamToggleButton(this), 0, 2, 2, 1);
        btn_->setObjectName("outputToggle");
        QObject::connect(btn_, &QPushButton::clicked, [this](bool checked) {
            if (checked && !HasStreamConfig()) {
                btn_->setChecked(false);
                SetMsg(tr("● Pendente"));
                msg_->setToolTip(tr("Informe o servidor RTMP e a stream key nas configurações."));
                QMessageBox::warning(
                    this,
                    tr("Transmissão não configurada"),
                    tr("Configure o servidor RTMP e a stream key antes de ativar esta transmissão."));
            } else {
                const bool previous = config_->enabled;
                config_->enabled = checked;
                if (!SaveMultiOutputConfig()) {
                    config_->enabled = previous;
                    btn_->setChecked(previous);
                    QMessageBox::warning(this, tr("Falha ao salvar"), tr("Não foi possível salvar a seleção desta saída no perfil do OBS."));
                }
            }
        });

        stream_btn_ = new QPushButton(this);
        stream_btn_->setObjectName("outputStreamButton");
        stream_btn_->setCursor(Qt::PointingHandCursor);
        stream_btn_->setFixedSize(32, 30);
        stream_btn_->setIcon(QIcon(":/streamhub-ui/icons/play.svg"));
        stream_btn_->setIconSize(QSize(17, 17));
        stream_btn_->setToolTip(tr("Iniciar ou parar somente esta saída"));
        stream_btn_->setAccessibleName(tr("Iniciar ou parar transmissão deste destino"));
        QObject::connect(stream_btn_, &QPushButton::clicked, this, [this]() {
            if(streamAction_){streamAction_();return;}
            if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) {
                if (forceStopAvailable_ && output_) obs_output_force_stop(output_);
                return;
            }
            if (IsRunning())
                StopStreaming();
            else
                StartStreaming();
        });
        layout->addWidget(stream_btn_, 0, 3, 2, 1);

        layout->addWidget(edit_btn_ = new QPushButton(this), 0, 4, 2, 1);
        edit_btn_->setObjectName("outputEdit");
        edit_btn_->setIcon(QIcon(":/streamhub-ui/icons/settings.svg"));
        edit_btn_->setIconSize(QSize(18, 18));
        edit_btn_->setToolTip(tr("Abrir configurações desta plataforma"));
        edit_btn_->setFixedSize(32, 30);

        remove_btn_ = new QPushButton(obs_module_text("Btn.Delete"), this);
        remove_btn_->setObjectName("outputDelete");
        remove_btn_->hide();
        setLayout(layout);

        LoadConfig();
    }
    
    ~PushWidgetImpl()
    {
        if (output_) {
            // The widget is going away, so no signal may retain a callback to
            // it. OBS owns its attached encoders until the asynchronous stop
            // finishes; release only our references here.
            DisconnectSignals(output_);
            if (obs_output_active(output_))
                obs_output_force_stop(output_);
            obs_output_release(output_);
            output_ = nullptr;
        }
        ReleaseOutputSceneView();
    }


    void StartStreaming() override {
        if (IsRunning())
            return;
        blog(LOG_INFO,"[streamhub-output] start requested: id=%s orientation=%s",targetid_.c_str(),config_->orientation.c_str());

        if (!HasStreamConfig()) {
            blog(LOG_WARNING,"[streamhub-output] start rejected: id=%s incomplete credentials or canvas",targetid_.c_str());
            SetMsg(tr("● Pendente"));
            msg_->setToolTip(tr("Informe o servidor RTMP e a stream key nas configurações."));
            return;
        }

        for (const auto &candidate : GlobalMultiOutputConfig().targets) {
            if (candidate->id == targetid_ || !candidate->serviceParam.is_object()) continue;
            if (candidate->serviceParam.value("key", std::string{}) != config_->serviceParam.value("key", std::string{})) continue;
            if (!(candidate->platform == "youtube" && config_->platform == "youtube") &&
                candidate->serviceParam.value("server", std::string{}) != config_->serviceParam.value("server", std::string{})) continue;
            OBSOutputAutoRelease other = obs_get_output_by_name(("streamhub-output-" + candidate->id).c_str());
            if (other && obs_output_active(other)) {
                SetMsg(tr("Esta chave já está em uso por outra saída. Use uma chave separada."));
                return;
            }
        }

        // Recreate only after the previous instance has fully stopped.
        if (!ReleaseOutput()) {
            SetMsg(tr("Aguardando a saída anterior encerrar..."));
            return;
        }

        if (output_ == nullptr)
        {
            obs_data* output_settings = obs_data_create_from_json(config_->outputParam.dump().c_str());

            auto protocolInfo = GetProtocolInfos()->GetInfo(config_->protocol.c_str());
            assert(protocolInfo);
            if (!protocolInfo) {
	        	blog(LOG_ERROR, TAG "Invalid protocol \"%s\", maybe broken config file.", config_->protocol.c_str());
	        	protocolInfo = GetProtocolInfos()->GetList();
	        }
            auto output_id = protocolInfo->outputId;

            blog(LOG_DEBUG, "Streaming to output: %s", output_id);

            output_ = obs_output_create(output_id, ("streamhub-output-" + targetid_).c_str(), output_settings, nullptr);
            obs_data_release(output_settings);
            if (!output_) {
                SetMsg(obs_module_text("Error.StartOutput"), true);
                return;
            }
            SetMeAsHandler(output_);
        }

        if (output_) {
            auto profileConfig = obs_frontend_get_profile_config();
            if (profileConfig) {
                bool useDelay = config_get_bool(profileConfig, "Output", "DelayEnable");
                bool preserveDelay = config_get_bool(profileConfig, "Output", "DelayPreserve");
                int delaySec = config_get_int(profileConfig, "Output", "DelaySec");
                obs_output_set_delay(output_,
                    useDelay ? delaySec : 0,
                    preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0
                );

            }
        }

        if (!PrepareOutputService())
        {
            SetMsg(obs_module_text("Error.CreateRtmpService"), true);
            ReleaseOutput();
            return;
        }

        if (!PrepareOutputEncoders())
        {
            SetMsg(obs_module_text("Error.CreateEncoder"), true);
            ReleaseOutput();
            return;
        }

        if (!PrepareEncoderSource())
        {
            SetMsg(config_->orientation == "vertical"
                ? tr("Selecione uma área vertical disponível nas configurações.")
                : QString(obs_module_text("Error.SceneNotExist")));
            ReleaseOutput();
            return;
        }

        lifecycle_.start();
        forceStopAvailable_ = false;
        SetMsg(tr("Conectando..."));
        UpdateStreamStatus();
        if (!obs_output_start(output_))
        {
            lifecycle_.stopped();
            SetMsg(obs_module_text("Error.StartOutput"), true);
            ReleaseOutput();
            UpdateStreamStatus();
        }
    }

    void StopStreaming() override {
        if (!IsRunning())
            return;
        
        if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) {
            if(forceStopAvailable_&&output_)obs_output_force_stop(output_);
            return;
        }
        lifecycle_.stop();
        timer_->stop();
        current_bps_ = 0.0;
        SetMsg(tr("Encerrando..."));
        UpdateStreamStatus();
        const auto stoppingGeneration = outputGeneration_.load();
        QTimer::singleShot(8000, this, [this, stoppingGeneration]() {
            if (outputGeneration_.load() == stoppingGeneration && lifecycle_.phase == OutputLifecycle::Phase::Stopping) {
                forceStopAvailable_ = true;
                SetMsg(tr("Encerrando — clique em parar para forçar"));
                UpdateStreamStatus();
            }
        });
        obs_output_stop(output_);
    }
   
    void OnOBSEvent(obs_frontend_event ev) override
    {
        if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_EXIT
            || ev == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_CHANGED
            || ev == obs_frontend_event::OBS_FRONTEND_EVENT_PROFILE_LIST_CHANGED
        ) {
            Stop();
        } else if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_STREAMING_STARTED) {
            if (!IsRunning() && StreamHubShouldSyncStartWithMain(*config_, GlobalMultiOutputConfig().targets)) {
                StartStreaming();
            }
        } else if (ev == obs_frontend_event::OBS_FRONTEND_EVENT_STREAMING_STOPPING) {
            if (IsRunning() && config_->syncStop) {
                StopStreaming();
            }
        }
    }

    void LoadConfig()
    {
        name_->setText(QString::fromUtf8(config_->name) + (config_->orientation == "vertical" ? tr(" · 9:16") : QString()));
        const auto &platform = StreamHubPlatformForTarget(*config_);
        static_cast<StreamToggleButton *>(btn_)->SetAccentColor(QColor(platform.accent));
        btn_->setChecked(config_->enabled);
        icon_->setPixmap(QIcon(platform.iconPath).pixmap(30, 30));
        icon_->setStyleSheet(QString("background:#080c14; border:1px solid %1; border-radius:8px;")
                                 .arg(platform.accent));
        setProperty("platform", platform.id);
        setStyleSheet(QString("QWidget#outputCard { border-left: 3px solid %1; }").arg(platform.accent));
        if (!IsRunning()) {
            const bool configured = HasStreamConfig();
            msg_->setProperty("ready", configured);
            SetMsg(configured ? tr("● Pronto") : tr("● Pendente"));
            if (!configured)
                msg_->setToolTip(tr("Informe o servidor RTMP e a stream key nas configurações."));
            msg_->style()->unpolish(msg_);
            msg_->style()->polish(msg_);
        }
        if (stream_btn_) {
            stream_btn_->setIcon(QIcon(IsRunning() ? ":/streamhub-ui/icons/stop.svg"
                                                    : ":/streamhub-ui/icons/play.svg"));
            stream_btn_->setToolTip(IsRunning() ? tr("Parar somente esta saída")
                                                : tr("Iniciar somente esta saída"));
            stream_btn_->setAccessibleName(stream_btn_->toolTip());
        }
    }

    void ReloadConfig() override { LoadConfig(); }

    bool IsEnabledForAll() const override {
        return config_ && StreamHubIncludedInStartAll(*config_, GlobalMultiOutputConfig().targets);
    }
    bool IsEnabledForLinkedStart() const override { return config_ && config_->enabled && config_->startWithParent; }
    bool IsRunningForAggregate() const override { return lifecycle_.phase == OutputLifecycle::Phase::Live; }
    bool IsBusy() const override { return IsRunning(); }
    double CurrentBitrateBps() const override { return IsRunningForAggregate() ? current_bps_ : 0.0; }
    nlohmann::json CaptureMainEncoding() override {
        if (IsBusy()) return {};
        const auto savedCopy = config_->copiedEncoding;
        const int savedBitrate = config_->videoBitrateKbps;
        config_->copiedEncoding = nullptr;
        config_->videoBitrateKbps = 0;
        auto video = CreateProfileStreamVideoEncoder();
        auto audio = CreateProfileStreamAudioEncoder(0);
        config_->copiedEncoding = savedCopy;
        config_->videoBitrateKbps = savedBitrate;
        if (!video || !audio) return {};
        OBSDataAutoRelease vs = obs_encoder_get_settings(video);
        OBSDataAutoRelease as = obs_encoder_get_settings(audio);
        obs_video_info info{};
        if (!obs_get_video_info(&info)) return {};
        auto *profile = obs_frontend_get_profile_config();
        return {{"videoId", obs_encoder_get_id(video)}, {"audioId", obs_encoder_get_id(audio)},
            {"video", nlohmann::json::parse(obs_data_get_json(vs))},
            {"audio", nlohmann::json::parse(obs_data_get_json(as))},
            {"width", info.output_width}, {"height", info.output_height},
            {"fpsNum", info.fps_num}, {"fpsDen", info.fps_den},
            {"mixer", std::max(0, int(config_get_int(profile, "AdvOut", "TrackIndex")) - 1)}};
    }

    void ResetInfo()
    {
        total_frames_ = 0;
        total_bytes_ = 0;
        current_bps_ = 0.0;
        last_info_time_ = clock::now();
        msg_->setText("");
    }

    bool IsRunning() const
    {
        return lifecycle_.busy() || (output_ != nullptr && obs_output_active(output_));
    }

    void StartStop()
    {
        if (IsRunning())
        {
            StopStreaming();
            return;
        }

        StartStreaming();
    }

    void Stop()
    {
        if (IsRunning())
        {
            obs_output_force_stop(output_);
        }
    }

    bool ShowEditDlg() override
    {
        std::unique_ptr<EditOutputWidget> dlg{ createEditOutputWidget(targetid_, (QMainWindow*)obs_frontend_get_main_window()) };

        if (dlg->exec() == QDialog::DialogCode::Accepted)
        {
            SaveMultiOutputConfig();
            LoadConfig();
            return true;
        }
        else
            return false;
    }

    QPushButton* GetStreamButton() override { return stream_btn_; }
    void SetStreamAction(std::function<void()> action) override { streamAction_=std::move(action); }
    QString StreamStatus() const override {
        switch(lifecycle_.phase) {
        case OutputLifecycle::Phase::Starting:return tr("Conectando...");
        case OutputLifecycle::Phase::Live:return tr("Ao vivo");
        case OutputLifecycle::Phase::Reconnecting:return tr("Reconectando...");
        case OutputLifecycle::Phase::Stopping:return tr("Encerrando...");
        default:return msg_->text().isEmpty()?tr("Parada"):msg_->text();
        }
    }
    void SetLinkedStatus(const QString &text,bool busy) override {
        linkedBusy_=busy;
        linkedStatus_->setText(text);linkedStatus_->setToolTip(text);linkedStatus_->setVisible(!text.isEmpty());
        const bool running=IsRunning()||linkedBusy_;
        stream_btn_->setIcon(QIcon(running?":/streamhub-ui/icons/stop.svg":":/streamhub-ui/icons/play.svg"));
        stream_btn_->setToolTip(text.isEmpty()?(running?tr("Parar somente esta saída"):tr("Iniciar somente esta saída")):
            (running?tr("Parar horizontal e vertical vinculada"):tr("Iniciar horizontal e vertical vinculada habilitada")));
        stream_btn_->setAccessibleName(stream_btn_->toolTip());
        edit_btn_->setEnabled(!running);
    }

    void SetMsg(QString msg, bool error = false)
    {
        if(error)blog(LOG_WARNING,"[streamhub-output] id=%s orientation=%s error=%s",targetid_.c_str(),config_->orientation.c_str(),msg.toUtf8().constData());
        msg_->setText(msg);
        msg_->setToolTip(msg);
        msg_->setProperty("error", error);
        msg_->setProperty("ready", !error && (lifecycle_.phase == OutputLifecycle::Phase::Live || (!IsRunning() && HasStreamConfig())));
        msg_->style()->unpolish(msg_);
        msg_->style()->polish(msg_);
    }

    // obs logical
    void OnStarting() override
    {
        InUi([this]() {
            if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) return;
            begin_time_ = clock::now();
            remove_btn_->setEnabled(false);
            btn_->setEnabled(true);
            SetMsg(obs_module_text("Status.Connecting"));
            UpdateStreamStatus();
            remove_btn_->setEnabled(false);
        });
    }

    void OnStarted() override
    {
        InUi([this]() {
            if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) return;
            lifecycle_.connected();
            remove_btn_->setEnabled(false);
            btn_->setEnabled(true);
            SetMsg(obs_module_text("Status.Streaming"));
            UpdateStreamStatus();

            ResetInfo();
            timer_->start();
        });
    }

    void OnReconnect() override
    {
        InUi([this]() {
            if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) return;
            lifecycle_.reconnect();
            current_bps_ = 0.0;
            timer_->stop();

            remove_btn_->setEnabled(false);
            btn_->setEnabled(true);
            SetMsg(obs_module_text("Status.Reconnecting"));
            UpdateStreamStatus();
        });
    }

    void OnReconnected() override
    {
        InUi([this]() {
            if (lifecycle_.phase == OutputLifecycle::Phase::Stopping) return;
            lifecycle_.connected();
            remove_btn_->setEnabled(false);
            btn_->setEnabled(true);
            SetMsg(obs_module_text("Status.Streaming"));
            UpdateStreamStatus();

            ResetInfo();
            timer_->start();
        });
    }

    void OnStopping() override
    {
        InUi([this]() {
            lifecycle_.stop();
            current_bps_ = 0.0;
            timer_->stop();

            remove_btn_->setEnabled(false);
            btn_->setEnabled(true);
            SetMsg(obs_module_text("Status.Stopping"));
            UpdateStreamStatus();
        });
    }

    void OnStopped(int code) override
    {
        InUi([this, code]() {
            ReleaseOutput();
            lifecycle_.stopped();
            forceStopAvailable_ = false;
            ResetInfo();
            timer_->stop();

            remove_btn_->setEnabled(true);
            btn_->setEnabled(true);
            SetMsg(u8"");
            UpdateStreamStatus();

            switch(code)
            {
                case 0:
                    LoadConfig();
                    break;
                case -1:
                    SetMsg(obs_module_text("Error.WrongRTMPUrl"), true);
                    break;
                case -2:
                    SetMsg(obs_module_text("Error.ServerConnect"), true);
                    break;
                case -3:
                    SetMsg(obs_module_text("Error.ServerHandshake"), true);
                    break;
                case -4:
                    SetMsg(obs_module_text("Error.ServerRefuse"), true);
                    break;
                default:
                    SetMsg(obs_module_text("Error.Unknown"), true);
                    break;
            }
        });

    }
};

PushWidget* createPushWidget(const std::string& targetid, QWidget* parent) {
    return new PushWidgetImpl(targetid, parent);
}
