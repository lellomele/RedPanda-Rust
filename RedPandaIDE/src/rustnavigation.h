// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTNAVIGATION_H
#define REDPANDA_RUSTNAVIGATION_H
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QHash>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include "rusttextedits.h"
class Editor;class EditorManager;class RustLspClient;class QTreeWidget;class QTreeWidgetItem;class QWidget;
class RustNavigation final:public QObject{
    Q_OBJECT
public:
    RustNavigation(EditorManager *manager,RustLspClient *client,QTreeWidget *outline,QWidget *window,
        const std::function<Editor *(const QString &)> &openFile);
    void attachEditors();
    void references();
    void rename();
    int renameTo(const QString &name);
    void searchSymbols();
    void macroExpansion();
    void codeActions();
    bool applyEdits(const QJsonObject &workspaceEdit,Editor *source,const QHash<QString,QString> &snapshot,bool preview,const QString &oldName={});
signals:
    void status(const QString &text);
    void referencesReady(const QJsonArray &locations);
    void generatedText(const QString &title,const QString &text);
    void editsApplied(int documents,int edits);
private:
    struct Pending {QString method,text,oldName;QPointer<Editor> editor;QHash<QString,QString> snapshot;};
    Editor *active()const;
    bool synchronize();
    int request(const QString &method,const QJsonObject &extra={},bool workspaceEdit=false);
    void receive(int id,const QString &method,const QString &path,int version,const QJsonValue &value);
    void refreshOutline();
    void fillOutline(const QJsonArray &symbols,QTreeWidgetItem *parent,int depth,int &count);
    EditorManager *mManager;
    QPointer<RustLspClient> mClient;
    QPointer<QWidget> mWindow;
    QTreeWidget *mOutline;
    std::function<Editor *(const QString &)> mOpen;
    QTimer mRefresh;
    QHash<int,Pending> mPending;
    QHash<QString,QString> mSnapshot;
    QString mOutlinePath;
};
#endif
