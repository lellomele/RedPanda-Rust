// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTTERMINAL_H
#define REDPANDA_RUSTTERMINAL_H
#include <QObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QStringConverter>
class RustTerminalWorker;
class RustTerminal final:public QObject{
    Q_OBJECT
public:
    explicit RustTerminal(QObject *parent=nullptr);
    ~RustTerminal();
    bool start(const QString &program,const QStringList &arguments,const QString &directory,const QProcessEnvironment &environment);
    bool write(const QByteArray &bytes);
    void stop();
    bool active()const{return !mWorker.isNull();}
signals:
    void started(qint64 processId);
    void output(const QString &text);
    void error(const QString &message);
    void finished(int code);
private:
    QPointer<RustTerminalWorker> mWorker;
    QStringDecoder mDecoder{QStringDecoder::Utf8};
};
#endif
