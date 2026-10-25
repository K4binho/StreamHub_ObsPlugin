

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

bool StreamHubVerticalPreview::CanvasPoint(const QPointF &point, QPointF &canvasPoint) const
{
    obs_video_info info{};
    auto target = FindById(GlobalMultiOutputConfig().targets, workspace_->TargetId());
    obs_canvas_t *canvas = target ? obs_get_canvas_by_uuid(target->canvasUuid.c_str()) : nullptr;
    if (!canvas)
        return false;
    const bool valid = obs_canvas_get_video_info(canvas, &info);
    obs_canvas_release(canvas);
    if (!valid || !info.base_width || !info.base_height)
        return false;
    const double scale = (std::min)(static_cast<double>(width()) / info.base_width,
                                    static_cast<double>(height()) / info.base_height)*zoom_.load();
    const double viewWidth = info.base_width * scale;
    const double viewHeight = info.base_height * scale;
    const double left = (width() - viewWidth) / 2.0+panX_.load()*scale;
    const double top = (height() - viewHeight) / 2.0+panY_.load()*scale;
    if (!dragging_ && !resizing_ && !boxSelecting_ && (point.x() < left || point.x() > left + viewWidth || point.y() < top || point.y() > top + viewHeight))
        return false;
    canvasPoint = QPointF((point.x() - left) / scale, (point.y() - top) / scale);
    return true;
}

obs_sceneitem_t *StreamHubVerticalPreview::FindTopItemAt(const QPointF &point)
{
    auto *scene = workspace_->SelectedScene();
    if (!scene)
        return nullptr;
    obs_sceneitem_t *hit=nullptr;
    StreamHubWalkItems(scene, [&](obs_sceneitem_t *candidate,int) {
        if (!StreamHubEditable(scene,candidate)) return;
        // A group is moved as a unit unless a child has explicitly been selected in the tree.
        if(obs_sceneitem_get_scene(candidate)!=scene && !obs_sceneitem_selected(candidate)) return;
        obs_transform_info info{};
        obs_sceneitem_get_info2(candidate, &info);
        obs_source_t *source = obs_sceneitem_get_source(candidate);
        obs_sceneitem_crop crop{};
        obs_sceneitem_get_crop(candidate, &crop);
        const float w = info.bounds_type == OBS_BOUNDS_NONE
            ? (std::max)(0.0f, float(obs_source_get_width(source)) - crop.left - crop.right) * std::abs(info.scale.x) : info.bounds.x;
        const float h = info.bounds_type == OBS_BOUNDS_NONE
            ? (std::max)(0.0f, float(obs_source_get_height(source)) - crop.top - crop.bottom) * std::abs(info.scale.y) : info.bounds.y;
        const float ax = (info.alignment & OBS_ALIGN_RIGHT) ? 1.0f : (info.alignment & OBS_ALIGN_LEFT) ? 0.0f : 0.5f;
        const float ay = (info.alignment & OBS_ALIGN_BOTTOM) ? 1.0f : (info.alignment & OBS_ALIGN_TOP) ? 0.0f : 0.5f;
        QTransform transform;
        transform.translate(info.pos.x, info.pos.y);
        transform.rotate(info.rot);
        transform.translate(-ax * w, -ay * h);
        bool invertible = false;
        const QPointF local = StreamHubWorldBox(scene,candidate).inverted(&invertible).map(point);
        if (invertible && QRectF(0, 0, 1, 1).contains(local)) {
            if (hit) obs_sceneitem_release(hit);
            hit = candidate; obs_sceneitem_addref(hit);
        }
    });
    obs_scene_release(scene);
    return hit;
}

QRectF StreamHubVerticalPreview::SelectedItemBounds()
{
    return workspace_->ItemBounds(selectedItemId_.load());
}

int StreamHubVerticalPreview::HandleAt(const QPointF &point)
{
    auto *scene=workspace_->SelectedScene();
    auto *item=scene ? StreamHubFindEditorItem(scene,selectedItemId_.load()) : nullptr;
    int handle=-1;
    if(item && StreamHubEditable(scene,item)) {
        const QTransform box=StreamHubWorldBox(scene,item);
        const QPointF points[]={{0,0},{.5,0},{1,0},{0,.5},{1,.5},{0,1},{.5,1},{1,1}};
        const double scale=ViewScale();
        double nearest=9;
        for(int i=0;i<8;++i) {
            const double distance=QLineF(box.map(points[i]),point).length()*scale;
            if(distance<nearest){nearest=distance;handle=i;}
        }
        QLineF rotation(box.map(QPointF(.5,.5)),box.map(QPointF(.5,0)));
        rotation.setLength(rotation.length()+24/(std::max)(.01,scale));
        if(QLineF(rotation.p2(),point).length()*scale<9) handle=8;
    }
    if(scene) obs_scene_release(scene);
    return handle;
}

