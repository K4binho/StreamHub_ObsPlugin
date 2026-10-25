

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

QWidget *StreamHubVerticalWorkspace::CreateCanvasPanel(QWidget *parent)
{
    canvasPanel_ = new QWidget(parent);
    canvasPanel_->setMinimumSize(0, 0);
    canvasPanel_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    canvasPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *root = new QVBoxLayout(canvasPanel_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *previewArea = new StreamHubVerticalPreviewArea(canvasPanel_);
    previewArea->setMinimumSize(0, 0);
    previewArea->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    previewArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview_ = new StreamHubVerticalPreview(this, previewArea);
    previewArea->SetPreview(preview_);
    root->addWidget(previewArea, 1);
    RefreshFromConfig();
    return canvasPanel_;
}

QWidget *StreamHubVerticalWorkspace::CreateScenesPanel(QWidget *parent)
{
    auto *panel = new QWidget(parent);
    panel->setMinimumSize(0, 0);
    panel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    BuildScenesPanel(panel);
    return panel;
}

QWidget *StreamHubVerticalWorkspace::CreateSourcesPanel(QWidget *parent)
{
    auto *panel = new QWidget(parent);
    panel->setMinimumSize(0, 0);
    panel->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    BuildSourcesPanel(panel);
    return panel;
}

void StreamHubVerticalWorkspace::BuildScenesPanel(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    sceneStatus_ = new QLabel(tr("As cenas acompanham as cenas principais."), parent);
    sceneStatus_->setWordWrap(true);
    sceneStatus_->setVisible(false);
    auto *sceneList = new StreamHubEditorList(parent); scenes_=sceneList;
    sceneList->reordered=[this](int from,int to){
        const int step=from<to?1:-1;
        while(scenes_->currentRow()!=to){const int previous=scenes_->currentRow();MoveSelectedScene(step<0);if(scenes_->currentRow()==previous)break;}
    };
    scenes_->setObjectName(QStringLiteral("SceneTree"));
    scenes_->setMinimumSize(0, 0);
    scenes_->setSelectionMode(QAbstractItemView::SingleSelection);
    scenes_->setDragEnabled(true);
    scenes_->setAlternatingRowColors(false);
    scenes_->setUniformItemSizes(true);
    scenes_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scenes_->setContextMenuPolicy(Qt::CustomContextMenu);
    for(const auto &[key,command]:std::initializer_list<std::pair<const char *,const char *>>{{"F2","rename"},{"Ctrl+D","duplicate"},{"Delete","remove"}}) {
        auto *shortcut=new QShortcut(QKeySequence(QString::fromLatin1(key)),scenes_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,[this,command](){SceneCommand(QString::fromLatin1(command));});
    }
    layout->addWidget(scenes_, 1);
    auto *toolbar = new QToolBar(parent);
    toolbar->setObjectName(QStringLiteral("scenesToolbar"));
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setFloatable(false);
    toolbar->setMovable(false);
    auto *create = new QToolButton(parent);
    create->setProperty("class", "icon-plus");
    create->setAutoRaise(true);
    create->setToolTip(tr("Criar cena somente vertical"));
    auto *copy = new QToolButton(parent);
    copy->setProperty("class", "icon-refresh");
    copy->setAutoRaise(true);
    copy->setToolTip(tr("Criar uma cópia da cena principal no canvas vertical"));
    sceneFiltersButton_ = new QToolButton(parent);
    sceneFiltersButton_->setProperty("class", "icon-filter");
    sceneFiltersButton_->setAutoRaise(true);
    sceneFiltersButton_->setToolTip(tr("Filtros da cena vertical selecionada"));
    deleteSceneButton_ = new QToolButton(parent);
    deleteSceneButton_->setProperty("class", "icon-trash");
    deleteSceneButton_->setAutoRaise(true);
    deleteSceneButton_->setToolTip(tr("Remover a cena do canvas vertical"));
    sceneUpButton_ = new QToolButton(parent);
    sceneUpButton_->setProperty("class", "icon-up");
    sceneUpButton_->setAutoRaise(true);
    sceneUpButton_->setToolTip(tr("Subir cena na lista vertical"));
    sceneDownButton_ = new QToolButton(parent);
    sceneDownButton_->setProperty("class", "icon-down");
    sceneDownButton_->setAutoRaise(true);
    sceneDownButton_->setToolTip(tr("Descer cena na lista vertical"));
    toolbar->addWidget(create);
    toolbar->addWidget(copy);
    toolbar->addSeparator();
    toolbar->addWidget(sceneFiltersButton_);
    toolbar->addWidget(deleteSceneButton_);
    toolbar->addWidget(sceneUpButton_);
    toolbar->addWidget(sceneDownButton_);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    showHiddenScenes_ = new QCheckBox(parent);
    showHiddenScenes_->setProperty("class", "checkbox-icon indicator-visibility");
    showHiddenScenes_->setToolTip(tr("Exibir cenas ocultas nesta lista vertical"));
    showHiddenScenes_->setAccessibleName(tr("Exibir cenas ocultas"));
    toolbar->addWidget(showHiddenScenes_);
    sceneSyncButton_ = new QToolButton(parent);
    sceneSyncButton_->setCheckable(true);
    sceneSyncButton_->setIconSize(QSize(16, 16));
    sceneSyncButton_->setAutoRaise(true);
    toolbar->addWidget(sceneSyncButton_);
    layout->addWidget(toolbar);
    connect(scenes_, &QListWidget::currentItemChanged, parent, [this]() { SelectScene(); });
    connect(create, &QToolButton::clicked, parent, [this]() { CreateScene(false); });
    connect(copy, &QToolButton::clicked, parent, [this]() { CreateScene(true); });
    connect(sceneFiltersButton_, &QToolButton::clicked, parent, [this]() { OpenSelectedFilters(true); });
    connect(deleteSceneButton_, &QToolButton::clicked, parent, [this]() { DeleteVerticalScene(); });
    connect(sceneUpButton_, &QToolButton::clicked, parent, [this]() { MoveSelectedScene(true); });
    connect(sceneDownButton_, &QToolButton::clicked, parent, [this]() { MoveSelectedScene(false); });
    connect(sceneSyncButton_, &QToolButton::toggled, parent, [this](bool enabled) { SetSceneSyncEnabled(enabled); });
    connect(showHiddenScenes_, &QCheckBox::toggled, parent, [this]() { RefreshScenes(); });
    connect(scenes_, &QListWidget::customContextMenuRequested, parent, [this](const QPoint &point) { ShowSceneContextMenu(point); });
    connect(scenes_, &QListWidget::currentItemChanged, parent, [this]() {
        if (!scenes_ || !scenes_->currentItem()) return;
        const bool verticalOnly = scenes_->currentItem()->data(Qt::UserRole + 1).toBool();
        const auto target = FindById(GlobalMultiOutputConfig().targets, targetId_);
        const bool follow = target && target->sceneMode == "follow";
        if (deleteSceneButton_) deleteSceneButton_->setEnabled(!follow || verticalOnly);
        if (sceneFiltersButton_) sceneFiltersButton_->setEnabled(true);
        if (sceneUpButton_) sceneUpButton_->setEnabled(!follow || verticalOnly);
        if (sceneDownButton_) sceneDownButton_->setEnabled(!follow || verticalOnly);
    });
    UpdateSceneSyncButton();
}

void StreamHubVerticalWorkspace::BuildSourcesPanel(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    sourceStatus_ = new QLabel(tr("Selecione uma cena para editar suas fontes."), parent);
    sourceStatus_->setWordWrap(true);
    sourceStatus_->setVisible(false);
    auto *sourceList = new StreamHubEditorList(parent); sources_=sourceList;
    sourceList->reordered=[this](int from,int to){
        auto *scene=SelectedScene();if(!scene)return;
        auto *item=StreamHubFindEditorItem(scene,sources_->item(from)->data(Qt::UserRole).toLongLong());
        auto *target=StreamHubFindEditorItem(scene,sources_->item(to)->data(Qt::UserRole).toLongLong());
        const auto before=StreamHubSceneState(scene);
        if(item&&target&&StreamHubEditable(scene,item)) {
            if(obs_sceneitem_get_scene(item)==obs_sceneitem_get_scene(target))
                obs_sceneitem_set_order_position(item,obs_sceneitem_get_order_position(target));
        }
        StreamHubRecordEdit(tr("Ordenar fontes verticais"),before,scene);obs_scene_release(scene);
        QTimer::singleShot(0,this,[this](){RefreshSources();});
    };
    sources_->setObjectName(QStringLiteral("SourceTree"));
    sources_->setMinimumSize(0, 0);
    sources_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sources_->setDragEnabled(true);
    sources_->setAlternatingRowColors(false);
    sources_->setUniformItemSizes(true);
    sources_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sources_->setContextMenuPolicy(Qt::CustomContextMenu);
    const std::pair<const char *, const char *> shortcuts[] = {
        {"Ctrl+C", "copy"}, {"Ctrl+V", "paste"}, {"Ctrl+Shift+V", "pasteDuplicate"},
        {"Ctrl+D", "duplicate"}, {"F2", "rename"}, {"Delete", "remove"},
        {"Ctrl+G", "group"}, {"Ctrl+Shift+G", "ungroup"}, {"Ctrl+F", "fit"},
        {"Ctrl+R", "reset"}, {"Ctrl+Shift+F", "stretch"}};
    for (const auto &[key, command] : shortcuts) {
        auto *shortcut = new QShortcut(QKeySequence(QString::fromLatin1(key)), sources_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, command]() { SourceCommand(QString::fromLatin1(command)); });
    }
    const std::pair<const char *,QPointF> nudges[]={{"Left",{-1,0}},{"Right",{1,0}},{"Up",{0,-1}},{"Down",{0,1}},
        {"Shift+Left",{-10,0}},{"Shift+Right",{10,0}},{"Shift+Up",{0,-10}},{"Shift+Down",{0,10}}};
    for(const auto &[key,delta]:nudges){
        auto *shortcut=new QShortcut(QKeySequence(QString::fromLatin1(key)),sources_);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut,&QShortcut::activated,this,[this,delta](){NudgeSelectedSources(delta);});
    }
    layout->addWidget(sources_, 1);
    auto *toolbar = new QToolBar(parent);
    toolbar->setObjectName(QStringLiteral("sourcesToolbar"));
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setFloatable(false);
    toolbar->setMovable(false);
    auto *add = new QToolButton(parent);
    add->setProperty("class", "icon-plus");
    add->setAutoRaise(true);
    add->setToolTip(tr("Adicionar fonte"));
    sourceFiltersButton_ = new QToolButton(parent);
    sourceFiltersButton_->setProperty("class", "icon-filter");
    sourceFiltersButton_->setAutoRaise(true);
    sourceFiltersButton_->setToolTip(tr("Filtros da fonte selecionada"));
    editSourceButton_ = new QToolButton(parent);
    editSourceButton_->setProperty("class", "icon-gear");
    editSourceButton_->setAutoRaise(true);
    editSourceButton_->setToolTip(tr("Transformar fonte selecionada"));
    deleteSourceButton_ = new QToolButton(parent);
    deleteSourceButton_->setProperty("class", "icon-trash");
    deleteSourceButton_->setAutoRaise(true);
    deleteSourceButton_->setToolTip(tr("Remover fonte desta cena vertical"));
    moveUpButton_ = new QToolButton(parent);
    moveUpButton_->setProperty("class", "icon-up");
    moveUpButton_->setAutoRaise(true);
    moveUpButton_->setToolTip(tr("Subir camada"));
    moveDownButton_ = new QToolButton(parent);
    moveDownButton_->setProperty("class", "icon-down");
    moveDownButton_->setAutoRaise(true);
    moveDownButton_->setToolTip(tr("Descer camada"));
    toolbar->addWidget(add);
    toolbar->addWidget(sourceFiltersButton_);
    toolbar->addWidget(editSourceButton_);
    toolbar->addWidget(deleteSourceButton_);
    toolbar->addSeparator();
    toolbar->addWidget(moveUpButton_);
    toolbar->addWidget(moveDownButton_);
    layout->addWidget(toolbar);
    connect(add, &QToolButton::clicked, parent, [this]() { AddSource(); });
    connect(sourceFiltersButton_, &QToolButton::clicked, parent, [this]() { OpenSelectedFilters(false); });
    connect(editSourceButton_, &QToolButton::clicked, parent, [this]() { EditSource(); });
    connect(deleteSourceButton_, &QToolButton::clicked, parent, [this]() { RemoveSelectedSource(); });
    connect(moveUpButton_, &QToolButton::clicked, parent, [this]() { ReorderSelectedSource(true); });
    connect(moveDownButton_, &QToolButton::clicked, parent, [this]() { ReorderSelectedSource(false); });
    connect(sources_, &QListWidget::customContextMenuRequested, parent, [this](const QPoint &point) { ShowSourceContextMenu(point); });
    connect(sources_, &QListWidget::currentItemChanged, parent, [this]() {
        for (int i = 0; i < sources_->count(); ++i) {
            auto *item = sources_->item(i);
            if (auto *row = dynamic_cast<StreamHubVerticalSourceRow *>(sources_->itemWidget(item)))
                row->SetSelected(item->isSelected());
        }
        selectedSourceId_ = sources_ && sources_->currentItem()
            ? sources_->currentItem()->data(Qt::UserRole).toLongLong() : -1;
        const bool selected = selectedSourceId_ >= 0;
        if (sourceFiltersButton_) sourceFiltersButton_->setEnabled(selected);
        if (editSourceButton_) editSourceButton_->setEnabled(selected);
        if (deleteSourceButton_) deleteSourceButton_->setEnabled(selected);
        if (moveUpButton_) moveUpButton_->setEnabled(selected && sources_->currentRow() > 0);
        if (moveDownButton_) moveDownButton_->setEnabled(selected && sources_->currentRow() + 1 < sources_->count());
        if (preview_)
            preview_->SetSelectedItemId(selectedSourceId_);
    });
    connect(sources_, &QListWidget::itemSelectionChanged, parent, [this]() {
        auto *scene = SelectedScene();
        if (!scene) return;
        for (int i = 0; i < sources_->count(); ++i) {
            auto *row = sources_->item(i);
            if (auto *item = StreamHubFindEditorItem(scene, row->data(Qt::UserRole).toLongLong()))
                obs_sceneitem_select(item, row->isSelected());
            if (auto *widget = dynamic_cast<StreamHubVerticalSourceRow *>(sources_->itemWidget(row)))
                widget->SetSelected(row->isSelected());
        }
        obs_scene_release(scene);
        auto *current=sources_->currentItem();
        selectedSourceId_=current && current->isSelected() ? current->data(Qt::UserRole).toLongLong() : -1;
        if(preview_) preview_->SetSelectedItemId(selectedSourceId_);
    });
}
