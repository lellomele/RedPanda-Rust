// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTTOOLSWIDGET_H
#define RUSTTOOLSWIDGET_H
#include "settingswidget.h"
#include <QMap>
#include <QStringList>
class QLineEdit;class QLabel;class QComboBox;class QPushButton;class QProcess;class QTimer;
class RustToolsWidget final:public SettingsWidget {
    Q_OBJECT
public:
    RustToolsWidget(const QString &name,const QString &group,IconsManager *icons,QWidget *parent=nullptr);
    ~RustToolsWidget();
protected:
    void doLoad() override;
    void doSave() override;
private:
    void probeNext();
    QMap<QString,QLineEdit *> mPaths;
    QMap<QString,QLabel *> mResults;
    QComboBox *mEdition;
    QPushButton *mVerify;
    QProcess *mProbe;
    QTimer *mTimeout;
    QStringList mQueue;
    QString mCurrent;
    QString mProbeDescription;
    QByteArray mOutput;
    bool mTimedOut=false;
};
#endif
