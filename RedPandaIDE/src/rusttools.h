// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTTOOLS_H
#define REDPANDA_RUSTTOOLS_H
#include <QString>
#include <QStringList>
#include <QMap>
class RustTools {
public:
    static void setSettingsFile(const QString &file);
    static QString settingsFile();
    static QStringList names();
    static QString configuredPath(const QString &name);
    static QString detectedPath(const QString &name);
    static QString effectivePath(const QString &name);
    static QString edition();
    static bool save(const QMap<QString,QString> &paths,const QString &edition,QString &error);
};
#endif