Qt::CursorShape StreamHubVerticalPreview::HandleCursor(int handle)
{
    if(handle==8) return resizing_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    if(handle<0) return Qt::ArrowCursor;
    auto *scene=workspace_->SelectedScene();
    auto *item=scene ? StreamHubFindEditorItem(scene,selectedItemId_.load()) : nullptr;
    const auto box=item ? StreamHubWorldBox(scene,item) : QTransform();
    const double rotation=std::atan2(box.m12(),box.m11())*180.0/3.141592653589793;
    const double base=(handle==0||handle==7)?45:(handle==2||handle==5)?-45:(handle==1||handle==6)?90:0;
    const int direction=((qRound((rotation+base)/45)%4)+4)%4;
    if(scene) obs_scene_release(scene);
    const Qt::CursorShape cursors[]={Qt::SizeHorCursor,Qt::SizeFDiagCursor,Qt::SizeVerCursor,Qt::SizeBDiagCursor};
    return cursors[direction];
}

void StreamHubVerticalPreview::UpdateCursor(const QPointF &point)
{
    const int handle=HandleAt(point);
    if(handle>=0){setCursor(HandleCursor(handle));return;}
    auto *hit=FindTopItemAt(point);
    setCursor(hit ? Qt::SizeAllCursor : Qt::ArrowCursor);
    if(hit) obs_sceneitem_release(hit);
}

