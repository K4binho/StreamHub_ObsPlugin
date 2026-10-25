

#include "output-config.h"
#include "obs.hpp"
#include "vertical-output.h"
#include "center-snap.h"
#include "vertical-editor-core.h"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include <QHBoxLayout>
#include <QCheckBox>
#include <QCursor>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPushButton>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLineF>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QWindow>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QKeyEvent>
#include <QRubberBand>
#include <QShortcut>
#include <QWheelEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <util/config-file.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "vertical-workspace.h"
#include "vertical-preview.h"
#include "vertical-editor-widgets.h"

StreamHubVerticalPreview::StreamHubVerticalPreview(StreamHubVerticalWorkspace *workspace, QWidget *parent)
    : QWidget(parent), workspace_(workspace)
{
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_StaticContents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
    // The preview is scaled into whatever room the dock has.  A fixed minimum
    // here made the whole dock impossible to shrink on compact OBS layouts.
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background:#05090f; border:1px solid #245078;");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);
    selectionBand_ = new QRubberBand(QRubberBand::Rectangle, this);
    for (auto &label : distanceLabels_) {
        label = new QLabel(this);
        label->setAttribute(Qt::WA_DontCreateNativeAncestors);
        label->setAttribute(Qt::WA_NativeWindow);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        label->setStyleSheet("QLabel { color:white; background:#172b3b; padding:2px 4px; border-radius:2px; }");
        label->hide();
    }
    auto *guidesTimer = new QTimer(this);
    connect(guidesTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible()) return;
        const QRectF bounds = SelectedItemBounds();
        const bool selected = guidesActive_.load() && selectedItemId_.load() >= 0 && !bounds.isEmpty();
        const double scale = ViewScale();
        const QPointF offset((width() - 1080 * scale) / 2+panX_.load()*scale, (height() - 1920 * scale) / 2+panY_.load()*scale);
        const double distances[] = {bounds.left(), 1080 - bounds.right(), bounds.top(), 1920 - bounds.bottom()};
        const QPointF points[] = {QPointF(bounds.left() / 2, bounds.center().y()),
            QPointF((bounds.right() + 1080) / 2, bounds.center().y()),
            QPointF(bounds.center().x(), bounds.top() / 2), QPointF(bounds.center().x(), (bounds.bottom() + 1920) / 2)};
        for (int i = 0; i < 4; ++i) {
            auto *label = distanceLabels_[i];
            label->setVisible(selected);
            if (!selected) continue;
            label->setText(QString::number(qRound(distances[i])) + tr(" px"));
            label->adjustSize();
            const QPointF position = offset + points[i] * scale;
            label->move(qBound(0, qRound(position.x()) - label->width() / 2, (std::max)(0, width() - label->width())),
                        qBound(0, qRound(position.y()) - label->height() / 2, (std::max)(0, height() - label->height())));
            label->raise();
        }
    });
    guidesTimer->start(50);
}

StreamHubVerticalPreview::~StreamHubVerticalPreview()
{
    if (display_) {
        obs_display_remove_draw_callback(display_, Draw, this);
        obs_display_destroy(display_);
        display_ = nullptr;
    }
}

void StreamHubVerticalPreview::SetCanvasUuid(std::string uuid)
{
    std::lock_guard<std::mutex> lock(sceneMutex_);
    canvasUuid_ = std::move(uuid);
}

void StreamHubVerticalPreview::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (windowHandle()) {
        connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible) CreateDisplay();
        });
        windowHandle()->installEventFilter(this);
    }
    QTimer::singleShot(0, this, [this]() { CreateDisplay(); });
}

void StreamHubVerticalPreview::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    ResizeDisplay();
}

bool StreamHubVerticalPreview::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type()==QEvent::MouseButtonPress && !dragging_ && !resizing_) {
        auto *widget=qobject_cast<QWidget *>(watched);
        auto *sourcePanel=workspace_->sources_ ? workspace_->sources_->parentWidget() : nullptr;
        const bool inPreview=widget && (widget==this || isAncestorOf(widget));
        const bool inSources=widget && sourcePanel && (widget==sourcePanel || sourcePanel->isAncestorOf(widget));
        if(widget && !inPreview && !inSources && !qobject_cast<QMenu *>(widget->window())) {
            QTimer::singleShot(0,this,[this]() {
                if(workspace_->sources_) workspace_->sources_->clearSelection();
                workspace_->selectedSourceId_=-1;
                SetSelectedItemId(-1);
            });
        }
    }
    if (watched == windowHandle() && event->type() == QEvent::PlatformSurface) {
        auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            DestroyDisplay();
    }
    return QWidget::eventFilter(watched, event);
}

void StreamHubVerticalPreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    CreateDisplay();
}

void StreamHubVerticalPreview::CreateDisplay()
{
    if (display_ || !isVisible() || !windowHandle() || !windowHandle()->isExposed() || width() <= 0 || height() <= 0)
        return;
    gs_init_data init{};
    init.window.hwnd = reinterpret_cast<void *>(winId());
    const QSize pixelSize = size() * devicePixelRatioF();
    init.cx = static_cast<uint32_t>(pixelSize.width());
    init.cy = static_cast<uint32_t>(pixelSize.height());
    init.format = GS_BGRA;
    init.zsformat = GS_ZS_NONE;
    display_ = obs_display_create(&init, 0xFF05090F);
    if (display_)
        obs_display_add_draw_callback(display_, Draw, this);
}

