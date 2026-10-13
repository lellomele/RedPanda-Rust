// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTOUTLINEMODEL_H
#define RUSTOUTLINEMODEL_H
#include <QAbstractItemModel>
#include <QJsonArray>
#include <QIcon>
#include <QColor>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>

class RustOutlineModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    struct Style { QIcon icon; QColor color; };
    struct Location { QString file, source; int line=-1, character=-1; bool valid=false; };
    using Styler=std::function<Style(int kind,int visibility,bool associated)>;
    enum Role { NameRole=Qt::UserRole+1, KindRole, VisibilityRole, AssociatedRole };
    explicit RustOutlineModel(QObject *parent=nullptr);
    ~RustOutlineModel() override;
    QModelIndex index(int row,int column,const QModelIndex &parent={}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent={}) const override;
    int columnCount(const QModelIndex &parent={}) const override;
    QVariant data(const QModelIndex &index,int role=Qt::DisplayRole) const override;
    QVariant headerData(int section,Qt::Orientation orientation,int role) const override;
    bool setSymbols(const QJsonArray &symbols,const QString &file,const QString &source,QString &error);
    void clear();
    void setOptions(bool byName,bool byKind,bool showTraitImplementations);
    void setStyler(Styler styler);
    Location location(const QModelIndex &index) const;
    QString file() const;
    bool byName() const {return mByName;}
    bool byKind() const {return mByKind;}
    bool showTraitImplementations() const {return mShowTraits;}
signals:
    void rejected(const QString &reason);
private:
    struct Node;struct Snapshot;
    struct Update {QJsonArray symbols;QString file,source;bool clear=false;};
    const Node *node(const QModelIndex &index,const std::shared_ptr<Snapshot> &snapshot) const;
    QModelIndex nodeIndex(const Node *node) const;
    std::shared_ptr<Snapshot> cloneSnapshot() const;
    void arrange(Node *parent,bool visible);
    void finishChange();
    void drainPending();
    std::shared_ptr<Snapshot> mData;
    quintptr mNextId=1;
    bool mByName=false,mByKind=false,mShowTraits=true,mChanging=false,mQueued=false;
    std::optional<Update> mPending;
    std::optional<std::tuple<bool,bool,bool>> mPendingOptions;
    std::optional<Styler> mPendingStyler;
    Styler mStyler;
};
#endif