void StreamHubVerticalPreview::leaveEvent(QEvent *event)
{
    if(!dragging_ && !resizing_) setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

void StreamHubVerticalPreview::mousePressEvent(QMouseEvent *event)
{
    if(event->button()==Qt::MiddleButton) {panning_=true;panStart_=event->position();setCursor(Qt::ClosedHandCursor);event->accept();return;}
    QPointF point;
    if (event->button() != Qt::LeftButton || !CanvasPoint(event->position(), point))
        return QWidget::mousePressEvent(event);
    setFocus(Qt::MouseFocusReason);
    auto *editScene = workspace_->SelectedScene();
    if (!editScene) return;
    undoBefore_ = StreamHubSceneState(editScene);
    resizeHandle_ = HandleAt(point);
    obs_scene_release(editScene);
    const QRectF bounds = SelectedItemBounds();
    const QPointF bottomRight = bounds.bottomRight();
    const double scale = (std::min)(static_cast<double>(width()) / 1080.0, static_cast<double>(height()) / 1920.0);
    if (resizeHandle_ >= 0) {
        resizing_ = true;
    } else {
        auto *hit = FindTopItemAt(point);
        if (!hit) {
            boxSelecting_ = true; selectionStart_=event->position().toPoint(); dragStart_=point;
            selectionBand_->setGeometry(QRect(selectionStart_,QSize())); selectionBand_->show();
            if (!(event->modifiers() & Qt::ControlModifier) && workspace_->sources_) workspace_->sources_->clearSelection();
            return;
        }
        selectedItemId_ = StreamHubEditorKey(hit);
        if (workspace_->sources_) {
            for (int i = 0; i < workspace_->sources_->count(); ++i)
                if (workspace_->sources_->item(i)->data(Qt::UserRole).toLongLong() == selectedItemId_.load())
                    workspace_->sources_->setCurrentRow(i, (event->modifiers() & Qt::ControlModifier)
                        ? QItemSelectionModel::Toggle : (workspace_->sources_->item(i)->isSelected()
                            ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect));
        }
        startPosition_ = QPointF();
        obs_transform_info info{};
        obs_sceneitem_get_info2(hit, &info);
        startPosition_ = QPointF(info.pos.x, info.pos.y);
        startScale_ = QPointF(info.scale.x, info.scale.y);
        obs_sceneitem_release(hit);
        dragging_ = true;
    }
    auto *scene = workspace_->SelectedScene();
    auto *selected = scene ? StreamHubFindEditorItem(scene, selectedItemId_.load()) : nullptr;
    if (selected && resizing_) {
        obs_transform_info info{};
        obs_sceneitem_get_info2(selected, &info);
        startScale_ = QPointF(info.scale.x, info.scale.y);
    }
    if (selected) {
        obs_sceneitem_get_info2(selected, &startInfo_); obs_sceneitem_get_crop(selected, &startCrop_);
        startBox_=StreamHubItemBox(selected);
    }
    dragPositions_.clear(); dragTransforms_.clear(); dragParents_.clear();
    deferredGroups_.clear();
    if(scene)StreamHubWalkItems(scene,[this](obs_sceneitem_t *item,int){
        if(obs_sceneitem_is_group(item)){obs_sceneitem_defer_group_resize_begin(item);deferredGroups_.emplace_back(item);}
    });
    if (scene && workspace_->sources_) for (auto *row : workspace_->sources_->selectedItems()) {
        auto *item=StreamHubFindEditorItem(scene,row->data(Qt::UserRole).toLongLong());
        if (item && StreamHubEditable(scene,item) && !StreamHubSelectedAncestor(scene,item)) {
            const auto key=StreamHubEditorKey(item);
            obs_transform_info info{}; obs_sceneitem_get_info2(item,&info);
            dragTransforms_[key]=info; dragParents_[key]=StreamHubParentTransform(scene,item);
            dragPositions_[key]=dragParents_[key].map(QPointF(info.pos.x,info.pos.y));
        }
    }
    if (scene) obs_scene_release(scene);
    dragStart_ = point;
    centerSnapX_=centerSnapY_=false;
    lastItemBounds_ = SelectedItemBounds();
    guidesActive_ = true;
    setCursor(resizing_ ? HandleCursor(resizeHandle_) : Qt::SizeAllCursor);
    update();
    event->accept();
}

bool StreamHubVerticalPreview::MoveSelectedItem(const QPointF &point)
{
    QPointF position = startPosition_ + point - dragStart_;
    auto *snapConfig=obs_frontend_get_user_config();
    if (snapConfig && config_get_bool(snapConfig,"BasicWindow","SnappingEnabled") && !(QApplication::keyboardModifiers() & Qt::ControlModifier)) {
        const QRectF moved = lastItemBounds_.translated(point - dragStart_);
        const double threshold = config_get_double(snapConfig,"BasicWindow","SnapDistance") / (std::max)(0.01, ViewScale());
        double dx = threshold + 1, dy = threshold + 1;
        auto snap = [threshold](double value, double target, double &best) {
            const double delta = target - value;
            if (std::abs(delta) <= threshold && std::abs(delta) < std::abs(best)) best = delta;
        };
        if (config_get_bool(snapConfig,"BasicWindow","ScreenSnapping")) {
            snap(moved.left(),0,dx); snap(moved.right(),1080,dx);
            snap(moved.top(),0,dy); snap(moved.bottom(),1920,dy);
        }
        if (auto *scene = config_get_bool(snapConfig,"BasicWindow","SourceSnapping") ? workspace_->SelectedScene() : nullptr) {
            struct Guides { std::vector<long long> ids; long long selected; } guides{{}, selectedItemId_.load()};
            obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
                auto &state = *static_cast<Guides *>(data);
                if (obs_sceneitem_visible(item) && !obs_sceneitem_selected(item) && StreamHubEditorKey(item) != state.selected)
                    state.ids.push_back(StreamHubEditorKey(item));
                return true;
            }, &guides);
            obs_scene_release(scene);
            for (auto id : guides.ids) {
                const QRectF other = workspace_->ItemBounds(id);
                for (double x : {moved.left(), moved.center().x(), moved.right()})
                    for (double target : {other.left(), other.center().x(), other.right()}) snap(x, target, dx);
                for (double y : {moved.top(), moved.center().y(), moved.bottom()})
                    for (double target : {other.top(), other.center().y(), other.bottom()}) snap(y, target, dy);
            }
        }
        const bool centerEnabled=config_get_bool(snapConfig,"BasicWindow","CenterSnapping");
        if(centerEnabled && StreamHubCenterSnap(540-moved.center().x(),threshold,centerSnapX_))
            position.rx()+=540-moved.center().x();
        else { centerSnapX_=false; if (std::abs(dx) <= threshold) position.rx() += dx; }
        if(centerEnabled && StreamHubCenterSnap(960-moved.center().y(),threshold,centerSnapY_))
            position.ry()+=960-moved.center().y();
        else { centerSnapY_=false; if (std::abs(dy) <= threshold) position.ry() += dy; }
    }
    else centerSnapX_=centerSnapY_=false;
    const QPointF offset=position-startPosition_;
    bool changed=false;
    for (const auto &[id,start] : dragPositions_)
        changed=workspace_->MoveItem(id,dragParents_[id].inverted().map(start+offset))||changed;
    return changed;
}

