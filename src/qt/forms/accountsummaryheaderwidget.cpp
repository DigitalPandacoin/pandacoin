#include "accountsummaryheaderwidget.h"
#include "ui_accountsummaryheaderwidget.h"

#include "walletmodel.h"
#include "addresstablemodel.h"

#include <QMessageBox>

AccountSummaryHeaderWidget::AccountSummaryHeaderWidget(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::AccountSummaryHeaderWidget)
{
    ui->setupUi(this);
    ui->account_header_line_edit->setVisible(false);
    ui->edit_account_label_button->setVisible(false);
    ui->accept_edit_account_label_button->setVisible(false);
    ui->cancel_edit_account_label_button->setVisible(false);
}

AccountSummaryHeaderWidget::~AccountSummaryHeaderWidget()
{
    delete ui;
}

void AccountSummaryHeaderWidget::update(QString accountLabel,QString accountAddress, QString Balance, QString Available, QString Interest, QString Pending, bool editable)
{
    ui->account_header_label->setText(accountLabel);
    ui->account_address->setText(accountAddress);
    ui->account_balance->setText("<span style='color:#009933;'>+</span> " + Balance);
    ui->available_balance->setText("<span style='color:#009933;'>+</span> " + Available);
    ui->interest_balance->setText("<span style='color:#009933;'>+</span> " + Interest);
    ui->pending_balance->setText("<span style='color:#009933;'>+</span> " + Pending);

    ui->account_header_label->setVisible(true);
    ui->account_header_line_edit->setVisible(false);
    if(editable)
    {
        ui->edit_account_label_button->setVisible(true);
        ui->accept_edit_account_label_button->setVisible(false);
        ui->cancel_edit_account_label_button->setVisible(false);
    }
    else
    {
        ui->edit_account_label_button->setVisible(false);
        ui->accept_edit_account_label_button->setVisible(false);
        ui->cancel_edit_account_label_button->setVisible(false);
    }
}

void AccountSummaryHeaderWidget::accept()
{
    QString oldLabel = ui->account_header_label->text();
    QString newLabel = ui->account_header_line_edit->text();

    ui->account_header_label->setVisible(true);
    ui->account_header_line_edit->setVisible(false);

    if(oldLabel == newLabel)
        return;

    if(!model->getAddressTableModel()->addressForLabel(newLabel).isEmpty())
    {
        QMessageBox::warning(this, tr("Error"), tr("An account with this name already exists."), QMessageBox::Ok);
        return;
    }

    int index = model->getAddressTableModel()->lookupAddress(ui->account_address->text());
    if(index != -1)
    {
        if(!model->getAddressTableModel()->setData(model->getAddressTableModel()->index(index,AddressTableModel::Label,QModelIndex()), newLabel, Qt::EditRole))
        {
            QMessageBox::warning(this, tr("Error"), tr("Error could not change name of Digital PandaBank account."), QMessageBox::Ok);
        }
    }
}

void AccountSummaryHeaderWidget::cancel()
{
    ui->account_header_label->setVisible(true);
    ui->account_header_line_edit->setVisible(false);
    ui->edit_account_label_button->setVisible(true);
    ui->accept_edit_account_label_button->setVisible(false);
    ui->cancel_edit_account_label_button->setVisible(false);
}

void AccountSummaryHeaderWidget::setModel(WalletModel *model_)
{
    model = model_;
}

void AccountSummaryHeaderWidget::setColumnWidths(int pendingWidth, int interestWidth, int availableWidth)
{
    if(pendingWidth < 0 || interestWidth < 0 || availableWidth < 0)
        return;

    ui->pending_balance->setFixedWidth(pendingWidth);
    ui->pending_balance_header->setFixedWidth(pendingWidth);

    ui->interest_balance_header->setFixedWidth(interestWidth);
    ui->interest_balance->setFixedWidth(interestWidth);

    ui->available_balance_header->setFixedWidth(availableWidth);
    ui->available_balance->setFixedWidth(availableWidth);
    ui->account_balance_header->setFixedWidth(availableWidth);
    ui->account_balance->setFixedWidth(availableWidth);
}

void AccountSummaryHeaderWidget::on_edit_account_label_button_pressed()
{
    ui->account_header_line_edit->setText(ui->account_header_label->text());

    ui->account_header_label->setVisible(false);
    ui->edit_account_label_button->setVisible(false);
    ui->accept_edit_account_label_button->setVisible(true);
    ui->cancel_edit_account_label_button->setVisible(true);
    ui->account_header_line_edit->setVisible(true);
}



void AccountSummaryHeaderWidget::on_cancel_edit_account_label_button_pressed()
{
    accept();
}

void AccountSummaryHeaderWidget::on_accept_edit_account_label_button_pressed()
{
    cancel();
}

void AccountSummaryHeaderWidget::on_account_header_line_edit_lostFocus()
{
    cancel();
}

void AccountSummaryHeaderWidget::on_account_header_line_edit_returnPressed()
{
    accept();
}
