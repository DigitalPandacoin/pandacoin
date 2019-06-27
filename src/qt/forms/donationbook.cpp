#include "donationbook.h"
#include "ui_donationbook.h"
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

class DonationAddressFilterModel: public QSortFilterProxyModel
{
public:
    DonationAddressFilterModel(QObject* parent=NULL)
    : QSortFilterProxyModel(parent)
    {

    }
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        QVariant vtype = sourceModel()->data(index, AccountModel::TypeRole);
        if (!vtype.isNull() && vtype.toInt() != AddressTableEntry::Donation)
            return false; // only show Donation addresses

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

DonationBookPage::DonationBookPage(QWidget *parent)
: QFrame(parent)
, ui(new Ui::DonationBookPage)
, model(NULL)
, filterModel(NULL)
{
    ui->setupUi(this);

    #ifndef USE_QRCODE
    ui->showQRCode->setVisible(false);
    #endif
}

DonationBookPage::~DonationBookPage()
{
    delete ui;
}

void DonationBookPage::setModel(WalletModel* model_)
{
    model = model_;
    if(model)
    {
        //LEAKLEAK
        SingleColumnAccountModel* listModel=new SingleColumnAccountModel(model->getExternalAccountModel(), false, false);
        filterModel = new DonationAddressFilterModel();
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
        connect(ui->address_book_transfer_from_next_button, SIGNAL(pressed()), this, SLOT(onAddressBookSendCoins()));
        connect(model, SIGNAL(addressBookUpdated()), this, SLOT(addressBookUpdated()));
        connect(ui->copyToClipboard, SIGNAL(pressed()), this, SLOT(onAddressBookCopyToClipboard()));
        connect(ui->showQRCode, SIGNAL(pressed()), this, SLOT(onAddressBookShowQRCode()));
        connect(ui->verifyMessage, SIGNAL(pressed()), this, SLOT(onAddressBookVerifyMessage()));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void DonationBookPage::onSearch()
{
    filterModel->setSearchString(ui->address_searchbox->text());
}

void DonationBookPage::onSelectionChanged()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    QString selectedAccountDescription = model->getAddressTableModel()->descriptionForLabel(selectedAccountLabel);

    ui->account_address_label->setText(selectedAccountLabel);
    ui->addressbook_account_address_value->setText(selectedAccountAddress);
    ui->donationbook_account_description_value->setText(selectedAccountDescription);

    ui->address_book_view_frame->setVisible(true);
}

void DonationBookPage::addressBookUpdated()
{
    filterModel->invalidate();
    ui->address_list->selectionModel()->setCurrentIndex(filterModel->index(0,0),QItemSelectionModel::Select);
    onSelectionChanged();
    ui->address_book_view_frame->setVisible(true);
}

void DonationBookPage::onAddressBookSendCoins()
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

void DonationBookPage::onAddressBookDeletePressed()
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
    ui->address_book_view_frame->setVisible(true);
}

void DonationBookPage::onAddressBookCopyToClipboard()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    QApplication::clipboard()->setText(selectedAccountAddress);
}

void DonationBookPage::onAddressBookShowQRCode()
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


void DonationBookPage::onAddressBookVerifyMessage()
{
    QString selectedAccountLabel = filterModel->data(ui->address_list->selectionModel()->currentIndex()).toString();
    QString selectedAccountAddress = model->getAddressTableModel()->addressForLabel(selectedAccountLabel);
    emit onVerifyMessage(selectedAccountAddress);
}

void DonationBookPage::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        int unit = model->getOptionsModel()->getDisplayUnit();
        ui->address_book_transfer_from_amount->setDisplayUnit(unit);
    }
}