#include "transferpage.h"
#include "ui_transferpage.h"

TransferPage::TransferPage(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::TransferPage)
{
    ui->setupUi(this);

    connect(ui->address_pane, SIGNAL(onVerifyMessage(QString)), this, SIGNAL(onVerifyMessage(QString)));
}

TransferPage::~TransferPage()
{
    delete ui;
}

void TransferPage::setModel(WalletModel *model)
{
    ui->transfer_pane->setModel(model);
    ui->address_pane->setModel(model);
    ui->donation_pane->setModel(model); 
}

bool TransferPage::handleURI(const QString &uri)
{
    return ui->transfer_pane->handleURI(uri);
}

void TransferPage::setFocusToTransferPane()
{
    ui->tabWidget->setCurrentIndex(0);
}

void TransferPage::setFocusToAddessBookPane()
{
    ui->tabWidget->setCurrentIndex(1);
}

void TransferPage::setFocusToDonationBookPane()
{
    ui->tabWidget->setCurrentIndex(2);
}