bool StreamHubVerticalPreview::ResizeSelectedItem(const QPointF &point)
{
    auto *scene=workspace_->SelectedScene();
    auto *item=scene ? StreamHubFindEditorItem(scene,selectedItemId_.load()) : nullptr;
    if (!item || !StreamHubEditable(scene,item)) { if(scene) obs_scene_release(scene); return false; }
    const auto localPoint=StreamHubParentTransform(scene,item).inverted().map(point);
    const auto modifiers=QApplication::keyboardModifiers();
    auto info=startInfo_;
    if (resizeHandle_==8) {
        const QPointF pivot(startInfo_.pos.x,startInfo_.pos.y);
        const QLineF initial(pivot,StreamHubParentTransform(scene,item).inverted().map(dragStart_)), current(pivot,localPoint);
        double angle=startInfo_.rot+initial.angle()-current.angle();
        if (!(modifiers & Qt::ControlModifier)) angle=std::round(angle/((modifiers & Qt::ShiftModifier)?15.0:1.0))*((modifiers & Qt::ShiftModifier)?15.0:1.0);
        info.rot=float(angle); obs_sceneitem_set_info2(item,&info); obs_scene_release(scene); return true;
    }
    QPointF adjusted=localPoint;
    auto *snapConfig=obs_frontend_get_user_config();
    if(obs_sceneitem_get_scene(item)==scene && std::abs(startInfo_.rot)<.001 && snapConfig && config_get_bool(snapConfig,"BasicWindow","SnappingEnabled") &&
       config_get_bool(snapConfig,"BasicWindow","ScreenSnapping") && !(modifiers&Qt::ControlModifier) && !(modifiers&Qt::AltModifier)) {
        const double threshold=config_get_double(snapConfig,"BasicWindow","SnapDistance")/(std::max)(.01,ViewScale());
        if(std::abs(adjusted.x())<threshold) adjusted.setX(0);
        if(std::abs(adjusted.x()-1080)<threshold) adjusted.setX(1080);
        if(std::abs(adjusted.y())<threshold) adjusted.setY(0);
        if(std::abs(adjusted.y()-1920)<threshold) adjusted.setY(1920);
    }
    const QPointF unit=startBox_.inverted().map(adjusted);
    const double oldW=QLineF(startBox_.map(QPointF(0,0)),startBox_.map(QPointF(1,0))).length();
    const double oldH=QLineF(startBox_.map(QPointF(0,0)),startBox_.map(QPointF(0,1))).length();
    if(oldW<.001 || oldH<.001) { obs_scene_release(scene); return false; }
    const bool left=resizeHandle_==0||resizeHandle_==3||resizeHandle_==5;
    const bool right=resizeHandle_==2||resizeHandle_==4||resizeHandle_==7;
    const bool top=resizeHandle_<=2, bottom=resizeHandle_>=5;
    double x0=left ? (std::min)(unit.x()*oldW,oldW-1) : 0;
    double x1=right ? (std::max)(unit.x()*oldW,1.0) : oldW;
    double y0=top ? (std::min)(unit.y()*oldH,oldH-1) : 0;
    double y1=bottom ? (std::max)(unit.y()*oldH,1.0) : oldH;
    const bool crop=(modifiers & Qt::AltModifier) && info.bounds_type==OBS_BOUNDS_NONE;
    if (crop) {
        auto cropped=startCrop_;
        auto *source=obs_sceneitem_get_source(item);
        const double sx=(std::max)(.001,double(std::abs(info.scale.x))), sy=(std::max)(.001,double(std::abs(info.scale.y)));
        if(left) cropped.left=qBound(0,startCrop_.left+qRound(x0/sx),int(obs_source_get_width(source))-cropped.right-1);
        if(right) cropped.right=qBound(0,startCrop_.right+qRound((oldW-x1)/sx),int(obs_source_get_width(source))-cropped.left-1);
        if(top) cropped.top=qBound(0,startCrop_.top+qRound(y0/sy),int(obs_source_get_height(source))-cropped.bottom-1);
        if(bottom) cropped.bottom=qBound(0,startCrop_.bottom+qRound((oldH-y1)/sy),int(obs_source_get_height(source))-cropped.top-1);
        x0=(cropped.left-startCrop_.left)*sx; x1=oldW-(cropped.right-startCrop_.right)*sx;
        y0=(cropped.top-startCrop_.top)*sy; y1=oldH-(cropped.bottom-startCrop_.bottom)*sy;
        obs_sceneitem_set_crop(item,&cropped);
    } else {
        const bool aspect=info.bounds_type==OBS_BOUNDS_NONE ? !(modifiers & Qt::ShiftModifier) : bool(modifiers & Qt::ShiftModifier);
        if(aspect) {
            const double factor=(left||right) ? (x1-x0)/oldW : (y1-y0)/oldH;
            if(left) x0=x1-oldW*factor; else x1=x0+oldW*factor;
            if(top) y0=y1-oldH*factor; else y1=y0+oldH*factor;
        }
        if(info.bounds_type==OBS_BOUNDS_NONE) { info.scale.x*=float((x1-x0)/oldW); info.scale.y*=float((y1-y0)/oldH); }
        else { info.bounds.x=float(x1-x0); info.bounds.y=float(y1-y0); }
    }
    const double ax=(info.alignment&OBS_ALIGN_LEFT)?0:(info.alignment&OBS_ALIGN_RIGHT)?1:.5;
    const double ay=(info.alignment&OBS_ALIGN_TOP)?0:(info.alignment&OBS_ALIGN_BOTTOM)?1:.5;
    QTransform rotation; rotation.rotate(startInfo_.rot);
    const QPointF offset=rotation.map(QPointF(x0+ax*(x1-x0-oldW),y0+ay*(y1-y0-oldH)));
    info.pos.x+=float(offset.x()); info.pos.y+=float(offset.y());
    obs_sceneitem_set_info2(item,&info); obs_scene_release(scene); return true;
}

