#ifndef ADDRESSTABLEMODEL_IMPL_H
#define ADDRESSTABLEMODEL_IMPL_H

#include <QAbstractTableModel>
#include <QStringList>
#include "script/script.h"
#include "base58.h"

class CWallet;
class AddressTable_impl;

struct AddressTableEntry
{
    enum Type {
        None,
        Sending,
        Receiving,
        Donation
    };

    Type type;
    QString label;
    QString address;
    QString description;

    private:
    AddressTableEntry() { type = None;}
    AddressTableEntry(Type type, const CTxDestination& destination, const QString &label, const QString &address, const QString &description = QString())
    : type(type), label(label), address(address), description(description), destination(destination) {}
    AddressTableEntry(Type type, const QString &label, const QString &address, const QString &description = QString())
    : type(type), label(label), address(address), description(description) {destination = CBitcoinAddress(address.toStdString()).Get();}
    CTxDestination destination;
    friend class AddressTable_impl;
};

class QAbstractAddressTableModel;

class AddressTable_impl
{
public:

    CWallet *wallet;
    QList<AddressTableEntry> donationAddressTable;
    QList<AddressTableEntry> cachedAddressTable;
    QAbstractAddressTableModel *parent;

    AddressTable_impl(CWallet *wallet, QAbstractAddressTableModel *parent, bool includeExternalAccounts, bool includeMyAccounts);
    void addDonationAddress(const QString &label, const QString &address, const QString &description = QString());
    void refreshAddressTable();
    void updateEntry(const QString &address, const QString &label, bool isMine, int status);
    int size();
    QString getLabel(int idx);
    QString getAddress(int idx);
    qint64 getBalance(int idx);
    AddressTableEntry::Type getType(int idx);
    AddressTableEntry *index(int idx);
private:
    bool includeExternalAccounts;
    bool includeMyAccounts;
};

class QAbstractAddressTableModel : public QAbstractTableModel
{
    public:
        QAbstractAddressTableModel(QObject* parent=0) : QAbstractTableModel(parent){};
        void beginInsertRows(const QModelIndex &parent, int first, int last)
        {
            QAbstractTableModel::beginInsertRows(parent, first, last);
        }
        void endInsertRows()
        {
            QAbstractTableModel::endInsertRows();
        }
        void beginRemoveRows(const QModelIndex &parent, int first, int last)
        {
            QAbstractTableModel::beginRemoveRows(parent, first, last);
        }
        void endRemoveRows()
        {
            QAbstractTableModel::endRemoveRows();
        }
        /** Notify listeners that data changed. */
        void emitDataChanged(int idx)
        {
            emit dataChanged(index(idx, 0, QModelIndex()), index(idx, columnCount()-1, QModelIndex()));
        }
        /* Update address list from core.
         */
        void updateEntry(const QString &address, const QString &label, bool isMine, int status)
        {
            // Update address book model from Bitcoin core
            priv->updateEntry(address, label, isMine, status);
        }
    protected:
        AddressTable_impl* priv;
};

#endif // ADDRESSTABLEMODEL_IMPL_H