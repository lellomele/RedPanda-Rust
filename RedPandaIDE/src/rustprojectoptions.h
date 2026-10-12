// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTPROJECTOPTIONS_H
#define REDPANDA_RUSTPROJECTOPTIONS_H
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QProcessEnvironment>
struct RustProjectOptions {
    bool release=false,allFeatures=false,noDefaultFeatures=false;
    QString package,targetKind,targetName,targetTriple,workingDirectory;
    QStringList features,arguments,rustFlags;
    QMap<QString,QString> environment;
    static RustProjectOptions load(const QString &root,QString *error=nullptr);
    bool save(const QString &root,QString &error)const;
    bool validate(const QString &root,QString &error)const;
    QJsonObject json()const;
    static bool decode(const QJsonObject &object,RustProjectOptions &result,QString &error);
    QStringList cargoArguments(const QString &command)const;
    QProcessEnvironment processEnvironment()const;
    QString runDirectory(const QString &root)const;
};
#endif
