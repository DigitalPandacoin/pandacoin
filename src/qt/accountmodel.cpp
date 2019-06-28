#include "accountmodel.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "addresstablemodel_impl.h"
#include "bitcoinunits.h"
#include "pandastyles.h"

#include "wallet.h"
#include "base58.h"

#include <QFont>
#include <QColor>
#include <QIcon>

AccountModel::AccountModel(CWallet *wallet, bool includeExternalAccounts, bool includeMyAccounts, WalletModel *parent)
: QAbstractAddressTableModel(parent)
, walletModel(parent)
, wallet(wallet)
{
    priv = new AddressTable_impl(wallet, dynamic_cast<QAbstractAddressTableModel*>(this), includeExternalAccounts, includeMyAccounts);
    columns << tr("Account Name") << tr("Account Address") << tr("Account Balance");
    priv->refreshAddressTable();
}

AccountModel::~AccountModel()
{
    delete priv;
}

int AccountModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int AccountModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant AccountModel::data(int col, int row) const
{
    switch(col)
    {
        case Label:
            return priv->getLabel(row);
        case Address:
            return priv->getAddress(row);
        case Balance:
        {
            int unit = walletModel->getOptionsModel()->getDisplayUnit();
            return BitcoinUnits::formatWithUnit(unit, priv->getBalance(row), true, false);
        }
        default:
            return QVariant();
    }
}

QVariant AccountModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    //First two columns left aligned, last column centered
    if (role == Qt::TextAlignmentRole )
    {
        switch(index.column())
        {
            case Label:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
            case Address:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
            case Balance:
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return QVariant();
        }
    }
    else if(role == RawData)
    {
        switch(index.column())
        {
            case Label:
                return priv->getLabel(index.row());
            case Address:
                return priv->getAddress(index.row());
            case Balance:
                return priv->getBalance(index.row());
            default:
                return QVariant();
        }
    }
    else if(role == TypeRole)
    {
        return QVariant((int)priv->getType(index.row()));
    }
    /*else if (role == Qt::DecorationRole)
    {
        switch(index.column())
        {
            case Label:
            {
                return QIcon(":/icons/right_arrow_1");
            }
            default:
                return QVariant();
        }
    }*/
    else if (role == Qt::FontRole)
    {
        switch(index.column())
        {
            case Label:
            {
                QFont underlineFont;
                underlineFont.setUnderline(true);
                QString fontSize = TOTAL_FONT_SIZE;
                fontSize = fontSize.replace("px","");
                underlineFont.setPixelSize(fontSize.toLong());
                return underlineFont;
            }
            default:
                return QVariant();
        }
    }
    else if (role == Qt::DisplayRole || role ==  Qt::EditRole)
    {
        return data(index.column(),index.row());
    }
    return QVariant();
}


QVariant AccountModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        //First two columns left aligned, last column centered
        if (role == Qt::TextAlignmentRole )
        {
            switch(section)
            {
                case Label:
                    return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
                case Address:
                    return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
                case Balance:
                    return QVariant(Qt::AlignRight | Qt::AlignVCenter);
                default:
                    return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
        else if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags AccountModel::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled;
}


SingleColumnAccountModel::SingleColumnAccountModel(AccountModel* parent,bool showBalances_,bool showAllAccounts_, QString hintText_)
: QAbstractTableModel()
, parentModel(parent)
, showBalances(showBalances_)
, showAllAccounts(showAllAccounts_)
, hintText(hintText_)
{
    connect(parent, SIGNAL(dataChanged(QModelIndex,QModelIndex)), this, SLOT(handleChanged(QModelIndex,QModelIndex)));
}

void SingleColumnAccountModel::handleChanged(QModelIndex from, QModelIndex to)
{
    int fromRow = from.row();
    int toRow = to.row();

    if(showAllAccounts)
    {
        fromRow++;
        toRow++;
    }
    if(!hintText.isEmpty())
    {
        fromRow++;
        toRow++;
    }

    emit dataChanged(index(fromRow,0),index(toRow,0));
}

int SingleColumnAccountModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

int SingleColumnAccountModel::rowCount(const QModelIndex &parent) const
{
    int rowCount = parentModel->rowCount(parent);

    if(!hintText.isEmpty())
        ++rowCount;
    if(showAllAccounts)
        ++rowCount;

    return rowCount;
}


QVariant SingleColumnAccountModel::data(const QModelIndex &index, int role) const
{
    if(role == Qt::UserRole)
    {
        if(index.column()==0)
        {
            if(!hintText.isEmpty())
            {
                if(index.row()==0)
                {
                    return hintText;
                }
            }
        }
    }
    else if (role == Qt::TextAlignmentRole )
    {
        if(index.column()==0)
        {
            return QVariant(Qt::AlignLeft|Qt::AlignVCenter);
        }
    }
    else if (role == Qt::DisplayRole || role ==  Qt::EditRole)
    {
        if(index.column()==0)
        {
            int row=index.row();
            int col=index.column();
            if(!hintText.isEmpty())
            {
                if(row==0)
                {
                    return "";
                }
                row--;
            }

            if(showAllAccounts)
            {
                if(row==0)
                {
                    if(showBalances)
                    {
                        qint64 balance=0;
                        int unit = parentModel->walletModel->getOptionsModel()->getDisplayUnit();
                        for(int i=0;i<parentModel->rowCount(index);i++)
                        {
                            qint64 amt;
                            BitcoinUnits::parse(unit,parentModel->data(2,i).toString(),&amt);
                            balance += amt;
                        }
                        return tr("All Accounts") + " (" + BitcoinUnits::formatWithUnit(unit, balance, true, false) + ")";
                    }
                    else
                    {
                        return tr("All Accounts");
                    }
                }
                row--;
            }

            if(showBalances)
            {
                QString ret=parentModel->data(parentModel->index(row,col), role).toString() + " (" + parentModel->data(parentModel->index(row,2), role).toString() + ") ";
                return ret;
            }
            else
            {
                return parentModel->data(parentModel->index(row,col), role);
            }
        }
    }
    else if (role == AccountModel::TypeRole)
    {
        return parentModel->data(parentModel->index(index.row(),index.column()), role);
    }
    return QVariant();
}

Qt::ItemFlags SingleColumnAccountModel::flags(const QModelIndex &index) const
{
    if(!hintText.isEmpty() && index.row() == 0)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}