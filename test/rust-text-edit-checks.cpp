// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include "rusttextedits.h"
class RustTextEditChecks:public QObject{
    Q_OBJECT
    static QJsonObject edit(int line,int begin,int end,const QString &text){return {{"range",QJsonObject{{"start",QJsonObject{{"line",line},{"character",begin}}},{"end",QJsonObject{{"line",line},{"character",end}}}}},{"newText",text}};}
private slots:
    void unicodeAndMultipleEdits(){
        const QString source=QString::fromUtf8("fn città() {}\nlet 🦀 = città();\n");QVector<RustTextEdits::Edit> plan;QString error;
        QVERIFY2(RustTextEdits::plan(source,{edit(0,3,8,"renamed"),edit(1,9,14,"renamed")},plan,error),qPrintable(error));
        QCOMPARE(RustTextEdits::apply(source,plan),QString::fromUtf8("fn renamed() {}\nlet 🦀 = renamed();\n"));
    }
    void orderedInsertions(){QVector<RustTextEdits::Edit> plan;QString error;QVERIFY(RustTextEdits::plan("x",{edit(0,0,0,"one"),edit(0,0,0,"two")},plan,error));QCOMPARE(RustTextEdits::apply("x",plan),QString("onetwox"));}
    void rejectOverlapsAndInvalidPositions(){
        QVector<RustTextEdits::Edit> preserved{{0,0,"preserved",0}};QString error;
        QVERIFY(!RustTextEdits::plan("abcdef",{edit(0,1,4,"x"),edit(0,3,5,"y")},preserved,error));QCOMPARE(preserved.first().text,QString("preserved"));
        QVERIFY(!RustTextEdits::plan("abc",{edit(-1,0,1,"x")},preserved,error));QVERIFY(!RustTextEdits::plan("abc",{edit(0,4,4,"x")},preserved,error));
        QVERIFY(!RustTextEdits::plan(QString::fromUtf8("🦀"),{edit(0,1,2,"x")},preserved,error));
        QVERIFY(!RustTextEdits::plan("abc",{edit(0,0,1,QString(QChar(0xd800)))},preserved,error));
    }
    void multilineAndRandomizedIndependentEdits(){
        QVector<RustTextEdits::Edit> plan;QString error;QJsonObject multiline{{"range",QJsonObject{{"start",QJsonObject{{"line",0},{"character",1}}},{"end",QJsonObject{{"line",1},{"character",1}}}}},{"newText","X\r\nY"}};
        QVERIFY(RustTextEdits::plan("abc\ndef",{multiline},plan,error));QCOMPARE(RustTextEdits::apply("abc\ndef",plan),QString("aX\nYef"));
        for(int count=1;count<=100;++count){QString source(count*4,'x'),expected=source;QJsonArray edits;
            for(int i=0;i<count;++i){edits.append(edit(0,i*4,i*4+2,"A"));}
            for(int i=count-1;i>=0;--i)expected.replace(i*4,2,"A");
            QVERIFY(RustTextEdits::plan(source,edits,plan,error));QCOMPARE(RustTextEdits::apply(source,plan),expected);
        }
    }
};
QTEST_GUILESS_MAIN(RustTextEditChecks)
#include "rust-text-edit-checks.moc"
