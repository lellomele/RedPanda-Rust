// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <qsynedit/syntaxer/rust.h>
#include <qsynedit/syntaxer/cpp.h>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QSynedit::RustSyntaxer rust;
    rust.setLine(20,"implo TestError {",20);rust.nextToEol();
    QSynedit::CppSyntaxer cpp;
    cpp.setState(rust.getState());
    return 0;
}
