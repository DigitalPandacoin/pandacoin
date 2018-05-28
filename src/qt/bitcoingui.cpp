// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bitcoingui.h"
#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "signverifymessagedialog.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "tabbar.h"
#include "forms/mainframe.h"
#include "forms/lockbar.h"
#include "forms/accountpage.h"
#include "forms/transferpage.h"
#include "menubar.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "pandastyles.h"
#include "checkpoints.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QStyle>
#include <QResizeEvent>
#include <iostream>


// Use this style when we want the progress bar to grab user attention.
QString styleImportant="QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }";

// Use this style when we want the progress bar to run in the 'background' without grabbing user attention.
QString styleBackground="QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #7F7F7F, stop: 1 #DCDDDF); border-radius: 7px; margin: 0px; }";

extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
double GetPoSKernelPS();

BitcoinGUI::BitcoinGUI(QWidget *parent)
: QMainWindow(parent)
, clientModel(NULL)
, walletModel(NULL)
, encryptWalletAction(NULL)
, changePassphraseAction(NULL)
, unlockWalletAction(NULL)
, lockWalletAction(NULL)
, aboutQtAction(NULL)
, trayIcon(NULL)
, notificator(NULL)
, rpcConsole(NULL)
, transferPage(NULL)
{
    resize(850, 550);
    setWindowTitle(tr("Digital PandaBank"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    setUnifiedTitleAndToolBarOnMac(false);
#endif

    // We always want menu icons to be visible on all platforms as this is *not* a native application but a skinned UI.
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, false);

    // Load an application style
    QFile styleFile(":qss/main");
    styleFile.open(QFile::ReadOnly);

    // Use the same style on all platforms to simplify skinning
    QApplication::setStyle("windows");

    // Apply the loaded stylesheet template
    QString style(styleFile.readAll());
    style.replace("gray1","#DCDDDF");//Dark gray
    style.replace("gray2","#F7F7F7");//Light gray
    style.replace("gray3","#EBEBEB");//Mid gray
    style.replace("gray4","#7F7F7F");//Very dark gray
    style.replace("gray5","#BDBDBD");//gray?
    style.replace("gray6","#8B8889");//hint gray

    style.replace("yellow1","#FFCC00");//Dark yellow
    style.replace("yellow2","#F6DF96");//Light yellow
    style.replace("yellow3","#FFE66F");//Mid yellow

    style.replace("BODY_FONT_SIZE",BODY_FONT_SIZE);
    style.replace("HEADER_FONT_SIZE",HEADER_FONT_SIZE);
    style.replace("CURRENCY_FONT_SIZE",CURRENCY_FONT_SIZE);
    style.replace("TOTAL_FONT_SIZE",TOTAL_FONT_SIZE);
    style.replace("CURRENCY_DECIMAL_FONT_SIZE",CURRENCY_DECIMAL_FONT_SIZE);
    style.replace("MAIN_FONTSTACK",MAIN_FONTSTACK);

    setStyleSheet(style);

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    signVerifyMessageDialog = new SignVerifyMessageDialog(this);

    // Create tabs
    overviewPage = new OverviewPage();
    transactionsPage = new AccountPage(pwalletMain,this);
    transferPage = new TransferPage(this);

    centralWidget = new MainFrame(this);
    //fixmeLEAK:
    centralWidget->addTab(overviewPage, tr("My Home"));
    centralWidget->addTab(transactionsPage, tr("View Accounts"));
    //fixme: leak
    centralWidget->addTab(transferPage, tr("Transfers"));
    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    labelEncryptionIcon = new QLabel();
    labelStakingIcon = new QLabel();
    labelConnectionsIcon = new QLabel();
    labelBlocksIcon = new QLabel();
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = qApp->style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet(styleImportant);
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(accountClicked(QString)), this, SLOT(gotoHistoryPage(QString)));
    connect(overviewPage, SIGNAL(requestGotoTransactionPage()), this, SLOT(gotoHistoryPage()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(transferPage, SIGNAL(onVerifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(transactionsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

    //Show menus as needed
    connect(centralWidget->getMenuBar(), SIGNAL(showModeMenu(QPoint)), this, SLOT(showModeMenu(QPoint)));
    connect(centralWidget->getMenuBar(), SIGNAL(showFileMenu(QPoint)), this, SLOT(showFileMenu(QPoint)));
    connect(centralWidget->getMenuBar(), SIGNAL(showSettingsMenu(QPoint)), this, SLOT(showSettingsMenu(QPoint)));
    connect(centralWidget->getMenuBar(), SIGNAL(showHelpMenu(QPoint)), this, SLOT(showHelpMenu(QPoint)));

    connect(centralWidget->getLockBar(), SIGNAL(requestLock()), this, SLOT(lockWallet()));
    connect(centralWidget->getLockBar(), SIGNAL(requestUnlock(bool)), this, SLOT(unlockWallet(bool)));
    connect(centralWidget->getLockBar(), SIGNAL(requestEncrypt(bool)), this, SLOT(encryptWallet(bool)));

    gotoOverviewPage();
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Overview"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send coins"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a Digital PandaBank address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(sendCoinsAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive coins"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(receiveCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/bitcoin"), tr("&About Digital PandaBank"), this);
    aboutAction->setToolTip(tr("Show information about Digital PandaBank"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(QIcon(":/trolltech/qmessagebox/images/qtlogo-64.png"), tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for Digital PandaBank"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Digital PandaBank..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt Digital PandaBank"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Digital PandaBank..."), this);
    backupWalletAction->setToolTip(tr("Backup Digital PandaBank to another location"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Password..."), this);
    changePassphraseAction->setToolTip(tr("Change the password used for wallet encryption"));
    unlockWalletAction = new QAction(QIcon(":/icons/lock_open"), tr("&Unlock Digital PandaBank..."), this);
    unlockWalletAction->setToolTip(tr("Unlock Digital PandaBank"));
    lockWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Lock Digital PandaBank"), this);
    lockWalletAction->setToolTip(tr("Lock Digital PandaBank"));
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);

    clientModeFullAction = new QAction(QIcon(":/icons/mode_full"), tr("Activate 'Classic' client mode."), this);
    clientModeFullAction->setToolTip(tr("Activate 'Classic' client mode."));
    clientModeHybridAction = new QAction(QIcon(":/icons/mode_hybrid"), tr("Activate 'Hybrid' client mode."), this);
    clientModeHybridAction->setToolTip(tr("Activate 'Hybrid' client mode."));
    clientModeLightAction = new QAction(QIcon(":/icons/mode_light"), tr("Activate 'Lite' client mode."), this);
    clientModeLightAction->setToolTip(tr("Activate 'Lite' client mode."));

    exportAction = new QAction(QIcon(":/icons/export"), tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered(bool)), this, SLOT(encryptWallet(bool)));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));


    connect(clientModeFullAction, SIGNAL(triggered()), this, SLOT(setFullClientMode()));
    connect(clientModeHybridAction, SIGNAL(triggered()), this, SLOT(setHybridClientMode()));
    connect(clientModeLightAction, SIGNAL(triggered()), this, SLOT(setLightClientMode()));
}

void BitcoinGUI::showModeMenu(QPoint pos)
{
    QMenu *mode = new QMenu(this);
    mode->addAction(clientModeFullAction);
    mode->addAction(clientModeHybridAction);
    mode->addAction(clientModeLightAction);

    mode->exec(pos,0);
}

void BitcoinGUI::showFileMenu(QPoint pos)
{
    QMenu *file = new QMenu(this);
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(quitAction);

    file->exec(pos,0);
}

void BitcoinGUI::showSettingsMenu(QPoint pos)
{
    QMenu *settings = new QMenu(this);
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addAction(unlockWalletAction);
    settings->addAction(lockWalletAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    settings->exec(pos,0);
}

void BitcoinGUI::showHelpMenu(QPoint pos)
{
    QMenu *help = new QMenu(this);
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);

    help->exec(pos,0);
}


void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("Digital PandaBank client") + QString(" ") + tr("[testnet]"));
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }

            aboutAction->setIcon(QIcon(":/icons/toolbar_testnet"));
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks(), clientModel->getNumBlocksOfPeers());
        connect(clientModel, SIGNAL(numBlocksChanged(int,int)), this, SLOT(setNumBlocks(int,int)));

        // Report errors from network/worker thread
        connect(clientModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        rpcConsole->setClientModel(clientModel);
    }
}

bool BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        centralWidget->getLockBar()->setModel(walletModel);

        // Report errors from wallet thread
        connect(walletModel, SIGNAL(error(QString,QString,bool)), this, SLOT(error(QString,QString,bool)));

        overviewPage->setModel(walletModel);
        transactionsPage->setModel(walletModel);
        signVerifyMessageDialog->setModel(walletModel);
        transferPage->setModel(walletModel);

        // Handle changing of client modes
        connect(walletModel->getOptionsModel(), SIGNAL(clientModeChanged(ClientMode)), centralWidget->getMenuBar(), SLOT(clientModeChanged(ClientMode)) );
        connect(walletModel->getOptionsModel(), SIGNAL(clientModeChanged(ClientMode)), centralWidget->getTabBar(), SLOT(clientModeChanged(ClientMode)) );


        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));


        if(walletModel->getEncryptionStatus() != WalletModel::Unencrypted)
        {
            if(!unlockWallet(true, true))
            {
                return false;
            }
        }
    }
    return true;
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("Digital PandaBank client"));
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon);
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg(this);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg(this);
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%1 active %2 to Digital PandaBank network").arg(count).arg(count == 1 ? tr("connection") : tr("connections")));
}

