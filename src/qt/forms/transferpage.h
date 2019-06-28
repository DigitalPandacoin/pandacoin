#ifndef TRANSFERPAGE_H
#define TRANSFERPAGE_H

#include <QFrame>
#include "walletmodel.h"

namespace Ui {
class TransferPage;
}

class TransferPage : public QFrame
{
    Q_OBJECT

public:
    explicit TransferPage(QWidget *parent = 0);
    ~TransferPage();
    void setModel(WalletModel *model);
    void setFocusToTransferPane();
    void setFocusToAddessBookPane();
    void setFocusToDonationBookPane();
    bool handleURI(const QString &uri);

signals:
    void onVerifyMessage(QString);

private:
    Ui::TransferPage *ui;
};

#endif // TRANSFERPAGE_H
