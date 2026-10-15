#include "streamhub-native-theme.h"

#include <QApplication>
#include <QEvent>
#include <QObject>
#include <QWidget>

#ifdef _WIN32
#include <Windows.h>

namespace {
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
    const HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (!module) return;
    using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    const auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(module, "DwmSetWindowAttribute"));
    if (setAttribute) {
        const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
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
            if (auto *widget = qobject_cast<QWidget *>(watched)) ApplyNativeTitleBar(widget);
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
    qApp->installEventFilter(new NativeThemeFilter(qApp));
    for (QWidget *widget : qApp->topLevelWidgets()) ApplyNativeTitleBar(widget);
#endif
}