void BitcoinGUI::setNumBlocks(int count, int nTotalBlocks)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel || clientModel->getNumConnections() == 0)
    {
        if(currentClientMode == ClientFull || currentLoadState == LoadState_AcceptingNewBlocks)
        {
            progressBarLabel->setVisible(false);
            progressBar->setVisible(false);
            return;
        }
        else if(currentLoadState != LoadState_VerifyAllBlocks)
        {
            progressBarLabel->setText(tr("Searching for peers."));
            progressBar->setTextVisible(false);
            progressBarLabel->setVisible(true);
            return;
        }
    }

    QString strStatusBarWarnings = clientModel->getStatusBarWarnings();
    QString tooltip;

    switch(currentLoadState)
    {
        case LoadState_Begin:
        case LoadState_Connect:
        {
            progressBar->setStyleSheet(styleImportant);

            progressBarLabel->setText(tr("Connecting to peers."));
            progressBar->setTextVisible(false);
            progressBarLabel->setVisible(true);
        }
        break;
        case LoadState_CheckPoint:
        {
            progressBar->setStyleSheet(styleImportant);

            int nNumLoadedCheckpoints = Checkpoints::GetNumLoadedCheckpoints();
            int nNumCheckpoints = Checkpoints::GetNumCheckpoints();
            int nRemainingBlocks = nNumCheckpoints-nNumLoadedCheckpoints;
            float nPercentageDone = nNumLoadedCheckpoints / (nNumCheckpoints * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                progressBarLabel->setText(tr("Fetching checkpoints."));
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("checkpoint") : tr("checkpoints")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nNumLoadedCheckpoints);
                progressBar->setValue(nNumCheckpoints);
                progressBar->setVisible(true);
            }
            tooltip = tr("Downloaded %1 of %2 checkpoints (%3% done).").arg(nNumLoadedCheckpoints).arg(nNumCheckpoints).arg(nPercentageDone, 0, 'f', 2);

        }
        break;
        case LoadState_SyncHeadersFromEpoch:
        {
            progressBar->setStyleSheet(styleImportant);

            int nTotalBlocksToSync = nTotalBlocks - epochCheckpointDepth;
            int nNumSynced = numSyncedHeaders;
            int nRemainingBlocks = nTotalBlocksToSync-nNumSynced;
            float nPercentageDone = nNumSynced / (nTotalBlocksToSync * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                QString Label = tr("Performing Instant Sync");
                if(currentClientMode != ClientLight)
                    Label += " " + tr("(Phase 1 of 3)");
                progressBarLabel->setText(Label);
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("header") : tr("headers")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nTotalBlocksToSync);
                progressBar->setValue(nNumSynced);
                progressBar->setVisible(true);
            }

            tooltip = tr("Downloaded %1 of %2 headers (%3% done).").arg(nNumSynced).arg(nTotalBlocksToSync).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_SyncBlocksFromEpoch:
        {
            progressBar->setStyleSheet(styleImportant);

            int nTotalBlocksToSync = nTotalBlocks - epochCheckpointDepth;
            int nNumSynced = numSyncedBlocks;
            int nRemainingBlocks = nTotalBlocksToSync-nNumSynced;
            float nPercentageDone = nNumSynced / (nTotalBlocksToSync * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                QString Label = tr("Performing Instant Sync");
                if(currentClientMode != ClientLight)
                    Label += " " + tr("(Phase 2 of 3)");
                progressBarLabel->setText(Label);
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("block") : tr("blocks")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nTotalBlocksToSync);
                progressBar->setValue(nNumSynced);
                progressBar->setVisible(true);
            }

            tooltip = tr("Downloaded %1 of %2 blocks (%3% done).").arg(nNumSynced).arg(nTotalBlocksToSync).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_ScanningTransactionsFromEpoch:
        {
            progressBar->setStyleSheet(styleImportant);

            int nTotalBlocksToSync = numEpochTransactionsToScan;
            int nNumSynced = numEpochTransactionsScanned;
            int nRemainingBlocks = nTotalBlocksToSync-nNumSynced;
            float nPercentageDone = nNumSynced / (nTotalBlocksToSync * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                QString Label = tr("Scanning wallet transactions");
                if(currentClientMode != ClientLight)
                    Label += " " + tr("(Phase 3 of 3)");
                progressBarLabel->setText(Label);
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("block") : tr("blocks")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nTotalBlocksToSync);
                progressBar->setValue(nNumSynced);
                progressBar->setVisible(true);
            }

            tooltip = tr("Scanned %1 of %2 blocks (%3% done).").arg(nNumSynced).arg(nTotalBlocksToSync).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_SyncAllHeaders:
        {
            progressBar->setStyleSheet(styleBackground);

            int nTotalBlocksToSync = nTotalBlocks;
            int nNumSynced = numSyncedHeaders;
            int nRemainingBlocks = nTotalBlocksToSync-nNumSynced;
            float nPercentageDone = nNumSynced / (nTotalBlocksToSync * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                progressBarLabel->setText(tr("Rapid Blockchain Download (Phase 1 of 2)."));
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("header") : tr("headers")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nTotalBlocksToSync);
                progressBar->setValue(nNumSynced);
                progressBar->setVisible(true);
            }

            tooltip = tr("Downloaded %1 of %2 headers (%3% done).").arg(nNumSynced).arg(nTotalBlocksToSync).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_SyncAllBlocks:
        {
            progressBar->setStyleSheet(styleBackground);

            int nTotalBlocksToSync = nTotalBlocks;
            int nNumSynced = numSyncedBlocks;
            int nRemainingBlocks = nTotalBlocksToSync-nNumSynced;
            float nPercentageDone = nNumSynced / (nTotalBlocksToSync * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                progressBarLabel->setText(tr("Rapid Blockchain Download (Phase 2 of 2)."));
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("block") : tr("blocks")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(nTotalBlocksToSync);
                progressBar->setValue(nNumSynced);
                progressBar->setVisible(true);
            }

            tooltip = tr("Downloaded %1 of %2 blocks (%3% done).").arg(nNumSynced).arg(nTotalBlocksToSync).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_VerifyAllBlocks:
        {
            progressBar->setStyleSheet(styleBackground);

            int nRemainingBlocks = numBlocksToVerify-numBlocksVerified;
            // Assign a double weight to first 100k blocks to try make progress bar smoother.
            float nPercentageDone = ( (numBlocksVerified > 100000 ? 100000 : numBlocksVerified) + numBlocksVerified ) / ( (100000 + numBlocksToVerify) * 0.01f);

            if (strStatusBarWarnings.isEmpty())
            {
                progressBarLabel->setText(tr("Verify blockchain."));
                progressBarLabel->setVisible(true);
                if(nRemainingBlocks <= 0)
                {
                    progressBar->setTextVisible(false);
                }
                else
                {
                    progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("block") : tr("blocks")));
                    progressBar->setTextVisible(true);
                }
                progressBar->setMaximum(100000 + numBlocksToVerify);
                progressBar->setValue((numBlocksVerified > 100000 ? 100000 : numBlocksVerified) + numBlocksVerified);
                progressBar->setVisible(true);
            }

            tooltip = tr("Verified %1 of %2 blocks (%3% done).").arg(numBlocksVerified).arg(numBlocksToVerify).arg(nPercentageDone, 0, 'f', 2);
        }
        break;
        case LoadState_AcceptingNewBlocks:
        default:
        {
            if(count < nTotalBlocks)
            {
                int nRemainingBlocks = nTotalBlocks - count;
                float nPercentageDone = count / (nTotalBlocks * 0.01f);

                if (strStatusBarWarnings.isEmpty())
                {
                    progressBarLabel->setText(tr("Synchronizing with network..."));
                    progressBarLabel->setVisible(true);
                    if(nRemainingBlocks <= 0)
                    {
                        progressBar->setTextVisible(false);
                    }
                    else
                    {
                        progressBar->setFormat(tr("%1 %2 remaining").arg(nRemainingBlocks).arg(nRemainingBlocks == 1 ? tr("block") : tr("blocks")));
                        progressBar->setTextVisible(true);
                    }
                    progressBar->setMaximum(nTotalBlocks);
                    progressBar->setValue(count);
                    progressBar->setVisible(true);
                }

                tooltip = tr("Downloaded %1 of %2 blocks of transaction history (%3% done).").arg(count).arg(nTotalBlocks).arg(nPercentageDone, 0, 'f', 2);
            }
            else
            {
                if (strStatusBarWarnings.isEmpty())
                    progressBarLabel->setVisible(false);

                progressBar->setVisible(false);
                tooltip = tr("Downloaded %1 blocks of transaction history.").arg(count);
            }
        }
    }

    // Override progressBarLabel text and hide progress bar, when we have warnings to display
    if (!strStatusBarWarnings.isEmpty())
    {
        progressBarLabel->setText(strStatusBarWarnings);
        progressBarLabel->setVisible(true);
        progressBar->setVisible(false);
    }

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    int secs = lastBlockDate.secsTo(QDateTime::currentDateTime());
    QString text;

    // Represent time from last generated block in human readable text
    if(secs <= 0)
    {
        // Fully up to date. Leave text empty.
    }
    else if(secs < 60)
    {
        text = tr("%1 %2 ago").arg(secs).arg(secs == 1 ? tr("second") : tr("seconds"));
    }
    else if(secs < 60*60)
    {
        text = tr("%1 %2 ago").arg(secs/60).arg(secs/60 == 1 ? tr("minute") : tr("minutes"));
    }
    else if(secs < 24*60*60)
    {
        text = tr("%1 %2 ago").arg(secs/(60*60)).arg(secs/(60*60) == 1 ? tr("hour") : tr("hours"));
    }
    else
    {
        text = tr("%1 %2 ago").arg(secs/(60*60*24)).arg(secs/(60*60*24) == 1 ? tr("day") : tr("days"));
    }

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60 && count >= nTotalBlocks)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        overviewPage->showOutOfSyncWarning(false);
    }
    else
    {
        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        labelBlocksIcon->setMovie(syncIconMovie);
        syncIconMovie->start();

        overviewPage->showOutOfSyncWarning(true);
    }

    if(!text.isEmpty())
    {
        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1.").arg(text);
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::error(const QString &title, const QString &message, bool modal)
{
    // Report errors from network/worker thread
    if (message != "")
    {
        if(modal)
        {
            QMessageBox::critical(this, title, message, QMessageBox::Ok, QMessageBox::Ok);
        }
        else
        {
            notificator->notify(Notificator::Critical, title, message);
        }
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    QString strMessage =
        tr("This transaction is over the size limit.  You can still send it for a fee of %1, "
          "which goes to the nodes that process your transaction and helps to support the network.  "
          "Do you want to pay the fee?").arg(
                BitcoinUnits::formatWithUnit(BitcoinUnits::BTC, nFeeRequired, true, false));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));

        notificator->notify(Notificator::Information,
                            (amount)<0 ? tr("Sent transaction") :
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Amount: %2\n"
                                 "Type: %3\n"
                                 "Address: %4\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true, true))
                              .arg(type)
                              .arg(address), icon);
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralWidget->setActiveTab(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage(const QString& account)
{
    historyAction->setChecked(true);
    centralWidget->setActiveTab(transactionsPage);
    transactionsPage->setActiveAccount(account);
    transactionsPage->setActivePane(0);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}


void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralWidget->setActiveTab(transferPage);
    transferPage->setFocusToTransferPane();

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    // call show() in showTab_SM()
    signVerifyMessageDialog->showTab_SM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    // call show() in showTab_VM()
    signVerifyMessageDialog->showTab_VM(true);

    if(!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (transferPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Digital PandaBank address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (transferPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid Digital PandaBank address or malformed URI parameters."));
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->show();
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}

void BitcoinGUI::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt:
                                     AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::backupWallet()
{
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    //We purposefully don't pass a parent here to avoid stylesheet propagating - users will except a system default file dialog.
    QString filename = QFileDialog::getSaveFileName(NULL, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

bool BitcoinGUI::unlockWallet(bool forStakingOnly, bool forLogin)
{
    if(!walletModel)
        return false;

    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog::Mode mode;
        if(forLogin)
        {
            mode = AskPassphraseDialog::Login;
        }
        else if(sender() == unlockWalletAction || forStakingOnly)
        {
            mode = AskPassphraseDialog::UnlockStaking;
        }
        else
        {
            mode = AskPassphraseDialog::Unlock;
        }
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        int ret = dlg.exec();
        if(ret == 0)
            return false;
    }
    return true;
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void BitcoinGUI::setFullClientMode()
{
    if(!walletModel)
        return;

    if(currentClientMode == ClientFull)
        return;

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Activate Digital PandaBank ‘Classic’"));
    msgBox.setText(tr("Digital PandaBank 'Classic' allows you to earn interest and help secure the Pandacoin Network once synchronization and download of the blockchain is completed. It operates using the outdated method of synchronization and downloading of the blockchain, which could take between 4 to 24 hours to complete. We recommend most Pandacoin users to use Pandacoin 'Hybrid'.\n\nSwitching to Digital PandaBank 'Classic' from other modes will wipe out your existing blockchain data.\n\nActivate Digital PandaBank 'Classic'?"));
    msgBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIconPixmap(QPixmap(":/icons/mode_full_35"));
    int reply = msgBox.exec();

    if(reply != QMessageBox::Yes)
        return;

    walletModel->getOptionsModel()->setClientMode(ClientFull);
}

void BitcoinGUI::setHybridClientMode()
{
    if(!walletModel)
        return;

    if(currentClientMode == ClientHybrid)
        return;

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Activate Digital PandaBank ‘Hybrid’"));
    msgBox.setText(tr("Digital PandaBank 'Hybrid' is the recommended mode for most Pandacoin users. Synchronization with the Pandacoin Network will only take seconds after installation so you can see and use your Pandacoins immediately. \n\nDigital PandaBank 'Hybrid allows you to earn interest and help secure the Pandacoin Network in approximately 5 to 15 minutes after installation, once both synchronization and download of the blockchain is completed.\n\nActivate Digital PandaBank 'Hybrid'?"));
    msgBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIconPixmap(QPixmap(":/icons/mode_hybrid_35"));
    int reply = msgBox.exec();

    if(reply == QMessageBox::Yes)
        walletModel->getOptionsModel()->setClientMode(ClientHybrid);
}

void BitcoinGUI::setLightClientMode()
{
    if(!walletModel)
        return;

    if(currentClientMode == ClientLight)
        return;

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Activate Digital PandaBank ‘Lite’"));
    msgBox.setText(tr("Digital PandaBank 'Lite' is for users that have access to limited download or hard drive storage space. Stored data is only a few megabytes. Synchronization with the Pandacoin Network will only take seconds after installation so you can see and use your Pandacoins immediately.\n\nPandacoin 'Lite' DOES NOT allow you to earn interest or help secure the Pandacoin Network.\n\nActivate Digital PandaBank 'Lite'?"));
    msgBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    msgBox.setIconPixmap(QPixmap(":/icons/mode_light_35"));
    int reply = msgBox.exec();

    if(reply == QMessageBox::Yes)
        walletModel->getOptionsModel()->setClientMode(ClientLight);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateStakingIcon()
{
    if(currentClientMode == ClientLight)
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_disabled").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Unable to earn interest in light mode.<br/>Switch to hybrid mode if you would like to earn interest."));
        return;
    }
    else if(currentClientMode == ClientHybrid && currentLoadState != LoadState_AcceptingNewBlocks)
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_disabled").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Unable to earn interest until syncing is completed."));
        return;
    }

    uint64_t nMinWeight = 0, nMaxWeight = 0, nWeight = 0;
    if (pwalletMain)
        pwalletMain->GetStakeWeight(*pwalletMain, nMinWeight, nMaxWeight, nWeight);

    if (nLastCoinStakeSearchInterval && nWeight)
    {
        uint64_t nNetworkWeight = GetPoSKernelPS();
        unsigned nEstimateTime = GetTargetSpacing() * nNetworkWeight / nWeight;

        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%1 %2").arg(nEstimateTime).arg(nEstimateTime == 1 ? tr("second") : tr("seconds"));
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%1 %2").arg(nEstimateTime/60).arg(nEstimateTime/60 == 1 ? tr("minute") : tr("minutes"));
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%1 %2").arg(nEstimateTime/(60*60)).arg(nEstimateTime/(60*60) == 1 ? tr("hour") : tr("hours"));
        }
        else
        {
            text = tr("%1 %2").arg(nEstimateTime/(60*60*24)).arg(nEstimateTime/(60*60*24) == 1 ? tr("day") : tr("days"));
        }

        labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
    }
    else
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        if (pwalletMain && pwalletMain->IsLocked())
            labelStakingIcon->setToolTip(tr("Not earning interest because wallet is locked"));
        else if (vNodes.empty())
            labelStakingIcon->setToolTip(tr("Not earning interest because wallet is offline"));
        else if (IsInitialBlockDownload())
            labelStakingIcon->setToolTip(tr("Not earning interest because wallet is syncing"));
        else if (!nWeight)
            labelStakingIcon->setToolTip(tr("Not earning interest because you don't have mature coins"));
        else
            labelStakingIcon->setToolTip(tr("Not earning interest"));
    }
}

void BitcoinGUI::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
}
