// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTDEBUGPANEL_H
#define REDPANDA_RUSTDEBUGPANEL_H
#include <QWidget>
#include <QPointer>
#include <QHash>
#include "rustdebugsession.h"
#include "rustvariablepages.h"
class QTreeWidget;class QTreeWidgetItem;class QComboBox;class QLineEdit;class QPlainTextEdit;class QPushButton;
class RustDebugPanel final:public QWidget{
    Q_OBJECT
public:
    RustDebugPanel(RustDebugSession *session,const QString &settingsFile,QWidget *parent=nullptr);
    void addWatch(const QString &expression);
    void showAssembly();
    void addDataBreakpoint();
    void configureFunctionBreakpoints();
signals:
    void sourceBreakpointsRequested();
    void sourceRequested(const QString &file,int line,int character);
private:
    struct Pending {QString kind;int epoch=0,target=0;QPointer<QPlainTextEdit> viewer;int start=0;};
    void resetRequests();
    void refresh();
    void stack(int thread);
    void selectFrame(QTreeWidgetItem *item);
    void variables(QTreeWidgetItem *item);
    void receive(int id,const QString &command,const QJsonObject &body,bool success,const QString &message);
    void fillVariables(QTreeWidgetItem *parent,const QJsonArray &items,int parentReference,int start=0);
    void forgetItems(QTreeWidgetItem *item);
    int registerItem(QTreeWidgetItem *item);
    void saveWatches();
    void appendOutput(const QString &text);
    void inspectMemory();
    QPointer<RustDebugSession> mSession;
    QTreeWidget *mStack,*mVariables,*mWatches;
    QComboBox *mThreads;
    QPlainTextEdit *mConsole;
    QLineEdit *mWatchExpression,*mInput,*mEvaluate;
    QPushButton *mMemory,*mAssembly,*mDataBreakpoint,*mFunctions;
    QHash<int,Pending> mPending;
    QHash<int,QTreeWidgetItem *> mItems;
    RustVariablePages mVariablePages;
    QStringList mWatchList;
    QJsonArray mDataBreakpoints,mFunctionBreakpoints;
    QString mSettingsFile,mInstructionPointer,mEscape;
    int mEpoch=0,mNextItem=1,mFrame=0,mThread=0;
};
#endif
