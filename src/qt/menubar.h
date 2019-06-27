#ifndef MENUBAR_H
#define MENUBAR_H

#include <QFrame>
#include "main.h" // For ClientMode

namespace Ui
{
    class MenuBar;
}

/** A skinnable menu bar to be shown at the right of the main frame tab bar. */
class MenuBar : public QFrame
{
    Q_OBJECT

public:
    explicit MenuBar(QWidget *parent = 0);
    ~MenuBar();

signals:
    void showModeMenu(QPoint);
    void showFileMenu(QPoint);
    void showSettingsMenu(QPoint);
    void showHelpMenu(QPoint);
    void showAddressBook(QPoint);
    void showDonationBook(QPoint);
    
public slots:
    void clientModeChanged(ClientMode);

private slots:
    void on_ModeButton_clicked();
    void on_FileButton_clicked();
    void on_SettingsButton_clicked();
    void on_HelpButton_clicked();
    void on_DonateButton_clicked();
    
private:
    Ui::MenuBar* ui;
};

#endif
