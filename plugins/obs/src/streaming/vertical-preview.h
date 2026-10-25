#pragma once

#include "obs.hpp"
#include <QWidget>
#include <QTransform>
#include <QByteArray>
#include <QPoint>
#include <QRectF>
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class QLabel;
class QRubberBand;
class StreamHubVerticalWorkspace;

class StreamHubVerticalPreview final : public QWidget {
public:
    explicit StreamHubVerticalPreview(StreamHubVerticalWorkspace *workspace, QWidget *parent = nullptr);
    ~StreamHubVerticalPreview() override;
    void SetCanvasUuid(std::string uuid);
    void SetSelectedItemId(long long id) { selectedItemId_ = id; guidesActive_=id>=0; update(); }
    bool DisplayReady() const { return display_ != nullptr; }
    bool HasRenderedSource() const { return hasRenderedSource_.load(); }

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPaintEngine *paintEngine() const override { return nullptr; }
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    static void Draw(void *param, uint32_t cx, uint32_t cy);
    void CreateDisplay();
    void DestroyDisplay();
    void ResizeDisplay();
    bool CanvasPoint(const QPointF &point, QPointF &canvasPoint) const;
    bool MoveSelectedItem(const QPointF &point);
    bool ResizeSelectedItem(const QPointF &point);
    QRectF SelectedItemBounds();
    obs_sceneitem_t *FindTopItemAt(const QPointF &point);

    StreamHubVerticalWorkspace *workspace_ = nullptr;
    obs_display_t *display_ = nullptr;
    std::string canvasUuid_;
    std::mutex sceneMutex_;
    std::atomic<long long> selectedItemId_{-1};
    std::atomic<bool> hasRenderedSource_{false};
    bool dragging_ = false;
    bool resizing_ = false;
    std::atomic<bool> guidesActive_{false};
    QPointF dragStart_;
    QPointF startPosition_;
    QPointF startScale_;
    QRectF lastItemBounds_;
    QLabel *distanceLabels_[4]{};
    int resizeHandle_ = 8;
    obs_transform_info startInfo_{};
    obs_sceneitem_crop startCrop_{};
    QTransform startBox_;
    QByteArray undoBefore_;
    std::map<long long, QPointF> dragPositions_;
    std::map<long long, obs_transform_info> dragTransforms_;
    std::map<long long, QTransform> dragParents_;
    std::vector<OBSSceneItem> deferredGroups_;
    QRubberBand *selectionBand_ = nullptr;
    QPoint selectionStart_;
    bool boxSelecting_ = false;
    bool centerSnapX_ = false, centerSnapY_ = false;
    std::atomic<double> zoom_{1}, panX_{0}, panY_{0};
    bool panning_=false;
    QPointF panStart_;
    double ViewScale() const { return (std::min)(width()/1080.0,height()/1920.0)*zoom_.load(); }
    void FinishEdit();
    int HandleAt(const QPointF &point);
    Qt::CursorShape HandleCursor(int handle);
    void UpdateCursor(const QPointF &point);
};
