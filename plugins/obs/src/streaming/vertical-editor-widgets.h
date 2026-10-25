#pragma once

#include "obs.hpp"
#include <graphics/matrix4.h>

#include <QHBoxLayout>
#include <QCheckBox>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QStyle>
#include <QTransform>
#include <QApplication>
#include <QResizeEvent>
#include <QDropEvent>
#include <QDrag>
#include <QMimeData>
#include <cstring>
#include <functional>

class StreamHubEditorList final : public QListWidget {
public:
    explicit StreamHubEditorList(QWidget *parent):QListWidget(parent) {
        setDragDropMode(QAbstractItemView::InternalMove);setDefaultDropAction(Qt::MoveAction);
        setDragEnabled(true);setAcceptDrops(true);setDropIndicatorShown(true);
    }
    std::function<void(int,int)> reordered;
protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(event->mimeData()->hasFormat("application/x-streamhub-source"))event->acceptProposedAction();
        else QListWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if(event->mimeData()->hasFormat("application/x-streamhub-source"))event->acceptProposedAction();
        else QListWidget::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        const int from=currentRow();auto *target=itemAt(event->position().toPoint());
        const int to=target?row(target):count()-1;
        if(from>=0&&to>=0&&from!=to&&reordered){reordered(from,to);event->acceptProposedAction();}
        else event->ignore();
    }
};

inline QTransform StreamHubItemBox(obs_sceneitem_t *item)
{
    matrix4 matrix;
    obs_sceneitem_get_box_transform(item, &matrix);
    return QTransform(matrix.x.x, matrix.x.y, matrix.y.x, matrix.y.y, matrix.t.x, matrix.t.y);
}

class StreamHubVerticalSourceRow final : public QWidget {
public:
    StreamHubVerticalSourceRow(QListWidget *list, QListWidgetItem *item, const QIcon &icon,
                               const QString &name, bool visible, bool locked,
                               std::function<void(bool)> visibilityChanged,
                               std::function<void(bool)> lockChanged, QWidget *parent = nullptr)
        : QWidget(parent), list_(list), item_(item)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setAutoFillBackground(true);
        setBackgroundRole(QPalette::Base);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(4, 0, 4, 0);
        layout->setSpacing(3);

        auto *iconLabel = new QLabel(this);
        iconLabel->setProperty("class", "source-icon");
        QPixmap sourcePixmap = icon.pixmap(QSize(16, 16));
        QPainter tintPainter(&sourcePixmap);
        tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tintPainter.fillRect(sourcePixmap.rect(), QColor(Qt::white));
        tintPainter.end();
        iconLabel->setPixmap(sourcePixmap);
        iconLabel->setEnabled(visible);
        layout->addWidget(iconLabel);

        auto *nameLabel = new QLabel(name, this);
        nameLabel->setAttribute(Qt::WA_TranslucentBackground);
        nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        nameLabel->setEnabled(visible);
        layout->addWidget(nameLabel, 1);

        auto *visibility = new QCheckBox(this);
        visibility->setProperty("class", "checkbox-icon indicator-visibility");
        visibility->setChecked(visible);
        visibility->setAccessibleName(tr("Visibilidade"));
        visibility->setAccessibleDescription(tr("Mostrar ou ocultar %1").arg(name));
        layout->addWidget(visibility);

        auto *lock = new QCheckBox(this);
        lock->setProperty("class", "checkbox-icon indicator-lock");
        lock->setChecked(locked);
        lock->setAccessibleName(tr("Bloqueio"));
        lock->setAccessibleDescription(tr("Bloquear ou desbloquear %1 no canvas").arg(name));
        layout->addWidget(lock);

