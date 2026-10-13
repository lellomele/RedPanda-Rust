// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTCLEANDIALOG_H
#define REDPANDA_RUSTCLEANDIALOG_H
#include <QDialog>
#include <QProcess>
#include <QTimer>
#include "processjob.h"
#include "rustcleanplan.h"
#include <functional>
class QLabel;class QPlainTextEdit;class QPushButton;
class RustCleanDialog final:public QDialog{
    Q_OBJECT
public:
    RustCleanDialog(const QString &root,const QStringList &protectedFiles,QWidget *parent=nullptr);
    ~RustCleanDialog() override;
    void reject() override;
    void setProtectedFilesProvider(std::function<QStringList()> provider){mProtectedProvider=std::move(provider);}
    void protectOpenedFiles();
private:
    void readMetadata();void inspect();void recycle();
    QString mRoot,mWorkspace,mTarget;QStringList mProtected;
    ProcessJob mJob;QProcess mProcess;QTimer mDeadline;QByteArray mBytes;
    RustClean::Plan mPlan;QLabel *mStatus;QPlainTextEdit *mPreview;QPushButton *mClean,*mCancel;
    bool mMoving=false;
    std::shared_ptr<std::atomic_bool> mCancelMove;
    std::function<QStringList()> mProtectedProvider;
};
#endif
