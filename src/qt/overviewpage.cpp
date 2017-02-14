// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "accountmodel.h"
#include "richtextdelegate.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QLocale>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QListView>
#include <QMenu>

#define DECORATION_SIZE 64
#define NUM_ITEMS 3


OverviewPage::OverviewPage(QWidget *parent)
: QWidget(parent)
, ui(new Ui::OverviewPage)
, currentBalance(-1)
, currentStake(0)
, currentUnconfirmedBalance(-1)
, contextMenu(NULL)
{
    ui->setupUi(this);

    connect(ui->portfolio_heading_more, SIGNAL(pressed()), this, SIGNAL(requestGotoTransactionPage()));
    connect(ui->PortfolioTable, SIGNAL(clicked(QModelIndex)), this, SLOT(handleAccountClicked(QModelIndex)));
    ui->PortfolioTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // Actions
    QAction *copyAddressAction = new QAction(tr("Copy account address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy account name"), this);
    QAction *copyAmountAction = new QAction(tr("Copy account balance"), this);
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(ui->PortfolioTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    // Menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyAmountAction);

    //fixme: hardcoded
    ui->welcome_heading->setText(tr("Welcome to your Digital PandaBank, You last logged on at") + " " + QDateTime::currentDateTime().time().toString() + " " + tr("on") + " " + QDateTime::currentDateTime().date().toString());

    //Hide for now (apparently this will only be visible in later versions of UI).
    ui->portfolio_overview_description->setVisible(false);
}

void OverviewPage::handleAccountClicked(const QModelIndex &index)
{
    if(model)
    {
        if(index.column() == AccountModel::Label)
        {
            emit accountClicked(ui->PortfolioTable->model()->data(index, Qt::UserRole).toString());
        }
    }
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, qint64 stake, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = model->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentStake = stake;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelBalance->setText(formatBitcoinAmountAsRichString(BitcoinUnits::formatWithUnit(unit, balance, true, false)));
    ui->labelStake->setText(formatBitcoinAmountAsRichString(BitcoinUnits::formatWithUnit(unit, stake, true, false)));
    ui->labelUnconfirmed->setText(formatBitcoinAmountAsRichString(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, true, false)));
    ui->labelTotal->setText(formatBitcoinAmountAsRichString(BitcoinUnits::formatWithUnit(unit, balance + stake + unconfirmedBalance, true, false)));
}

class OverViewModel: public QSortFilterProxyModel
{
public:
    OverViewModel(QObject* parent=NULL)
    : QSortFilterProxyModel(parent)
    {

    }
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
    {
        return true;
    }
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex &sourceParent) const
    {
        return true;
    }
    QVariant data(const QModelIndex &index, int role) const
    {
        if(index.column() == AccountModel::Label)
        {
            if (role == Qt::DisplayRole || role ==  Qt::EditRole)
            {
                return QSortFilterProxyModel::data(index, role).toString() + "<span style='text-decoration: none;'>&nbsp;&nbsp;</span><img src=':/icons/right_arrow'/>";
            }
        }

        return QSortFilterProxyModel::data(index, role);
    }
private:
    QString filterString;
};

