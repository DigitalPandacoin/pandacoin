#include "addressbookpage_new.h"
#include "ui_addressbookpage_new.h"
#include "walletmodel.h"
#include "wallet.h"
#include "accountmodel.h"
#include "optionsmodel.h"
#include "guiutil.h"
#include "addresstablemodel.h"
#include "richtextdelegate.h"
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QListView>
#include <qrcodedialog.h>
#include "editaddressdialog.h"
#include "signverifymessagedialog.h"

class AddressFilterModel: public QSortFilterProxyModel
{
public:
    AddressFilterModel(QObject* parent=NULL)
    : QSortFilterProxyModel(parent)
    {

    }
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        QVariant vtype = sourceModel()->data(index, AccountModel::TypeRole);
        if (!vtype.isNull() && vtype.toInt() == AddressTableEntry::Donation)
            return false; // skip donation addresses
        if(index.data().toString().contains(filterString))
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    bool filterAcceptsColumn(int sourceColumn, const QModelIndex &sourceParent) const
    {
        return true;
    }
    void setSearchString(const QString& searchString)
    {
        filterString = searchString;
        invalidateFilter();
    }
private:
    QString filterString;
};

AddressBookPage_new::AddressBookPage_new(QWidget *parent)
: QFrame(parent)
, ui(new Ui::AddressBookPage_new)
, model(NULL)
, filterModel(NULL)
{
    ui->setupUi(this);

    ui->address_book_edit_frame->setVisible(false);

    #ifndef USE_QRCODE
    ui->showQRCode->setVisible(false);
    #endif
}

AddressBookPage_new::~AddressBookPage_new()
{
    delete ui;
}

void AddressBookPage_new::setModel(WalletModel* model_)
{
    model = model_;
    if(model)
    {
        //LEAKLEAK
        SingleColumnAccountModel* listModel=new SingleColumnAccountModel(model->getExternalAccountModel(), false, false);
        filterModel = new AddressFilterModel();
        filterModel->setSourceModel(listModel);
        ui->address_list->setModel(filterModel);
        ui->address_list->selectionModel()->setCurrentIndex(filterModel->index(0,0),QItemSelectionModel::Select);
        onSelectionChanged();

        if(filterModel->rowCount() == 0)
        {
            ui->address_book_view_frame->setVisible(false);
        }

        //LEAKLEAK
        SingleColumnAccountModel* fromListModel=new SingleColumnAccountModel(model->getMyAccountModel(),true,false);
        ui->address_book_transfer_from_combobox->setModel(fromListModel);
        //LEAKLEAK
        RichTextDelegate* delegate = new RichTextDelegate(this);
        ui->address_book_transfer_from_combobox->setItemDelegate(delegate);
        // Sadly the below is necessary in order to be able to style QComboBox pull down lists properly.
        ui->address_book_transfer_from_combobox->setView(new QListView(this));

        connect(ui->address_searchbox, SIGNAL(returnPressed()), this, SLOT(onSearch()));
        connect(ui->address_search_button, SIGNAL(pressed()), this, SLOT(onSearch()));
        connect(ui->address_list->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(onSelectionChanged()));
        connect(ui->address_book_edit_button, SIGNAL(pressed()), this, SLOT(onAddressBookEdit()));
        connect(ui->address_book_transfer_from_next_button, SIGNAL(pressed()), this, SLOT(onAddressBookSendCoins()));
        connect(ui->address_book_done_button, SIGNAL(pressed()), this, SLOT(onAddressBookChangeDone()));
        connect(model, SIGNAL(addressBookUpdated()), this, SLOT(addressBookUpdated()));
        connect(ui->address_book_delete_button, SIGNAL(pressed()), this, SLOT(onAddressBookDeletePressed()));
        connect(ui->deleteButton, SIGNAL(pressed()), this, SLOT(onAddressBookDeletePressed()));
        connect(ui->copyToClipboard, SIGNAL(pressed()), this, SLOT(onAddressBookCopyToClipboard()));
        connect(ui->showQRCode, SIGNAL(pressed()), this, SLOT(onAddressBookShowQRCode()));
        connect(ui->verifyMessage, SIGNAL(pressed()), this, SLOT(onAddressBookVerifyMessage()));
        connect(ui->newAddressButton, SIGNAL(pressed()), this, SLOT(onAddressBookNewAddress()));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void AddressBookPage_new::onSearch()
{
    filterModel->setSearchString(ui->address_searchbox->text());
}

void AddressBookPage_new::onSelectionChanged()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);

    ui->account_address_label->setText(selectedAccountLabel);
    ui->addressbook_account_address_value->setText(selectedAccountAddress);
    ui->account_name_editbox->setText(selectedAccountLabel);
    ui->account_address_editbox->setText(selectedAccountAddress);

    ui->address_book_edit_frame->setVisible(false);
    ui->address_book_view_frame->setVisible(true);
}

