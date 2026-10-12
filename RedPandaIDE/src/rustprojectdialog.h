// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTPROJECTDIALOG_H
#define REDPANDA_RUSTPROJECTDIALOG_H
#include <QDialog>
#include <QProcess>
#include <QTimer>
#include <QJsonArray>
#include "rustprojectoptions.h"
#include "processjob.h"
class QComboBox;class QLineEdit;class QPlainTextEdit;class QCheckBox;class QLabel;
class RustProjectDialog final:public QDialog{
    Q_OBJECT
public:
    RustProjectDialog(const QString &root,QWidget *parent=nullptr);
    ~RustProjectDialog() override;
private:
    void save();
    void readMetadata();
    void populateTargets();
    QString mRoot;
    QComboBox *mProfile,*mPackage,*mKind,*mTarget;
    QLineEdit *mTriple,*mFeatures,*mDirectory;
    QPlainTextEdit *mArguments,*mEnvironment,*mFlags;
    QCheckBox *mAllFeatures,*mNoDefault;
    QLabel *mStatus;
    ProcessJob mMetadataJob;
    QProcess mMetadata;
    QTimer mDeadline;
    QByteArray mMetadataBytes;
    QJsonArray mPackages;
};
#endif
