// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QPaintEvent>
#include <qsynedit/qsynedit.h>
#include <qsynedit/syntaxer/lua.h>
#include "common.h"
#include "rustcoordinates.h"
class PaintObserver:public QObject {
public:
    QList<QRect> regions;
    bool eventFilter(QObject *,QEvent *event) override {
        if(event->type()==QEvent::Paint)regions.append(static_cast<QPaintEvent *>(event)->region().boundingRect());
        return false;
    }
};
class RustSharedChecks:public QObject {
    Q_OBJECT
private slots:
    void issueDefaults(){CompileIssue issue;QCOMPARE(issue.line,0);QCOMPARE(issue.column,0);QCOMPARE(issue.endColumn,0);QCOMPARE(issue.type,CompileIssueType::Other);}
    void rustcScalarColumns(){
        const auto line=QString::fromUtf8("a🦀èz");QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,1),0);QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,2),1);QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,3),3);QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,5),5);
        QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,0),-1);QCOMPARE(RustCoordinates::scalarColumnToUtf16(line,6),-1);QCOMPARE(RustCoordinates::scalarColumnToUtf16(QString(QChar(0xd800)),2),-1);
    }
    void reversedGutterInvalidation(){
        QSynedit::QSynEdit editor;editor.resize(500,300);editor.setContent("a\nb\nc\nd\ne\nf\ng");
        PaintObserver observer;editor.viewport()->installEventFilter(&observer);editor.show();QTest::qWait(60);
        observer.regions.clear();editor.invalidateGutterLines(1,4);QTRY_VERIFY(!observer.regions.isEmpty());const auto forward=observer.regions.last();
        observer.regions.clear();editor.invalidateGutterLines(4,1);QTRY_VERIFY(!observer.regions.isEmpty());QCOMPARE(observer.regions.last(),forward);
        observer.regions.clear();editor.invalidateGutterLines(1,INT_MAX);QTRY_VERIFY(!observer.regions.isEmpty());QCOMPARE(observer.regions.last().bottom(),editor.viewport()->rect().bottom());
        editor.invalidateGutterLines(INT_MIN,INT_MAX);QTest::qWait(20);editor.invalidateGutterLines(INT_MAX,INT_MAX);QTest::qWait(20);
    }
    void luaExponent_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<bool>("valid");
        for(const auto &s:QStringList{"1e2","1.e2","1.0e2","1e+2","0.5E-3"})QTest::newRow(qPrintable(s))<<s<<true;
        for(const auto &s:QStringList{"1ee2","1.e.e2"})QTest::newRow(qPrintable(s))<<s<<false;
    }
    void luaExponent(){QFETCH(QString,source);QFETCH(bool,valid);QSynedit::LuaSyntaxer lexer;lexer.resetState();lexer.setLine(0,source,1);
        const bool accepted=lexer.getToken()==source&&lexer.getTokenAttribute()==lexer.floatAttribute();QCOMPARE(accepted,valid);
    }
};
QTEST_MAIN(RustSharedChecks)
#include "rust-shared-checks.moc"
