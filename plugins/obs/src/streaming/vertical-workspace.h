#pragma once

#include <obs.h>
#include <QObject>
#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <functional>
#include <string>

class QWidget;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QToolButton;
class QCheckBox;
class StreamHubVerticalPreview;

class StreamHubVerticalWorkspace final : public QObject {
public:
    explicit StreamHubVerticalWorkspace(std::function<void(bool)> horizontalStreamAction,
                                        std::function<void()> stopAuxiliaryOutputs, QObject *parent = nullptr);
    QWidget *CreateCanvasPanel(QWidget *parent = nullptr);
    QWidget *CreateScenesPanel(QWidget *parent = nullptr);
    QWidget *CreateSourcesPanel(QWidget *parent = nullptr);
    void RefreshFromConfig();
    bool HorizontalOnlyMode() const { return horizontalOnlyMode_; }
    void SetHorizontalOnlyMode(bool active) { horizontalOnlyMode_ = active; }
    const std::string &TargetId() const { return targetId_; }
    obs_scene_t *SelectedScene() const;
    void SelectSourceItem(long long id);
    void RefreshSources();
    void ReorderSelectedSource(bool up);
    void NudgeSelectedSources(const QPointF &delta);
    void SourceCommand(const QString &command);
    void SceneCommand(const QString &command);

private:
    friend class StreamHubVerticalPreview;
    void BuildScenesPanel(QWidget *parent);
    void BuildSourcesPanel(QWidget *parent);
    void UpdateSceneSyncButton();
    void SetSceneSyncEnabled(bool enabled);
    void RefreshScenes();
    void SelectScene();
    void CreateScene(bool copyMain);
    void DeleteVerticalScene();
    void MoveSelectedScene(bool up);
    void SetSelectedSceneHidden(bool hidden);
    void ShowSceneContextMenu(const QPoint &position);
    void ShowSourceContextMenu(const QPoint &position);
    void AddSource();
    void RemoveSelectedSource();
    void OpenSelectedFilters(bool sceneFilters);
    void EditSource();
    void SetSourceVisible(QListWidgetItem *row, bool visible);
    void SetSourceLocked(QListWidgetItem *row, bool locked);
    bool MoveItem(long long id, const QPointF &position);
    bool ResizeItem(long long id, const QPointF &scale);
    QRectF ItemBounds(long long id) const;
    void SetSelectedSceneUuid(std::string uuid);
    void StartHorizontalOnly();
    void StopHorizontalOnly();
    void ToggleRecording();

    std::function<void(bool)> horizontalStreamAction_;
    std::function<void()> stopAuxiliaryOutputs_;
    std::string targetId_;
    std::string selectedSceneUuid_;
    long long selectedSourceId_ = -1;
    bool horizontalOnlyMode_ = false;
    QWidget *canvasPanel_ = nullptr;
    StreamHubVerticalPreview *preview_ = nullptr;
    QListWidget *scenes_ = nullptr;
    QListWidget *sources_ = nullptr;
    QLabel *status_ = nullptr;
    QLabel *sceneStatus_ = nullptr;
    QLabel *sourceStatus_ = nullptr;
    QToolButton *deleteSceneButton_ = nullptr;
    QToolButton *sceneFiltersButton_ = nullptr;
    QToolButton *sceneSyncButton_ = nullptr;
    QToolButton *sceneUpButton_ = nullptr;
    QToolButton *sceneDownButton_ = nullptr;
    QToolButton *editSourceButton_ = nullptr;
    QToolButton *sourceFiltersButton_ = nullptr;
    QToolButton *deleteSourceButton_ = nullptr;
    QToolButton *moveUpButton_ = nullptr;
    QToolButton *moveDownButton_ = nullptr;
    QCheckBox *showHiddenScenes_ = nullptr;
    QByteArray sourceClipboard_;
    QString filterClipboard_;
    unsigned long editorRevision_ = 0;
};
