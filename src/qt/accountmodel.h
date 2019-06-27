#ifndef ACCOUNTMODEL_H
#define ACCOUNTMODEL_H

#include <QAbstractTableModel>
#include <QIdentityProxyModel>
#include <QStringList>
#include "addresstablemodel_impl.h"

class CWallet;
class WalletModel;

/**
   Qt model of the accounts in the core. This allows views to access and modify the accounts.
   Contains 3 columns: Label, Address, Balance
 */
class AccountModel : public QAbstractAddressTableModel
{
    Q_OBJECT
public:
    explicit AccountModel(CWallet *wallet, bool includeExternalAccounts, bool includeMyAccounts, WalletModel *parent = 0);
    ~AccountModel();

    enum RoleIndex {
        RawData = Qt::UserRole,
        TypeRole /**< Type of address (donation or address) */
    };

    enum ColumnIndex
    {
        Label = 0,   /**< User specified label */
        Address = 1,  /**< Bitcoin address */
        Balance = 2,  /**< Bitcoin balance */
    };

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant data(int col, int row) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;

    WalletModel *walletModel;
private:
    CWallet *wallet;
    QStringList columns;
};


//fixme: Reimplement as QSortFilterProxyModel
/**
  Proxy of AccountModel that performs the same identical functionality
  Except with only one column - in which both the label and balance are combined
  */
class SingleColumnAccountModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    SingleColumnAccountModel(AccountModel* parent,bool showBalances_, bool showAllAccounts_, QString hintText="");
    int columnCount(const QModelIndex &parent) const;
    int rowCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;

private:
    AccountModel* parentModel;
    bool showBalances;
    bool showAllAccounts;
    QString hintText;

private slots:
    void handleChanged(QModelIndex from, QModelIndex to);
};

#endif // ACCOUNTMODEL_H