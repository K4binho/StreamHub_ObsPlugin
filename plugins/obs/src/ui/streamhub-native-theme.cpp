#include "streamhub-native-theme.h"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QEvent>
#include <QLayout>
#include <QMainWindow>
#include <QObject>
#include <QWidget>
#include <obs-frontend-api.h>
#include <util/config-file.h>

#ifdef _WIN32
#include <Windows.h>

namespace {
void RestoreStartupLayout(QWidget *widget)
{
    auto *mainWindow = qobject_cast<QMainWindow *>(widget);
    if (!mainWindow || !mainWindow->inherits("OBSBasic") ||
        mainWindow->property("streamHubStartupLayoutRestored").toBool()) return;
    mainWindow->setProperty("streamHubStartupLayoutRestored", true);
    config_t *config = obs_frontend_get_user_config();
    if (!config) return;
    const char *saved = config_get_string(config, "BasicWindow", "DockState");
    if (!saved || !*saved) return;
    // OBS shows its window before restoring docks; restore before the first paint too.
    const QByteArray state = QByteArray::fromBase64(QByteArray(saved));
    if (!state.isEmpty() && mainWindow->restoreState(state) && mainWindow->layout())
        mainWindow->layout()->activate();
}

bool IsDirectPreview(QWidget *widget)
{
    return widget && widget->testAttribute(Qt::WA_PaintOnScreen);
}

void ClearPreviewBackground(QWidget *widget)
{
    if (!IsDirectPreview(widget) || !widget->internalWinId()) return;
    const HWND hwnd = reinterpret_cast<HWND>(widget->internalWinId());
    RECT rect;
    if (!GetClientRect(hwnd, &rect)) return;
    if (HDC dc = GetDC(hwnd)) {
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        ReleaseDC(hwnd, dc);
    }
}

class PreviewBackgroundFilter final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override
    {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_ERASEBKGND ||
            !IsDirectPreview(QWidget::find(reinterpret_cast<WId>(msg->hwnd)))) return false;
        RECT rect;
        if (!GetClientRect(msg->hwnd, &rect)) return false;
        FillRect(reinterpret_cast<HDC>(msg->wParam), &rect,
                 static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        *result = 1;
        return true;
    }
};

bool IsK4ThemeActive()
{
    const QString sheet = qApp ? qApp->styleSheet() : QString();
    return sheet.contains("K4binho", Qt::CaseInsensitive) ||
           (sheet.contains("#00C8FF", Qt::CaseInsensitive) &&
            sheet.contains("#080D14", Qt::CaseInsensitive));
}

void ApplyNativeTitleBar(QWidget *widget)
{
    if (!widget || !widget->isWindow() || !IsK4ThemeActive()) return;
    // Never create a native window while Qt is still restoring the startup UI.
    const WId nativeId = widget->internalWinId();
    if (!nativeId) return;
    const HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (!module) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    const auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(module, "DwmSetWindowAttribute"));
    if (setAttribute) {
        const HWND hwnd = reinterpret_cast<HWND>(nativeId);
        const BOOL enabled = TRUE;
        if (FAILED(setAttribute(hwnd, 20, &enabled, sizeof(enabled))))
            setAttribute(hwnd, 19, &enabled, sizeof(enabled));
        const COLORREF caption = RGB(16, 26, 38);
        const COLORREF border = RGB(0, 200, 255);
        const COLORREF text = RGB(241, 250, 255);
        setAttribute(hwnd, 35, &caption, sizeof(caption));
        setAttribute(hwnd, 34, &border, sizeof(border));
        setAttribute(hwnd, 36, &text, sizeof(text));
    }
    FreeLibrary(module);
}

class NativeThemeFilter final : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            if (auto *widget = qobject_cast<QWidget *>(watched)) {
                if (event->type() == QEvent::Show) RestoreStartupLayout(widget);
                ClearPreviewBackground(widget);
                ApplyNativeTitleBar(widget);
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
}
#endif

void StreamHubInstallNativeThemeHook()
{
#ifdef _WIN32
    if (!qApp || qApp->property("streamHubNativeThemeHook").toBool()) return;
    qApp->setProperty("streamHubNativeThemeHook", true);
    static PreviewBackgroundFilter previewBackgroundFilter;
    qApp->installNativeEventFilter(&previewBackgroundFilter);
    qApp->installEventFilter(new NativeThemeFilter(qApp));
    for (QWidget *widget : qApp->allWidgets()) ClearPreviewBackground(widget);
    for (QWidget *widget : qApp->topLevelWidgets()) ApplyNativeTitleBar(widget);
#endif
}