void StreamHubVerticalPreview::mouseMoveEvent(QMouseEvent *event)
{
    if(panning_) {
        const auto delta=(event->position()-panStart_)/(std::max)(.01,ViewScale());
        panX_=panX_.load()+delta.x();panY_=panY_.load()+delta.y();panStart_=event->position();event->accept();return;
    }
    QPointF point;
    if (!CanvasPoint(event->position(), point)) {
        setCursor(Qt::ArrowCursor);
        return QWidget::mouseMoveEvent(event);
    }
    if(boxSelecting_) { selectionBand_->setGeometry(QRect(selectionStart_,event->position().toPoint()).normalized()); return; }
    if (dragging_ || resizing_) {
        if (dragging_) MoveSelectedItem(point);
        else {
            ResizeSelectedItem(point);
            auto *scene=workspace_->SelectedScene();
            auto *primary=scene?StreamHubFindEditorItem(scene,selectedItemId_.load()):nullptr;
            if(primary && dragPositions_.size()>1) {
                const auto initial=startBox_*dragParents_[selectedItemId_.load()];
                bool invertible=false;
                const auto change=initial.inverted(&invertible)*StreamHubWorldBox(scene,primary);
                obs_transform_info current{}; obs_sceneitem_get_info2(primary,&current);
                const double sx=startInfo_.scale.x?current.scale.x/startInfo_.scale.x:1;
                const double sy=startInfo_.scale.y?current.scale.y/startInfo_.scale.y:1;
                for(const auto &[id,start]:dragTransforms_) if(id!=selectedItemId_.load() && invertible) {
                    auto *item=StreamHubFindEditorItem(scene,id); if(!item) continue;
                    auto info=start;
                    const auto position=dragParents_[id].inverted().map(change.map(dragPositions_[id]));
                    info.pos.x=float(position.x()); info.pos.y=float(position.y());
                    info.rot+=current.rot-startInfo_.rot;
                    if(info.bounds_type==OBS_BOUNDS_NONE){info.scale.x*=float(sx);info.scale.y*=float(sy);}
                    else {info.bounds.x*=float(sx);info.bounds.y*=float(sy);}
                    obs_sceneitem_set_info2(item,&info);
                }
            }
            if(scene) obs_scene_release(scene);
        }
        update();
        return;
    }
    UpdateCursor(point);
}

