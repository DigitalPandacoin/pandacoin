#include "createaccountwidget.h"
#include "ui_createaccountwidget.h"
#include "walletmodel.h"
#include "key.h"
#include "wallet.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include <QMessageBox>

CreateAccountWidget::CreateAccountWidget(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::CreateAccountWidget)
{
    ui->setupUi(this);

    ui->account_create_button->setEnabled(false);
    connect(ui->account_name_value,SIGNAL(textChanged(QString)),this,SLOT(accountLabelChanged(QString)));
    connect(ui->account_create_button,SIGNAL(pressed()),this,SLOT(createAccount()));
    connect(ui->account_cancel_button,SIGNAL(pressed()),this,SIGNAL(cancelAccountCreation()));
}

CreateAccountWidget::~CreateAccountWidget()
{
    delete ui;
}

void CreateAccountWidget::setModel(WalletModel *model_)
{
    model = model_;
}

void CreateAccountWidget::accountLabelChanged(const QString& newAccountLabel)
{
    ui->account_address_value->setText("");

    if(ui->account_name_value->text() == "")
    {
        ui->account_create_button->setEnabled(false);
    }
    else
    {
        ui->account_create_button->setEnabled(true);
    }
}

void CreateAccountWidget::createAccount()
{
    QString accountName = ui->account_name_value->text();

    if(accountName == "")
        return;

    if(!model->getAddressTableModel()->addressForLabel(accountName).isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("An account with this name already exists."), QMessageBox::Ok);
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    {
        try
        {
            CPubKey key = model->getWallet()->GenerateNewKey();
            QString strAddress = CBitcoinAddress(key.GetID()).ToString().c_str();
            ui->account_address_value->setText(strAddress);

            model->getWallet()->SetAddressBookName(CBitcoinAddress(key.GetID()).Get(), accountName.toStdString());
            ui->account_create_button->setEnabled(false);

            QMessageBox::information(this, tr("Digital PandaBank account created"), tr("Your Digital PandaBank Account has been created."), QMessageBox::Ok);
        }
        catch(...)
        {
            QMessageBox::warning(this, tr("Error"), tr("Error creating Digital PandaBank account."), QMessageBox::Ok);
        }
    }
}
