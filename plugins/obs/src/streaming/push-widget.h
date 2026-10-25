#pragma once
#include "pch.h"
#include <json.hpp>
#include <functional>

class PushWidget : virtual public QWidget {
public:
    virtual ~PushWidget() {}
    virtual bool ShowEditDlg() = 0;
    virtual void StartStreaming() = 0;
    virtual void StopStreaming() = 0;
    virtual void OnOBSEvent(obs_frontend_event ev) = 0;
    virtual QPushButton* GetDeleteButton() = 0;
    virtual QPushButton* GetEditButton() = 0;
    virtual QPushButton* GetStreamButton() = 0;
    virtual void SetStreamAction(std::function<void()> action) { (void)action; }
    virtual QString StreamStatus() const { return {}; }
    virtual void SetLinkedStatus(const QString &text, bool busy) { (void)text; (void)busy; }
    virtual void ReloadConfig() = 0;
    virtual bool IsEnabledForAll() const = 0;
    virtual bool IsEnabledForLinkedStart() const { return IsEnabledForAll(); }
    virtual bool IsRunningForAggregate() const = 0;
    virtual bool IsBusy() const { return IsRunningForAggregate(); }
    virtual double CurrentBitrateBps() const = 0;
    virtual nlohmann::json CaptureMainEncoding() { return {}; }
};

PushWidget* createPushWidget(const std::string& targetId, QWidget* parent = 0);
