// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include "rustsnippet.h"
class RustSnippetChecks:public QObject{
    Q_OBJECT
private slots:
    void expansion_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<QString>("plain");QTest::addColumn<QString>("editor");
        QTest::newRow("function")<<"answer()$0"<<"answer()"<<"answer()%INSERT%";
        QTest::newRow("final-placeholder")<<"${0:todo!()}"<<"todo!()"<<"%REPL_BEGIN%todo!()%REPL_END%%INSERT%";
        QTest::newRow("arguments")<<"call(${1:name}, ${2:value})$0"<<"call(name, value)"<<"call(%REPL_BEGIN%name%REPL_END%, %REPL_BEGIN%value%REPL_END%)%INSERT%";
        QTest::newRow("empty")<<"call($1)$0"<<"call()"<<"call(%REPL_BEGIN%%REPL_END%)%INSERT%";
        QTest::newRow("choice")<<"${1|one,two|}$0"<<"one"<<"%REPL_BEGIN%one%REPL_END%%INSERT%";
        QTest::newRow("escapes")<<"\\$x ${1:a\\}b} \\\\"<<"$x a}b \\"<<"$x %REPL_BEGIN%a}b%REPL_END% \\";
        QTest::newRow("multiline-body")<<"fn main() {\n    $0\n}"<<"fn main() {\n    \n}"<<"fn main() {\n    %INSERT%\n}";
        QTest::newRow("unicode")<<QString::fromUtf8("${1:città 🦀}$0")<<QString::fromUtf8("città 🦀")<<QString::fromUtf8("%REPL_BEGIN%città 🦀%REPL_END%%INSERT%");
    }
    void expansion(){QFETCH(QString,source);QFETCH(QString,plain);QFETCH(QString,editor);const auto value=RustSnippet::expand(source);QVERIFY(value.valid);QCOMPARE(value.text,plain);QCOMPARE(value.editorSnippet,editor);}
    void rejectsUnsupported_data(){QTest::addColumn<QString>("source");for(const auto &source:QStringList{"${1:unclosed","$VARIABLE","${1:x}$1","${2:x}${1:y}","${1:${2:x}}","${1:x\ny}","%INSERT%","$0$1","${1/foo/bar/}","${99999999999999:x}"})QTest::newRow(qPrintable(source))<<source;}
    void rejectsUnsupported(){QFETCH(QString,source);QVERIFY(!RustSnippet::expand(source).valid);}
};
QTEST_GUILESS_MAIN(RustSnippetChecks)
#include "rust-snippet-checks.moc"
