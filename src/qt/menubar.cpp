#include "ui_menubar.h"
#include "menubar.h"


MenuBar::MenuBar(QWidget *parent)
: QFrame(parent)
, ui(new Ui::MenuBar)
{
    ui->setupUi(this);
}

MenuBar::~MenuBar()
{
    delete ui;
}


void MenuBar::clientModeChanged(ClientMode mode)
{
    switch(mode)
    {
        case ClientFull:
            ui->ModeButton->setIcon(QIcon(":/icons/mode_full"));
            break;
        case ClientLight:
            ui->ModeButton->setIcon(QIcon(":/icons/mode_light"));
            break;
        case ClientHybrid:
        default:
            ui->ModeButton->setIcon(QIcon(":/icons/mode_hybrid"));
            break;
    }
}

void MenuBar::on_ModeButton_clicked()
{
    QPoint pos=ui->FileButton->mapToGlobal(QPoint(0,0));
      pos.setY(pos.y()+ui->FileButton->height());
      emit showAddressBook(pos);
      ui->ModeButton->update();
}

void MenuBar::on_DonateButton_clicked()
{
    QPoint pos=ui->FileButton->mapToGlobal(QPoint(0,0));
      pos.setY(pos.y()+ui->FileButton->height());
      emit showDonationBook(pos);
      ui->ModeButton->update();
}


void MenuBar::on_FileButton_clicked()
{
    QPoint pos=ui->FileButton->mapToGlobal(QPoint(0,0));
    pos.setY(pos.y()+ui->FileButton->height());
    emit showFileMenu(pos);
    ui->FileButton->update();
}

void MenuBar::on_SettingsButton_clicked()
{
    QPoint pos=ui->SettingsButton->mapToGlobal(QPoint(0,0));
    pos.setY(pos.y()+ui->SettingsButton->height());
    emit showSettingsMenu(pos);
    ui->SettingsButton->update();
}

void MenuBar::on_HelpButton_clicked()
{
    QPoint pos=ui->HelpButton->mapToGlobal(QPoint(0,0));
    pos.setY(pos.y()+ui->HelpButton->height());
    emit showHelpMenu(pos);
    ui->HelpButton->update();
}

