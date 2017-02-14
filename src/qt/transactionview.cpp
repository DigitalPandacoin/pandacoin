// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionview.h"

#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "walletmodel.h"
#include "transactiontablemodel.h"
#include "richtextdelegate.h"
#include "addresstablemodel.h"
#include "transactiontablemodel.h"
#include "transactionviewtable.h"
#include "bitcoinunits.h"
#include "csvmodelwriter.h"
#include "transactiondescdialog.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "guiutil.h"

#include <QScrollBar>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QTableView>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>
#include <QPoint>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QDesktopServices>
#include <QUrl>

TransactionView::TransactionView(QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0)
{
    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    transactionView = new TransactionViewTable(this);
    //vlayout->addLayout(hlayout);
    vlayout->addWidget(transactionView);
    vlayout->setSpacing(0);
    /*int width = view->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
#ifdef Q_OS_MAC
    hlayout->addSpacing(width+2);
#else
    hlayout->addSpacing(width);
#endif*/
    // Always show scroll bar
    transactionView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    transactionView->setTabKeyNavigation(false);
    transactionView->setContextMenuPolicy(Qt::CustomContextMenu);
    transactionView->setFrameShape(QFrame::NoFrame);
    transactionView->setFrameShadow(QFrame::Plain);
    transactionView->verticalHeader()->setDefaultSectionSize(25);
    transactionView->verticalHeader()->setMinimumSectionSize(18);

    // Actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyTxIDAction = new QAction(tr("Copy transaction ID"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);
    QAction *viewOnBlockExplorer = new QAction(tr("Show transaction in blockchain explorer"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTxIDAction);
    contextMenu->addSeparator();
    contextMenu->addAction(editLabelAction);
    contextMenu->addAction(showDetailsAction);
    contextMenu->addSeparator();
    contextMenu->addAction(viewOnBlockExplorer);

    // Connect actions
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(transactionView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(copyTxID()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));
    connect(viewOnBlockExplorer, SIGNAL(triggered()), this, SLOT(viewOnBlockExplorer()));
}

void TransactionView::setModel(WalletModel *model)
{
    this->model = model;
    if(model)
    {
        transactionProxyModel = new TransactionFilterProxy(model, this);
        transactionProxyModel->setSourceModel(model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionView->setModel(transactionProxyModel);
        transactionView->setSortingEnabled(true);
        transactionView->sortByColumn(1);

        //LEAKLEAK
        RichTextDelegate* richDelegate = new RichTextDelegate(this);
        transactionView->setItemDelegateForColumn(4 ,richDelegate);
        transactionView->setItemDelegateForColumn(5 ,richDelegate);
    }


}

void TransactionView::exportClicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Transaction Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(tr("Amount"), 0, TransactionTableModel::FormattedAmountRole);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxIDRole);

    if(!writer.write())
    {
        QMessageBox::critical(this, tr("Error exporting"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    contextMenuTriggerIndex = transactionView->indexAt(point);
    if(contextMenuTriggerIndex.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, contextMenuTriggerIndex.row(), 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, contextMenuTriggerIndex.row(), 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, contextMenuTriggerIndex.row(), 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, contextMenuTriggerIndex.row(), 0, TransactionTableModel::TxIDRole);
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;

    AddressTableModel *addressBook = model->getAddressTableModel();
    if(!addressBook)
        return;
    QString address = contextMenuTriggerIndex.data(TransactionTableModel::AddressRole).toString();
    if(address.isEmpty())
    {
        // If this transaction has no associated address, exit
        return;
    }
    // Is address in address book? Address book can miss address when a transaction is
    // sent from outside the UI.
    int idx = addressBook->lookupAddress(address);
    if(idx != -1)
    {
        // Edit sending / receiving address
        QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
        // Determine type of address, launch appropriate editor dialog type
        QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

        EditAddressDialog dlg(type==AddressTableModel::Receive
                                     ? EditAddressDialog::EditReceivingAddress
                                     : EditAddressDialog::EditSendingAddress,
                              this);
        dlg.setModel(addressBook);
        dlg.loadRow(idx);
        dlg.exec();
    }
    else
    {
        // Add sending address
        EditAddressDialog dlg(EditAddressDialog::NewSendingAddress, this);
        dlg.setModel(addressBook);
        dlg.setAddress(address);
        dlg.exec();
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;

    TransactionDescDialog dlg(contextMenuTriggerIndex);
    dlg.exec();
}

void TransactionView::viewOnBlockExplorer()
{
    QString format("https://chainz.cryptoid.info/pnd/tx.dws?");
    format += contextMenuTriggerIndex.data(TransactionTableModel::TxIDRole).toString();
    QDesktopServices::openUrl(QUrl(format));
}


void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;

    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}


int TransactionView::getBalanceColumnWidth()
{
    return transactionView->columnWidth(5);
}

int TransactionView::getAmountColumnWidth()
{
    return transactionView->columnWidth(4);
}

int TransactionView::getAccountColumnWidth()
{
    return transactionView->columnWidth(3);
}


