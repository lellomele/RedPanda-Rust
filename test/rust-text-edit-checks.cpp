// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include "rusttextedits.h"
#include "rusttextedits-baseline.h"
#include <random>
#include <limits>
#include <qsynedit/qsynedit.h>
class RustTextEditChecks:public QObject{
    Q_OBJECT
    static QJsonObject edit(int line,int begin,int end,const QString &text){return {{"range",QJsonObject{{"start",QJsonObject{{"line",line},{"character",begin}}},{"end",QJsonObject{{"line",line},{"character",end}}}}},{"newText",text}};}
private slots:
    void editorApplyUndoRedoAndRejectedEdit(){
        QSynedit::QSynEdit editor;const QString original=QString::fromUtf8("fn città() {}\nlet 🦀 = città();\n");editor.setContentPreservingEndings(original);
        QVector<RustTextEdits::Edit> planned;QString error,after;
        QVERIFY(RustTextEdits::plan(editor.text(),{edit(0,3,8,"renamed"),edit(1,9,14,"renamed")},planned,error));
        QVERIFY(RustTextEdits::applyChecked(editor.text(),planned,after,error));
        editor.beginEditing();editor.setCaretAndSelection({0,0},{0,0},editor.fileEnd());editor.setSelText(after);editor.endEditing();
        QCOMPARE(editor.text(),QString::fromUtf8("fn renamed() {}\nlet 🦀 = renamed();\n"));editor.undo();QCOMPARE(editor.text(),original);editor.redo();QCOMPARE(editor.text(),after);
        const auto before=editor.text();QVERIFY(!RustTextEdits::applyChecked(before,{{-1,100,"",0}},after,error));QCOMPARE(editor.text(),before);
    }
    void differentialGeneratedCases(){
        std::mt19937 random(0x52455041);const QString unit=QString::fromUtf8("abc città 🦀\n");
        for(int sample=0;sample<20000;++sample){
            QString source;for(int i=0,n=1+random()%12;i<n;++i)source+=unit;
            QJsonArray proposals;int cursor=0;
            for(int i=0,n=random()%20;i<n&&cursor<source.size();++i){
                int start=qMin(int(source.size()),cursor+int(random()%5));
                if(start>0&&start<source.size()&&source[start].isLowSurrogate()&&source[start-1].isHighSurrogate())++start;
                int end=qMin(int(source.size()),start+int(random()%5));
                if(end>0&&end<source.size()&&source[end].isLowSurrogate()&&source[end-1].isHighSurrogate())++end;
                const auto position=[&](int at){int line=0,begin=0;for(int x=0;x<at;++x)if(source[x]=='\n'){++line;begin=x+1;}return QJsonObject{{"line",line},{"character",at-begin}};};
                QString replacement=sample%3==0?QString{}:sample%3==1?QString::fromUtf8("🦀x"):QString("multi\r\nline");
                proposals.append(QJsonObject{{"range",QJsonObject{{"start",position(start)},{"end",position(end)}}},{"newText",replacement}});cursor=end+1;
            }
            if(sample%4==0&&!proposals.isEmpty()){auto bad=proposals.last().toObject();bad["newText"]=QString(QChar(0xdc00));proposals.append(bad);}
            QVector<RustTextEdits::Edit> a{{0,0,"sentinel",0}},b=a;QString ea,eb;const auto before=source;
            const bool acceptedA=RustTextEditsBaseline::plan(source,proposals,a,ea),acceptedB=RustTextEdits::plan(source,proposals,b,eb);
            QCOMPARE(acceptedB,acceptedA);QCOMPARE(eb,ea);QCOMPARE(source,before);
            if(acceptedA){QString result="sentinel";QVERIFY2(RustTextEdits::applyChecked(source,b,result,eb),qPrintable(eb));QCOMPARE(result,RustTextEditsBaseline::apply(source,a));}
            else{QCOMPARE(b.size(),1);QCOMPARE(b[0].text,QString("sentinel"));}
        }
    }
    void forgedPlansPreserveOutputAndRetry(){
        const QString source=QString::fromUtf8("ab🦀cd");QString result="sentinel",error;
        const QList<QVector<RustTextEdits::Edit>> invalid={{{-1,1,"x",0}},{{0,std::numeric_limits<int>::max(),"x",0}},{{4,2,"x",0}},{{4,5,"x",0},{0,1,"y",1}},{{0,3,"x",0},{2,4,"y",1}},{{3,4,"x",0}},{{0,0,QString(QChar(0xd800)),0}},{{1,1,"x",0},{1,2,"y",1}}};
        for(int i=0;i<5000;++i){QVERIFY(!RustTextEdits::applyChecked(source,invalid[i%invalid.size()],result,error));QCOMPARE(result,QString("sentinel"));QCOMPARE(RustTextEdits::apply(source,invalid[i%invalid.size()]),source);}
        QVERIFY(RustTextEdits::applyChecked(source,{{0,2,"OK",0}},result,error));QCOMPARE(result,QString::fromUtf8("OK🦀cd"));QVERIFY(error.isEmpty());
        QString alias=source;QVERIFY(RustTextEdits::applyChecked(alias,{{0,2,"OK",0}},alias,error));QCOMPARE(alias,result);
    }
    void allocationFailurePreservesStateAndRetry(){
        QString result="sentinel",error;const QString source="abc";QVector<RustTextEdits::Edit> edits{{1,2,"XX",0}};
        RustTextEdits::failNextAllocationForTest();QVERIFY(!RustTextEdits::applyChecked(source,edits,result,error));QCOMPARE(result,QString("sentinel"));QCOMPARE(source,QString("abc"));QVERIFY(error.contains("Memoria"));
        QVERIFY(RustTextEdits::applyChecked(source,edits,result,error));QCOMPARE(result,QString("aXXc"));
        QVector<RustTextEdits::Edit> planned{{0,0,"sentinel",0}};RustTextEdits::failNextAllocationForTest();
        QVERIFY(!RustTextEdits::plan(source,{edit(0,1,2,"XX")},planned,error));QCOMPARE(planned[0].text,QString("sentinel"));
        QVERIFY(RustTextEdits::plan(source,{edit(0,1,2,"XX")},planned,error));
    }
    void invalidJsonMatchesBaseline(){
        const auto good=edit(0,0,1,"OK");QList<QJsonArray> cases{{QJsonValue(42)},{QJsonObject{}},{good,QJsonValue(false)}};
        for(const auto value:{QJsonValue(-1),QJsonValue(0.5),QJsonValue(1e30),QJsonValue("0"),QJsonValue(QJsonValue::Null)}){
            auto invalid=good;auto range=invalid["range"].toObject();auto start=range["start"].toObject();start["character"]=value;range["start"]=start;invalid["range"]=range;cases.append({invalid});
        }
        auto invalid=good;invalid["newText"]=42;cases.append({invalid});cases.append({edit(0,0,2,"a"),edit(0,1,3,"b")});
        for(int repetition=0;repetition<100;++repetition)for(const auto &proposal:cases){
            QVector<RustTextEdits::Edit> a{{0,0,"sentinel",0}},b=a;QString ea,eb;
            const bool acceptedA=RustTextEditsBaseline::plan("abc",proposal,a,ea),acceptedB=RustTextEdits::plan("abc",proposal,b,eb);
            QVERIFY(!acceptedA);QCOMPARE(acceptedB,acceptedA);QCOMPARE(eb,ea);QCOMPARE(b[0].text,QString("sentinel"));
        }
    }
    void limitsAndDegeneratePlans(){
        QString result="sentinel",error;const QString source(RustTextEdits::MaxTextUnits,'x');
        QVERIFY(RustTextEdits::applyChecked(source,{{0,1,"y",0}},result,error));QCOMPARE(result.size(),source.size());
        result="sentinel";QVERIFY(!RustTextEdits::applyChecked(source,{{0,0,"y",0}},result,error));QCOMPARE(result,QString("sentinel"));
        QVERIFY(!RustTextEdits::applyChecked(QString(RustTextEdits::MaxTextUnits+1,'x'),{},result,error));
        QVERIFY(!RustTextEdits::applyChecked("",QVector<RustTextEdits::Edit>(RustTextEdits::MaxEdits+1),result,error));
        QVERIFY(!RustTextEdits::applyChecked("",{{0,0,QString(RustTextEdits::MaxReplacementUnits+1,'x'),0}},result,error));
        QVector<RustTextEdits::Edit> inserts;for(int i=0;i<10000;++i)inserts.append({0,0,QString::number(i%10),i});
        QVERIFY(RustTextEdits::applyChecked("x",inserts,result,error));QCOMPARE(result,RustTextEditsBaseline::apply("x",inserts));
        QVERIFY(RustTextEdits::applyChecked("abc",{{0,3,"",0}},result,error));QVERIFY(result.isEmpty());
    }
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
QTEST_MAIN(RustTextEditChecks)
#include "rust-text-edit-checks.moc"
