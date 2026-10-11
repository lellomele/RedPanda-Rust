// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QDialog>
#include <QMainWindow>
#include <QDomDocument>
#include <QFile>
#include "ui_aboutdialog.h"
#include "appidentity.h"
class RustIconChecks : public QObject {
    Q_OBJECT
private slots:
    void existingWindowIconsUseRust(){
        QFile rust(":/icons/app/red-panda-rust.ico"),legacy(":/icons/images/devcpp.ico");
        QVERIFY(rust.open(QIODevice::ReadOnly));QVERIFY(legacy.open(QIODevice::ReadOnly));
        const QByteArray bytes=rust.readAll();QVERIFY(!bytes.isEmpty());QCOMPARE(legacy.readAll(),bytes);
        const QIcon expected(":/icons/app/red-panda-rust.ico");QVERIFY(!expected.isNull());
        QDialog about;Ui::AboutDialog ui;ui.setupUi(&about);
        QCOMPARE(about.windowIcon().pixmap(32,32).toImage(),expected.pixmap(32,32).toImage());
        QFile source(QStringLiteral(RUST_IDE_SOURCE_DIR)+"/src/mainwindow.ui");QVERIFY(source.open(QIODevice::ReadOnly));
        QDomDocument doc;QVERIFY(doc.setContent(&source));
        const auto widget=doc.documentElement().firstChildElement("widget");
        QString uri;
        for(auto p=widget.firstChildElement("property");!p.isNull();p=p.nextSiblingElement("property"))
            if(p.attribute("name")=="windowIcon")uri=p.firstChildElement("iconset").firstChildElement().text();
        QVERIFY(!uri.isEmpty());QIcon configured;configured.addFile(uri,QSize(),QIcon::Normal,QIcon::On);
        QMainWindow main;main.setWindowIcon(configured);
        QCOMPARE(main.windowIcon().pixmap(32,32).toImage(),expected.pixmap(32,32).toImage());
        QFile png(AppIdentity::iconResource()),oldPng(":/icons/app/redpanda-cpp.png");
        QVERIFY(png.open(QIODevice::ReadOnly));QVERIFY(oldPng.open(QIODevice::ReadOnly));QCOMPARE(oldPng.readAll(),png.readAll());
    }
};
QTEST_MAIN(RustIconChecks)
#include "rust-icon-checks.moc"