void OverviewPage::setModel(WalletModel *model)
{
    this->model = model;
    if(model && model->getOptionsModel())
    {
        //LEAKLEAK
        RichTextDelegate* richDelegateCurrency = new RichTextDelegate(this);
        RichTextDelegate* richDelegate = new RichTextDelegate(this, false);
        richDelegate->setLeftPadding(8);

        //LEAKLEAK
        QSortFilterProxyModel* sortableAccountModel = new OverViewModel(this);
        sortableAccountModel->setSortRole(Qt::UserRole);
        sortableAccountModel->setSourceModel(model->getMyAccountModel());
        ui->PortfolioTable->setModel(sortableAccountModel);
        ui->PortfolioTable->setItemDelegateForColumn(AccountModel::Balance ,richDelegateCurrency);
        ui->PortfolioTable->setItemDelegateForColumn(AccountModel::Label ,richDelegate);
        ui->PortfolioTable->setItemDelegateForColumn(AccountModel::Address ,richDelegate);

        ui->PortfolioTable->setSortingEnabled(true);
        ui->PortfolioTable->sortByColumn(0);
        ui->PortfolioTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // Allow table to change its size hints when rows added or removed.
        connect(sortableAccountModel, SIGNAL(rowsInserted(QModelIndex, int, int)), ui->PortfolioTable, SLOT(layoutChanged()));
        connect(sortableAccountModel, SIGNAL(rowsRemoved(QModelIndex, int, int)), ui->PortfolioTable, SLOT(layoutChanged()));
        ui->PortfolioTable->layoutChanged();

        //LEAKLEAK
        SingleColumnAccountModel* listModel=new SingleColumnAccountModel(model->getMyAccountModel(), true, false, tr("Select account"));
        ui->quick_transfer_from_combobox->setModel(listModel);
        ui->quick_transfer_from_combobox->setItemDelegate(richDelegateCurrency);
        // Sadly the below is necessary in order to be able to style QComboBox pull down lists properly.
        ui->quick_transfer_from_combobox->setView(new QListView(this));

        //LEAKLEAK
        SingleColumnAccountModel* listModel2=new SingleColumnAccountModel(model->getExternalAccountModel(), false, false, tr("Select account"));
        ui->quick_transfer_to_combobox->setModel(listModel2);
        ui->quick_transfer_to_combobox->setItemDelegate(richDelegateCurrency);
        // Sadly the below is necessary in order to be able to style QComboBox pull down lists properly.
        ui->quick_transfer_to_combobox->setView(new QListView(this));

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getStake(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        int unit = model->getOptionsModel()->getDisplayUnit();

        if(currentBalance != -1)
            setBalance(currentBalance, model->getStake(), currentUnconfirmedBalance, currentImmatureBalance);

        ui->quick_transfer_amount->setDisplayUnit(unit);

        // Update txdelegate->unit with the current unit
        //txdelegate->unit = model->getOptionsModel()->getDisplayUnit();

        //ui->listTransactions->update();
    }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    //ui->labelWalletStatus->setVisible(fShow);
    //ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::on_quick_transfer_next_button_clicked()
{
    std::vector<SendCoinsRecipient> recipients;

    if(!model)
        return;

    if(!ui->quick_transfer_amount->validate())
    {
        ui->quick_transfer_amount->setValid(false);
        return;
    }
    else
    {
        if(ui->quick_transfer_amount->value() <= 0)
        {
            // Cannot send 0 coins or less
            ui->quick_transfer_amount->setValid(false);
            return;
        }
    }
    if(ui->quick_transfer_to_combobox->currentIndex() < 1)
    {
        //fixme: indicate invalid somehow
        //ui->quick_transfer_to_combobox->setValid(false);
        return;
    }
    if(ui->quick_transfer_from_combobox->currentIndex() < 1)
    {
        //fixme: indicate invalid somehow
        //ui->quick_transfer_from_combobox->setValid(false);
        return;
    }

    int toIndex = ui->quick_transfer_to_combobox->currentIndex() - 1;
    int fromIndex = ui->quick_transfer_from_combobox->currentIndex() - 1;
    QString toAddress = model->getExternalAccountModel()->data(AccountModel::Address, toIndex).toString().trimmed();
    QString toLabel = model->getExternalAccountModel()->data(AccountModel::Label, toIndex).toString().trimmed();
    std::string fromAccountAddress = model->getMyAccountModel()->data(AccountModel::Address, fromIndex).toString().trimmed().toStdString();

    qint64 amt=ui->quick_transfer_amount->value();

    recipients.push_back(SendCoinsRecipient(amt, toAddress.toStdString(), toLabel.toStdString()));

    std::string transactionHash;
    if(GUIUtil::SendCoinsHelper(this, recipients, model, fromAccountAddress, true, transactionHash))
    {
        ui->quick_transfer_amount->clear();
        ui->quick_transfer_to_combobox->clear();
        ui->quick_transfer_from_combobox->clear();
    }
}

void OverviewPage::contextualMenu(const QPoint &point)
{
    contextMenuTriggerIndex = ui->PortfolioTable->indexAt(point);
    if(contextMenuTriggerIndex.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void OverviewPage::copyAddress()
{
    GUIUtil::copyEntryData(ui->PortfolioTable, contextMenuTriggerIndex.row(), 1, Qt::UserRole);
}

void OverviewPage::copyLabel()
{
    GUIUtil::copyEntryData(ui->PortfolioTable, contextMenuTriggerIndex.row(), 0, Qt::UserRole);
}

void OverviewPage::copyAmount()
{
    GUIUtil::copyEntryData(ui->PortfolioTable, contextMenuTriggerIndex.row(), 2, Qt::DisplayRole);
}
