// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTCLEANPLAN_H
#define REDPANDA_RUSTCLEANPLAN_H
#include <QStringList>
#include <QByteArray>
#include <QHash>
namespace RustClean {
struct Plan {QString target,error;QStringList preview,files;QHash<QString,QPair<qint64,qint64>> fileState;QByteArray fingerprint;int entries=0;qint64 bytes=0;bool valid=false;};
Plan inspect(const QString &target,const QString &workspace,const QStringList &protectedFiles);
bool recycle(const Plan &plan,const QString &workspace,const QStringList &protectedFiles,QString &error);
}
#endif
