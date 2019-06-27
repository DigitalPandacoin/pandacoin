#ifndef DONATIONBOOK_H
#define DONATIONBOOK_H

#include <QFrame>

namespace Ui
{
    class DonationBookPage;
}
class WalletModel;
class DonationAddressFilterModel;

class DonationBookPage : public QFrame
{
    Q_OBJECT

public:
    explicit DonationBookPage(QWidget *parent = 0);
    ~DonationBookPage();
    void setModel(WalletModel* model);

private slots:
    void onSearch();
    void onSelectionChanged();
    void onAddressBookSendCoins();
    void addressBookUpdated();
    void onAddressBookDeletePressed();
    void onAddressBookCopyToClipboard();
    void onAddressBookShowQRCode();
    void onAddressBookVerifyMessage();
    void updateDisplayUnit();

signals:
    void onVerifyMessage(QString);

private:
    Ui::DonationBookPage *ui;
    WalletModel* model;
    DonationAddressFilterModel* filterModel;
};

#endif // DONATIONBOOK_H