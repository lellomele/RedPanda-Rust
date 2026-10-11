// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTEDITORSUPPORT_H
#define REDPANDA_RUSTEDITORSUPPORT_H
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include "rustlspclient.h"
class Editor;
class QListWidget;

class RustEditorSupport final : public QObject {
    Q_OBJECT
public:
    RustEditorSupport(Editor *editor,RustLspClient *client);
    ~RustEditorSupport();
    void complete(bool explicitRequest=true);
signals:
    void definitionRequested(const QString &path,int line,int character);
private:
    bool eventFilter(QObject *watched,QEvent *event) override;
    bool isRust() const;
    bool synchronize();
    void dismiss();
    void hover();
    void receive(int id,const QString &method,const QString &path,int version,const QJsonValue &value);
    void showCompletions(const QJsonValue &value);
    void acceptCompletion();
    void applyCompletion(const QJsonObject &item);
    void applyDiagnostics(const QString &path,int version,const QJsonArray &items);
    Editor *mEditor;
    QPointer<RustLspClient> mClient;
    QListWidget *mPopup;
    QTimer mSync,mComplete,mHover;
    QString mPath;
    QJsonArray mItems;
    QJsonObject mSelected;
    int mVersion=0,mCompletionId=0,mHoverId=0,mDefinitionId=0,mResolveId=0;
    quint64 mRevision=0,mSyncedRevision=0,mCompletionRevision=0,mHoverRevision=0,mDefinitionRevision=0;
    int mCompletionLine=0,mCompletionCharacter=0,mDefinitionLine=0,mDefinitionCharacter=0;
    QPoint mHoverPosition,mRequestedHover;
    bool mApplying=false,mExplicitPending=false;
};
#endif