void StreamHubVerticalPreview::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button()==Qt::MiddleButton&&panning_){panning_=false;setCursor(Qt::ArrowCursor);event->accept();return;}
    if (event->button()==Qt::LeftButton && boxSelecting_) {
        QPointF end; CanvasPoint(event->position(),end);
        const QRectF selection=QRectF(dragStart_,end).normalized();
        if(workspace_->sources_) for(int i=0;i<workspace_->sources_->count();++i) {
            auto *row=workspace_->sources_->item(i);
            if(selection.intersects(workspace_->ItemBounds(row->data(Qt::UserRole).toLongLong()))) row->setSelected(true);
        }
        boxSelecting_=false; selectionBand_->hide(); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && (dragging_ || resizing_)) {
        FinishEdit();
        dragging_ = false;
        resizing_ = false;
        guidesActive_ = selectedItemId_.load()>=0;
        if(!guidesActive_.load())for (auto *label : distanceLabels_) label->hide();
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void StreamHubVerticalPreview::FinishEdit()
{
    for(auto it=deferredGroups_.rbegin();it!=deferredGroups_.rend();++it)obs_sceneitem_defer_group_resize_end(*it);
    deferredGroups_.clear();
    auto *scene=workspace_->SelectedScene();
    if(!scene) return;
    const QByteArray after=StreamHubSceneState(scene);
    obs_scene_release(scene);
    if(!undoBefore_.isEmpty() && undoBefore_!=after)
        obs_frontend_add_undo_redo_action("Transformar fontes verticais",StreamHubRestoreSceneState,StreamHubRestoreSceneState,
            undoBefore_.constData(),after.constData(),false);
    undoBefore_.clear(); obs_frontend_save();
}

void StreamHubVerticalPreview::keyPressEvent(QKeyEvent *event)
{
    if(event->key()==Qt::Key_0){zoom_=1;panX_=panY_=0;event->accept();return;}
    if(event->matches(QKeySequence::Copy)){workspace_->SourceCommand("copy");event->accept();return;}
    if(event->matches(QKeySequence::Paste)){workspace_->SourceCommand("paste");event->accept();return;}
    if(event->key()==Qt::Key_Delete){workspace_->RemoveSelectedSource();event->accept();return;}
    if(event->key()==Qt::Key_F2){workspace_->SourceCommand("rename");event->accept();return;}
    if(event->modifiers()&Qt::ControlModifier) {
        const bool shift=event->modifiers()&Qt::ShiftModifier;
        QString command;
        switch(event->key()) {
        case Qt::Key_D:command="duplicate";break;
        case Qt::Key_G:command=shift?"ungroup":"group";break;
        case Qt::Key_F:command=shift?"stretch":"fit";break;
        case Qt::Key_R:command="reset";break;
        case Qt::Key_V:if(shift)command="pasteDuplicate";break;
        }
        if(!command.isEmpty()){workspace_->SourceCommand(command);event->accept();return;}
    }
    if(event->key()==Qt::Key_Escape && (dragging_||resizing_)) {
        for(auto it=deferredGroups_.rbegin();it!=deferredGroups_.rend();++it)obs_sceneitem_defer_group_resize_end(*it);
        deferredGroups_.clear();
        if(!undoBefore_.isEmpty()) StreamHubRestoreSceneState(undoBefore_.constData());
        undoBefore_.clear(); dragging_=resizing_=false; guidesActive_=false;
        for(auto *label:distanceLabels_) label->hide(); event->accept(); return;
    }
    QPointF delta;
    const int step=(event->modifiers()&Qt::ShiftModifier)?10:1;
    switch(event->key()) {
    case Qt::Key_Left: delta.setX(-step); break;
    case Qt::Key_Right: delta.setX(step); break;
    case Qt::Key_Up: delta.setY(-step); break;
    case Qt::Key_Down: delta.setY(step); break;
    default: QWidget::keyPressEvent(event); return;
    }
    workspace_->NudgeSelectedSources(delta);event->accept();
}

void StreamHubVerticalPreview::wheelEvent(QWheelEvent *event)
{
    if(dragging_||resizing_||boxSelecting_){event->ignore();return;}
    const double oldScale=ViewScale();
    const QPointF center(width()/2.0,height()/2.0);
    const QPointF anchor=(event->position()-center)/(std::max)(.01,oldScale)-QPointF(panX_.load(),panY_.load());
    zoom_=qBound(.25,zoom_.load()*std::pow(1.15,event->angleDelta().y()/120.0),8.0);
    const auto pan=(event->position()-center)/(std::max)(.01,ViewScale())-anchor;
    panX_=pan.x();panY_=pan.y();event->accept();
}