        connect(visibility, &QCheckBox::toggled, this, [this, nameLabel, iconLabel, callback = std::move(visibilityChanged)](bool checked) {
            if (list_ && item_) list_->setCurrentItem(item_);
            nameLabel->setEnabled(checked);
            iconLabel->setEnabled(checked);
            callback(checked);
        });
        connect(lock, &QCheckBox::toggled, this, [this, callback = std::move(lockChanged)](bool checked) {
            if (list_ && item_) list_->setCurrentItem(item_);
            callback(checked);
        });
    }

    void SetSelected(bool selected)
    {
        QPalette rowPalette = palette();
        rowPalette.setColor(QPalette::Window, selected ? rowPalette.color(QPalette::Highlight)
                                                       : rowPalette.color(QPalette::Base));
        setPalette(rowPalette);
        for (auto *label : findChildren<QLabel *>()) {
            QPalette labelPalette = label->palette();
            labelPalette.setColor(QPalette::WindowText, selected ? rowPalette.color(QPalette::HighlightedText)
                                                                 : rowPalette.color(QPalette::Text));
            label->setPalette(labelPalette);
        }
        setProperty("selected", selected);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (list_ && item_ && event->button() == Qt::LeftButton) {
            pressPosition_=event->position().toPoint();
            list_->setCurrentItem(item_, (event->modifiers() & Qt::ControlModifier)
                ? QItemSelectionModel::Toggle : (event->modifiers() & Qt::ShiftModifier)
                ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect);
            list_->setFocus(Qt::MouseFocusReason);event->accept();return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if((event->buttons()&Qt::LeftButton)&&(event->position().toPoint()-pressPosition_).manhattanLength()>=QApplication::startDragDistance()) {
            QDrag drag(this);auto *mime=new QMimeData;mime->setData("application/x-streamhub-source",QByteArray::number(item_->data(Qt::UserRole).toLongLong()));
            drag.setMimeData(mime);drag.exec(Qt::MoveAction);return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        QWidget::paintEvent(event);
    }

private:
    QPoint pressPosition_;
    QListWidget *list_ = nullptr;
    QListWidgetItem *item_ = nullptr;
};

inline QIcon StreamHubVerticalSourceIcon(obs_source_t *source)
{
    if (!source)
        return QIcon(QStringLiteral(":/res/images/sources/default.svg"));
    const char *id = obs_source_get_id(source);
    if (strcmp(id, "scene") == 0)
        return QIcon(QStringLiteral(":/res/images/sources/scene.svg"));
    if (strcmp(id, "group") == 0)
        return QIcon(QStringLiteral(":/res/images/sources/group.svg"));

    const char *icon = "default";
    switch (obs_source_get_icon_type(id)) {
    case OBS_ICON_TYPE_IMAGE: icon = "image"; break;
    case OBS_ICON_TYPE_COLOR: icon = "brush"; break;
    case OBS_ICON_TYPE_SLIDESHOW: icon = "slideshow"; break;
    case OBS_ICON_TYPE_AUDIO_INPUT:
    case OBS_ICON_TYPE_AUDIO_OUTPUT:
    case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT: icon = "microphone"; break;
    case OBS_ICON_TYPE_DESKTOP_CAPTURE:
    case OBS_ICON_TYPE_WINDOW_CAPTURE: icon = "window"; break;
    case OBS_ICON_TYPE_GAME_CAPTURE: icon = "gamepad"; break;
    case OBS_ICON_TYPE_CAMERA: icon = "camera"; break;
    case OBS_ICON_TYPE_TEXT: icon = "text"; break;
    case OBS_ICON_TYPE_MEDIA: icon = "media"; break;
    case OBS_ICON_TYPE_BROWSER: icon = "globe"; break;
    default: break;
    }
    return QIcon(QStringLiteral(":/res/images/sources/") + QString::fromLatin1(icon) + QStringLiteral(".svg"));
}

class StreamHubVerticalPreviewArea final : public QWidget {
public:
    explicit StreamHubVerticalPreviewArea(QWidget *parent = nullptr) : QWidget(parent) {}
    void SetPreview(QWidget *preview) { preview_ = preview; }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (!preview_) return;
        constexpr int widthRatio = 9;
        constexpr int heightRatio = 16;
        int previewWidth = width();
        int previewHeight = previewWidth * heightRatio / widthRatio;
        if (previewHeight > height()) {
            previewHeight = height();
            previewWidth = previewHeight * widthRatio / heightRatio;
        }
        preview_->setGeometry((width() - previewWidth) / 2, (height() - previewHeight) / 2,
                              previewWidth, previewHeight);
    }

private:
    QWidget *preview_ = nullptr;
};