void StreamHubVerticalPreview::DestroyDisplay()
{
    if (!display_) return;
    obs_display_remove_draw_callback(display_, Draw, this);
    obs_display_destroy(display_);
    display_ = nullptr;
}

void StreamHubVerticalPreview::ResizeDisplay()
{
    if (!display_) return;
    const QSize pixelSize = size() * devicePixelRatioF();
    if (pixelSize.width() > 0 && pixelSize.height() > 0)
        obs_display_resize(display_, static_cast<uint32_t>(pixelSize.width()), static_cast<uint32_t>(pixelSize.height()));
}

void StreamHubVerticalPreview::Draw(void *param, uint32_t cx, uint32_t cy)
{
    auto *self = static_cast<StreamHubVerticalPreview *>(param);
    std::string canvasUuid;
    {
        std::lock_guard<std::mutex> lock(self->sceneMutex_);
        canvasUuid = self->canvasUuid_;
    }
    obs_canvas_t *canvas = canvasUuid.empty() ? nullptr : obs_get_canvas_by_uuid(canvasUuid.c_str());
    if (!canvas) {
        self->hasRenderedSource_ = false;
        return;
    }
    OBSSourceAutoRelease source = obs_canvas_get_channel(canvas, 0);
    obs_video_info canvasInfo{};
    const bool validInfo = obs_canvas_get_video_info(canvas, &canvasInfo);
    if (!source || !cx || !cy) {
        obs_canvas_release(canvas);
        self->hasRenderedSource_ = false;
        return;
    }
    self->hasRenderedSource_ = true;

    const uint32_t sourceWidth = validInfo ? (std::max)(canvasInfo.base_width, 1u) : (std::max)(obs_source_get_width(source), 1u);
    const uint32_t sourceHeight = validInfo ? (std::max)(canvasInfo.base_height, 1u) : (std::max)(obs_source_get_height(source), 1u);
    const float scale = (std::min)(static_cast<float>(cx) / sourceWidth, static_cast<float>(cy) / sourceHeight)*float(self->zoom_.load());
    const uint32_t drawWidth = static_cast<uint32_t>(sourceWidth * scale);
    const uint32_t drawHeight = static_cast<uint32_t>(sourceHeight * scale);
    const int x = static_cast<int>((double(cx) - drawWidth) / 2+self->panX_.load()*scale);
    const int y = static_cast<int>((double(cy) - drawHeight) / 2+self->panY_.load()*scale);

    gs_viewport_push();
    gs_projection_push();
    const bool previousSrgb = gs_set_linear_srgb(true);
    gs_ortho(0.0f, static_cast<float>(sourceWidth), 0.0f, static_cast<float>(sourceHeight), -100.0f, 100.0f);
    gs_set_viewport(x, y, drawWidth, drawHeight);
    // Match the native OBS preview: show the canvas mix, not a second render of its sources.
    if(obs_canvas_get_video(canvas))obs_render_canvas_texture(canvas);
    else obs_canvas_render(canvas);

    auto *scene = obs_scene_from_source(source);
    const long long selectedId = self->selectedItemId_.load();
    if (scene) {
        StreamHubWalkItems(scene,[&](obs_sceneitem_t *item,int) {
        if ((obs_sceneitem_selected(item) || StreamHubEditorKey(item)==selectedId) && StreamHubEditable(scene,item)) {
            obs_transform_info transform{};
            obs_sceneitem_get_info2(item, &transform);
            auto *itemSource = obs_sceneitem_get_source(item);
            obs_sceneitem_crop crop{};
            obs_sceneitem_get_crop(item, &crop);
            const float w = transform.bounds_type == OBS_BOUNDS_NONE
                ? (std::max)(0.0f, float(obs_source_get_width(itemSource)) - crop.left - crop.right) * std::abs(transform.scale.x) : transform.bounds.x;
            const float h = transform.bounds_type == OBS_BOUNDS_NONE
                ? (std::max)(0.0f, float(obs_source_get_height(itemSource)) - crop.top - crop.bottom) * std::abs(transform.scale.y) : transform.bounds.y;
            const float ax = (transform.alignment & OBS_ALIGN_RIGHT) ? 1.0f :
                             (transform.alignment & OBS_ALIGN_LEFT) ? 0.0f : 0.5f;
            const float ay = (transform.alignment & OBS_ALIGN_BOTTOM) ? 1.0f :
                             (transform.alignment & OBS_ALIGN_TOP) ? 0.0f : 0.5f;
            gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
            vec4 color;
            vec4_set(&color, 0.0f, 0.78f, 1.0f, 1.0f);
            gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);
            gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
            gs_technique_begin(tech);
            gs_technique_begin_pass(tech, 0);
            gs_matrix_push();
            matrix4 boxMatrix;
            const auto world=StreamHubWorldBox(scene,item);
            matrix4_identity(&boxMatrix);
            boxMatrix.x.x=float(world.m11()); boxMatrix.x.y=float(world.m12());
            boxMatrix.y.x=float(world.m21()); boxMatrix.y.y=float(world.m22());
            boxMatrix.t.x=float(world.dx()); boxMatrix.t.y=float(world.dy());
            gs_matrix_mul(&boxMatrix);
            gs_render_start(true);
            gs_vertex2f(0.0f, 0.0f); gs_vertex2f(0.0f, 1.0f);
            gs_vertex2f(1.0f, 1.0f); gs_vertex2f(1.0f, 0.0f);
            gs_vertex2f(0.0f, 0.0f);
            gs_render_stop(GS_LINESTRIP);
            const float hx = 4.0f / (std::max)(scale * w, 1.0f);
            const float hy = 4.0f / (std::max)(scale * h, 1.0f);
            const float rotationY=-24.0f/(std::max)(scale*h,1.0f);
            gs_render_start(true);
            gs_vertex2f(.5f,0); gs_vertex2f(.5f,rotationY);
            gs_render_stop(GS_LINES);
            gs_render_start(true);
            gs_vertex2f(.5f-hx,rotationY-hy); gs_vertex2f(.5f+hx,rotationY-hy);
            gs_vertex2f(.5f+hx,rotationY+hy); gs_vertex2f(.5f-hx,rotationY+hy); gs_vertex2f(.5f-hx,rotationY-hy);
            gs_render_stop(GS_LINESTRIP);
            for (const QPointF &handle : {QPointF(0, 0), QPointF(.5, 0), QPointF(1, 0), QPointF(0, .5),
                                         QPointF(1, .5), QPointF(0, 1), QPointF(.5, 1), QPointF(1, 1)}) {
                const float px = static_cast<float>(handle.x()), py = static_cast<float>(handle.y());
                gs_render_start(true);
                gs_vertex2f(px - hx, py - hy); gs_vertex2f(px + hx, py - hy);
                gs_vertex2f(px + hx, py + hy); gs_vertex2f(px - hx, py + hy); gs_vertex2f(px - hx, py - hy);
                gs_render_stop(GS_LINESTRIP);
            }
            gs_matrix_pop();
            if (self->guidesActive_.load() && StreamHubEditorKey(item)==selectedId) {
            QTransform boundsTransform;
            boundsTransform.translate(transform.pos.x, transform.pos.y);
            boundsTransform.rotate(transform.rot);
            boundsTransform.translate(-ax * w, -ay * h);
            const QRectF bounds = world.mapRect(QRectF(0,0,1,1));
            vec4_set(&color, 1.0f, 0.72f, 0.2f, 1.0f);
            gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);
            gs_render_start(true);
            const float midX = static_cast<float>(bounds.center().x()), midY = static_cast<float>(bounds.center().y());
            const float arrow = 8.0f / (std::max)(scale, 0.01f);
            gs_vertex2f(0, midY); gs_vertex2f(static_cast<float>(bounds.left()), midY);
            gs_vertex2f(static_cast<float>(bounds.right()), midY); gs_vertex2f(static_cast<float>(sourceWidth), midY);
            gs_vertex2f(midX, 0); gs_vertex2f(midX, static_cast<float>(bounds.top()));
            gs_vertex2f(midX, static_cast<float>(bounds.bottom())); gs_vertex2f(midX, static_cast<float>(sourceHeight));
            if (std::abs(midX - sourceWidth / 2.0f) < 1) {
                gs_vertex2f(midX, 0); gs_vertex2f(midX, static_cast<float>(sourceHeight));
                gs_vertex2f(midX, 0); gs_vertex2f(midX - arrow, arrow);
                gs_vertex2f(midX, 0); gs_vertex2f(midX + arrow, arrow);
                gs_vertex2f(midX, sourceHeight); gs_vertex2f(midX - arrow, sourceHeight - arrow);
                gs_vertex2f(midX, sourceHeight); gs_vertex2f(midX + arrow, sourceHeight - arrow);
            }
            if (std::abs(midY - sourceHeight / 2.0f) < 1) {
                gs_vertex2f(0, midY); gs_vertex2f(static_cast<float>(sourceWidth), midY);
                gs_vertex2f(0, midY); gs_vertex2f(arrow, midY - arrow);
                gs_vertex2f(0, midY); gs_vertex2f(arrow, midY + arrow);
                gs_vertex2f(sourceWidth, midY); gs_vertex2f(sourceWidth - arrow, midY - arrow);
                gs_vertex2f(sourceWidth, midY); gs_vertex2f(sourceWidth - arrow, midY + arrow);
            }
            gs_render_stop(GS_LINES);
            }
            gs_technique_end_pass(tech);
            gs_technique_end(tech);
        }
        });
    }
    gs_set_linear_srgb(previousSrgb);
    gs_load_vertexbuffer(nullptr);
    gs_projection_pop();
    gs_viewport_pop();
    obs_canvas_release(canvas);
}
