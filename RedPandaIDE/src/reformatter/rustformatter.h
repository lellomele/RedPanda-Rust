// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTFORMATTER_H
#define RUSTFORMATTER_H
#include "basereformatter.h"
#include <functional>
class RustFormatter final:public BaseReformatter {
public:
    RustFormatter(QString tool,QString file,QString edition,std::function<bool()> current={},int timeout=10000);
    QString refomat(const QString &content,QString &error,bool &ok) override;
    static QString editionForFile(const QString &file,const QString &fallback,QString &error);
private:
    QString mTool,mFile,mEdition;
    std::function<bool()> mCurrent;
    int mTimeout;
};
#endif