void AddressBookPage_new::addressBookUpdated()
{
    filterModel->invalidate();
    ui->address_list->selectionModel()->setCurrentIndex(filterModel->index(0,0),QItemSelectionModel::Select);
    onSelectionChanged();
    ui->address_book_edit_frame->setVisible(false);
    ui->address_book_view_frame->setVisible(true);
}

void AddressBookPage_new::onAddressBookEdit()
{
    ui->address_book_edit_frame->setVisible(true);
    ui->address_book_view_frame->setVisible(false);
}


void AddressBookPage_new::onAddressBookSendCoins()
{
    std::vector<SendCoinsRecipient> recipients;

    if(!model)
        return;

    if(!ui->address_book_transfer_from_amount->validate())
    {
        ui->address_book_transfer_from_amount->setValid(false);
        return;
    }
    else
    {
        if(ui->address_book_transfer_from_amount->value() <= 0)
        {
            // Cannot send 0 coins or less
            ui->address_book_transfer_from_amount->setValid(false);
            return;
        }
    }
    if(ui->address_book_transfer_from_combobox->currentIndex() < 0)
    {
        //fixme: indicate invalid somehow
        //ui->quick_transfer_to_combobox->setValid(false);
        return;
    }

    int fromIndex = ui->address_book_transfer_from_combobox->currentIndex();
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    std::string fromAccountAddress = model->getMyAccountModel()->data(AccountModel::Address, fromIndex).toString().trimmed().toStdString();
    qint64 amt=ui->address_book_transfer_from_amount->value();

    recipients.push_back(SendCoinsRecipient(amt, selectedAccountAddress.toStdString(), selectedAccountLabel.toStdString()));

    std::string transactionHash;
    if(GUIUtil::SendCoinsHelper(this, recipients, model, fromAccountAddress, true, transactionHash))
    {
        ui->address_book_transfer_from_amount->clear();
        ui->address_book_transfer_from_combobox->clear();
    }
}

void AddressBookPage_new::onAddressBookChangeDone()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    QString newAccountLabel = ui->account_name_editbox->text();
    QString newAccountAddress = ui->account_address_editbox->text();

    int index = model->getAddressTableModel()->lookupAddress(selectedAccountAddress);
    if(index != -1)
    {
        if(newAccountAddress != selectedAccountAddress)
        {
            model->getAddressTableModel()->setData(model->getAddressTableModel()->index(index,AddressTableModel::Address,QModelIndex()), newAccountAddress, Qt::EditRole);
        }
        if(newAccountLabel != selectedAccountLabel)
        {
            model->getAddressTableModel()->setData(model->getAddressTableModel()->index(index,AddressTableModel::Label,QModelIndex()), newAccountLabel, Qt::EditRole);
        }
    }
    ui->address_book_edit_frame->setVisible(false);
    ui->address_book_view_frame->setVisible(true);
}

void AddressBookPage_new::onAddressBookDeletePressed()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);

    int index = model->getAddressTableModel()->lookupAddress(selectedAccountAddress);
    if(index != -1)
    {
        model->getAddressTableModel()->removeRows(index, 1, QModelIndex());
        filterModel->invalidate();
        ui->address_list->selectionModel()->setCurrentIndex(filterModel->index(0,0),QItemSelectionModel::Select);
        onSelectionChanged();
    }
    ui->address_book_edit_frame->setVisible(false);
    ui->address_book_view_frame->setVisible(true);
}

void AddressBookPage_new::onAddressBookCopyToClipboard()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    QApplication::clipboard()->setText(selectedAccountAddress);
}

void AddressBookPage_new::onAddressBookShowQRCode()
{
    #ifdef USE_QRCODE
        QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
        QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
        QRCodeDialog *dialog = new QRCodeDialog(selectedAccountAddress, selectedAccountLabel, false , this);
        dialog->setModel(model->getOptionsModel());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    #endif
}


void AddressBookPage_new::onAddressBookVerifyMessage()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    emit onVerifyMessage(selectedAccountAddress);
}

void AddressBookPage_new::onAddressBookNewAddress()
{
    if(!model)
        return;
    EditAddressDialog dlg(EditAddressDialog::NewSendingAddress, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        QString newAddressToSelect = dlg.getAddress();
        //ui->address_list->selectionModel()->setCurrentIndex(filterModel->index(newAddressToSelect,0),QItemSelectionModel::Select);
        onSelectionChanged();
        ui->address_book_edit_frame->setVisible(true);
        ui->address_book_view_frame->setVisible(false);
    }
}

void AddressBookPage_new::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        int unit = model->getOptionsModel()->getDisplayUnit();
        ui->address_book_transfer_from_amount->setDisplayUnit(unit);
    }
}