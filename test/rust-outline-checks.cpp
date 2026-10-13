// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QAbstractItemModelTester>
#include <QStandardItemModel>
#include <QPersistentModelIndex>
#include <QPointer>
#include <thread>
#include "rustoutlinemodel.h"

class RustOutlineChecks:public QObject {
    Q_OBJECT
    const QString source="pub struct Tank {\n    pub litres: i64,\n    hidden: i64,\n}\nimpl Tank {\n    pub fn zebra(&self) {}\n    fn alpha(&self) {}\n    fn new() -> Self {}\n}\nimpl Display for Tank {}\nfn main() {}\n";
    static QJsonObject symbol(const QString &name,int kind,int line,int start,const QString &detail={},const QJsonArray &children={}){
        const QJsonObject begin{{"line",line},{"character",start}},end{{"line",line},{"character",start+name.size()}};
        return {{"name",name},{"kind",kind},{"detail",detail},{"selectionRange",QJsonObject{{"start",begin},{"end",end}}},{"range",QJsonObject{{"start",QJsonObject{{"line",line},{"character",0}}},{"end",end}}},{"children",children}};
    }
    QJsonArray fixture() const {
        auto implementation=symbol("Tank",19,4,5,{}, {symbol("zebra",6,5,11,"fn(&self)"),symbol("alpha",6,6,7,"fn(&self)"),symbol("new",12,7,7,"fn() -> Self")});implementation["name"]="impl Tank";
        auto trait=symbol("Display for Tank",19,9,5);trait["name"]="impl Display for Tank";
        return {symbol("Tank",23,0,11,{}, {symbol("litres",8,1,8,"i64"),symbol("hidden",8,2,4,"i64")}),implementation,trait,symbol("main",12,10,3,"fn()")};
    }
    static QStringList names(RustOutlineModel &m,const QModelIndex &parent={}){QStringList result;for(int row=0;row<m.rowCount(parent);++row)result.append(m.data(m.index(row,0,parent),RustOutlineModel::NameRole).toString());return result;}
    static QModelIndex named(RustOutlineModel &m,const QString &name,const QModelIndex &parent={}){for(int row=0;row<m.rowCount(parent);++row){const auto i=m.index(row,0,parent);if(m.data(i,RustOutlineModel::NameRole)==name)return i;const auto child=named(m,name,i);if(child.isValid())return child;}return {};}
private slots:
    void structureMergeIconsAndVisibility(){
        RustOutlineModel model;QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);int calls=0;
        model.setStyler([&](int kind,int visibility,bool){++calls;QPixmap p(16,16);p.fill(kind==23?Qt::cyan:visibility==2?Qt::green:Qt::red);return RustOutlineModel::Style{QIcon(p),Qt::blue};});
        QString error;QVERIFY2(model.setSymbols(fixture(),"/probe.rs",source,error),qPrintable(error));
        QCOMPARE(model.rowCount(),3);const auto tank=named(model,"Tank");QVERIFY(tank.isValid());QCOMPARE(model.rowCount(tank),5);
        QCOMPARE(names(model,tank),QStringList({"litres","hidden","zebra","alpha","new"}));
        const auto field=named(model,"litres");QCOMPARE(model.data(field,RustOutlineModel::VisibilityRole).toInt(),2);
        QCOMPARE(model.data(named(model,"hidden"),RustOutlineModel::VisibilityRole).toInt(),0);
        QCOMPARE(model.data(named(model,"zebra"),RustOutlineModel::VisibilityRole).toInt(),2);
        QVERIFY(model.data(field,Qt::DecorationRole).value<QIcon>().isNull()==false);
        QVERIFY(model.data(named(model,"new"),RustOutlineModel::AssociatedRole).toBool());
        QCOMPARE(model.data(named(model,"zebra"),Qt::DisplayRole).toString(),QString("zebra(&self)"));
        QVERIFY(calls<=8);const auto location=model.location(named(model,"zebra"));QVERIFY(location.valid);QCOMPARE(location.line,5);QCOMPARE(location.character,11);QCOMPARE(location.source,source);
    }
    void everySortingCombinationAndPersistentSelection(){
        RustOutlineModel model;QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);QString error;QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));
        QPersistentModelIndex selection(named(model,"zebra"));
        model.setOptions(true,false,true);QCOMPARE(names(model,named(model,"Tank")),QStringList({"alpha","hidden","litres","new","zebra"}));QVERIFY(selection.isValid());QCOMPARE(model.location(selection).line,5);
        model.setOptions(false,true,true);QCOMPARE(names(model,named(model,"Tank")),QStringList({"new","zebra","alpha","litres","hidden"}));QCOMPARE(model.data(selection,RustOutlineModel::NameRole).toString(),QString("zebra"));
        model.setOptions(true,true,true);QCOMPARE(names(model,named(model,"Tank")),QStringList({"new","alpha","zebra","hidden","litres"}));
        model.setOptions(false,false,true);QCOMPARE(names(model,named(model,"Tank")),QStringList({"litres","hidden","zebra","alpha","new"}));
        for(int i=0;i<1000;++i){model.setOptions(i%2,(i/2)%2,true);QVERIFY(selection.isValid());QCOMPARE(model.location(selection).line,5);}
        model.setOptions(false,false,false);QCOMPARE(model.rowCount(),2);QVERIFY(!named(model,"impl Display for Tank").isValid());QCOMPARE(model.rowCount(named(model,"Tank")),5);
        model.setOptions(false,false,true);QCOMPARE(model.rowCount(),3);
    }
    void staleAndForeignIndexesAreRejected(){
        RustOutlineModel model;QString error;QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));const auto stale=named(model,"zebra");QPersistentModelIndex persistent(stale);
        for(int cycle=0;cycle<500;++cycle){QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));QVERIFY(!model.data(stale).isValid());QVERIFY(!model.location(stale).valid);QVERIFY(!persistent.isValid());}
        QStandardItemModel other(1,1);QVERIFY(!model.data(other.index(0,0)).isValid());QCOMPARE(model.rowCount(other.index(0,0)),0);QVERIFY(!model.index(-1,0).isValid());QVERIFY(!model.index(0,1).isValid());
        model.clear();QVERIFY(!model.location(stale).valid);QCOMPARE(model.rowCount(),0);
    }
    void malformedRepliesPreserveSnapshot(){
        RustOutlineModel model;QString error;QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));const auto before=model.location(named(model,"zebra"));
        const auto base=symbol("main",12,10,3,"fn()");QList<QJsonArray> bad{{QJsonValue(1)}};
        for(const auto &field:{QString("name"),QString("kind"),QString("selectionRange"),QString("children")}){auto item=base;item[field]=true;bad.append({item});}
        auto fractional=base;fractional["kind"]=1.5;bad.append({fractional});
        auto wrong=base;wrong["selectionRange"]=QJsonObject{{"start",QJsonObject{{"line",1e30},{"character",0}}},{"end",QJsonObject{{"line",0},{"character",0}}}};bad.append({wrong});
        auto broken=base;broken["name"]=QString(QChar(0xdc00));bad.append({broken});
        for(const auto &reply:bad){QVERIFY(!model.setSymbols(reply,"/probe.rs",source,error));QVERIFY(!error.isEmpty());QCOMPARE(model.location(named(model,"zebra")).source,before.source);QCOMPARE(model.rowCount(),3);}
        QJsonArray many;for(int i=0;i<10001;++i)many.append(base);QVERIFY(!model.setSymbols(many,"/probe.rs",source,error));QCOMPARE(model.rowCount(),3);
        auto deep=base;for(int i=0;i<26;++i){auto parent=base;parent["children"]=QJsonArray{deep};deep=parent;}QVERIFY(!model.setSymbols({deep},"/probe.rs",source,error));
        QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));
    }
    void reentrantResetAndSortAreDeferred(){
        RustOutlineModel model;QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);bool first=true;QString error;
        connect(&model,&QAbstractItemModel::modelAboutToBeReset,&model,[&]{if(first){first=false;QString ignored;QVERIFY(model.setSymbols({symbol("b",12,0,3)},"/second.rs","fn b() {}",ignored));model.setOptions(true,true,true);}});
        QVERIFY(model.setSymbols({symbol("a",12,0,3)},"/first.rs","fn a() {}",error));
        QTRY_COMPARE(model.file(),QString("/second.rs"));QCOMPARE(names(model),QStringList{"b"});QVERIFY(model.byName());
        bool once=true;connect(&model,&QAbstractItemModel::layoutAboutToBeChanged,&model,[&]{if(once){once=false;model.clear();model.setOptions(false,false,true);}});
        model.setOptions(false,true,true);QTRY_COMPARE(model.rowCount(),0);QVERIFY(!model.byKind());
    }
    void stylingFailureAndOwnerDestruction(){
        RustOutlineModel model;QString error;QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));const auto before=model.location(named(model,"zebra"));
        model.setStyler([](int,int,bool)->RustOutlineModel::Style{throw std::bad_alloc();});QCOMPARE(model.location(named(model,"zebra")).source,before.source);QVERIFY(model.setSymbols(fixture(),"/probe.rs",source,error));
        QPointer<RustOutlineModel> destroyed=new RustOutlineModel;auto raw=destroyed.data();raw->setStyler([raw](int,int,bool){delete raw;return RustOutlineModel::Style{};});
        QVERIFY(!raw->setSymbols({symbol("a",12,0,3)},"/probe.rs","fn a() {}",error));QVERIFY(destroyed.isNull());
        QPointer<RustOutlineModel> sorted=new RustOutlineModel;QVERIFY(sorted->setSymbols(fixture(),"/probe.rs",source,error));
        connect(sorted,&QAbstractItemModel::layoutAboutToBeChanged,this,[sorted]{delete sorted.data();});sorted->setOptions(true,false,true);QVERIFY(sorted.isNull());
    }
    void unicodeAndHostileTooltip(){
        RustOutlineModel model;QString error;auto item=symbol("a",12,0,3,"<img src='file:///secret'>");QVERIFY(model.setSymbols({item},"/probe.rs",QString::fromUtf8("🦀 a"),error));
        const auto text=model.data(model.index(0,0),Qt::ToolTipRole).toString();QVERIFY(text.contains("&lt;img"));QVERIFY(!text.contains("<img"));
        auto broken=item;broken["selectionRange"]=QJsonObject{{"start",QJsonObject{{"line",0},{"character",1}}},{"end",QJsonObject{{"line",0},{"character",2}}}};QVERIFY(!model.setSymbols({broken},"/probe.rs",QString::fromUtf8("🦀 a"),error));
    }
    void foreignThreadMutationIsRejected(){
        RustOutlineModel model;bool accepted=true;QString error;std::thread worker([&]{accepted=model.setSymbols({symbol("a",12,0,3)},"/probe.rs","fn a() {}",error);});worker.join();QVERIFY(!accepted);QVERIFY(!error.isEmpty());QCOMPARE(model.rowCount(),0);
    }
};
QTEST_MAIN(RustOutlineChecks)
#include "rust-outline-checks.moc"
