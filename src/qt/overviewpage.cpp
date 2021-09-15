// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/blockindexdetailsdialog.h>
#include <qt/clientmodel.h>
#include <qt/createnewsdialog.h>
#include <qt/drivenetunits.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/latestblocktablemodel.h>
#include <qt/managenewsdialog.h>
#include <qt/mempooltablemodel.h>
#include <qt/newstablemodel.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/txdetails.h>
#include <qt/walletmodel.h>

#include <QMenu>
#include <QPoint>
#include <QScrollBar>

#include <txdb.h>
#include <validation.h>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1)
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelWalletStatus->setIcon(icon);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));

    createNewsDialog = new CreateNewsDialog(this);
    manageNewsDialog = new ManageNewsDialog(this);
    connect(manageNewsDialog, SIGNAL(NewTypeCreated()), this, SLOT(updateNewsTypes()));
    connect(manageNewsDialog, SIGNAL(NewTypeCreated()), createNewsDialog, SLOT(updateTypes()));

    latestBlockModel = new LatestBlockTableModel(this);
    ui->tableViewBlocks->setModel(latestBlockModel);

    newsModel = new NewsTableModel(this);
    ui->tableViewNews->setModel(newsModel);

    blockIndexDialog = new BlockIndexDetailsDialog(this);

    // Style mempool & block table

    // Resize cells (in a backwards compatible way)
#if QT_VERSION < 0x050000
    ui->tableViewMempool->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewBlocks->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewNews->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
#else
    ui->tableViewMempool->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewBlocks->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableViewNews->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
#endif

    // Don't stretch last cell of horizontal header
    ui->tableViewMempool->horizontalHeader()->setStretchLastSection(false);
    ui->tableViewBlocks->horizontalHeader()->setStretchLastSection(false);

    ui->tableViewNews->horizontalHeader()->setStretchLastSection(true);

    // Hide vertical header
    ui->tableViewBlocks->verticalHeader()->setVisible(false);
    ui->tableViewNews->verticalHeader()->setVisible(false);

    // Left align the horizontal header text
    ui->tableViewBlocks->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->tableViewNews->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);

    // Set horizontal scroll speed to per 3 pixels (very smooth, default is awful)
    ui->tableViewMempool->horizontalHeader()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableViewMempool->horizontalHeader()->horizontalScrollBar()->setSingleStep(3); // 3 Pixels
    ui->tableViewBlocks->horizontalHeader()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableViewBlocks->horizontalHeader()->horizontalScrollBar()->setSingleStep(3); // 3 Pixels
    ui->tableViewNews->horizontalHeader()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->tableViewNews->horizontalHeader()->horizontalScrollBar()->setSingleStep(3); // 3 Pixels

    // Disable word wrap
    ui->tableViewMempool->setWordWrap(false);
    ui->tableViewBlocks->setWordWrap(false);
    ui->tableViewNews->setWordWrap(false);

    // Select rows
    ui->tableViewMempool->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewBlocks->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableViewNews->setSelectionBehavior(QAbstractItemView::SelectRows);

    // Apply custom context menu
    ui->tableViewNews->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableViewMempool->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableViewBlocks->setContextMenuPolicy(Qt::CustomContextMenu);

    // News table context menu
    QAction *showDetailsNewsAction = new QAction(tr("Show full data decode"), this);
    contextMenuNews = new QMenu(this);
    contextMenuNews->setObjectName("contextMenuNews");
    contextMenuNews->addAction(showDetailsNewsAction);

    // Recent txns (mempool) table context menu
    QAction *showDetailsMempoolAction = new QAction(tr("Show transaction details from mempool"), this);
    contextMenuMempool = new QMenu(this);
    contextMenuMempool->setObjectName("contextMenuMempool");
    contextMenuMempool->addAction(showDetailsMempoolAction);

    // Recent block table context menu
    QAction *showDetailsBlockAction = new QAction(tr("Show in block explorer"), this);
    contextMenuBlocks = new QMenu(this);
    contextMenuBlocks->setObjectName("contextMenuBlocks");
    contextMenuBlocks->addAction(showDetailsBlockAction);

    // Connect context menus
    connect(ui->tableViewNews, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenuNews(QPoint)));
    connect(ui->tableViewMempool, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenuMempool(QPoint)));
    connect(ui->tableViewBlocks, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenuBlocks(QPoint)));

    connect(showDetailsNewsAction, SIGNAL(triggered()), this, SLOT(showDetailsNews()));
    connect(showDetailsMempoolAction, SIGNAL(triggered()), this, SLOT(showDetailsMempool()));
    connect(showDetailsBlockAction, SIGNAL(triggered()), this, SLOT(showDetailsBlock()));

    // Setup news type combo box options
    // Start with preset types
    ui->comboBoxNewsType->addItem("All OP_RETURN data");
    ui->comboBoxNewsType->addItem("Tokyo Daily News");
    ui->comboBoxNewsType->addItem("US Daily News");
    // Now add custom news types
    std::vector<CustomNewsType> vCustom;
    popreturndb->GetCustomTypes(vCustom);
    for (const CustomNewsType c : vCustom)
        ui->comboBoxNewsType->addItem(QString::fromStdString(c.title));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

void OverviewPage::on_pushButtonCreateNews_clicked()
{
    createNewsDialog->show();
}

void OverviewPage::on_pushButtonManageNews_clicked()
{
    manageNewsDialog->show();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());

        latestBlockModel->setClientModel(model);

        newsModel->setClientModel(model);
        newsModel->setFilter(COIN_NEWS_ALL);
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::setMemPoolModel(MemPoolTableModel *model)
{
    this->memPoolModel = model;

    if (model)
        ui->tableViewMempool->setModel(memPoolModel);
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
}

void OverviewPage::on_tableViewBlocks_doubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    QMessageBox messageBox;

    QString strHash = index.data(LatestBlockTableModel::HashRole).toString();
    uint256 hash = uint256S(strHash.toStdString());

    // TODO update error message
    if (hash.IsNull()) {
        messageBox.setWindowTitle("Error - invalid block hash!");
        messageBox.setText("Block hash is null!\n");
        messageBox.exec();
        return;
    }

    // TODO update error message
    CBlockIndex* pBlockIndex = latestBlockModel->GetBlockIndex(hash);
    if (!pBlockIndex) {
        messageBox.setWindowTitle("Error - couldn't locate block index!");
        messageBox.setText("Invalid block index!\n");
        messageBox.exec();
        return;
    }

    blockIndexDialog->SetBlockIndex(pBlockIndex);
    blockIndexDialog->show();
}

void OverviewPage::on_tableViewMempool_doubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    QMessageBox messageBox;

    QString strHash = index.data(MemPoolTableModel::HashRole).toString();
    uint256 hash = uint256S(strHash.toStdString());

    // TODO update error message
    if (hash.IsNull()) {
        messageBox.setWindowTitle("Error - invalid block hash!");
        messageBox.setText("Block hash is null!\n");
        messageBox.exec();
        return;
    }

    CTransactionRef txRef;
    if (!memPoolModel->GetTx(hash, txRef)) {
        messageBox.setWindowTitle("Error - not found in mempool!");
        messageBox.setText("Transaction is not in your memory pool!\n");
        messageBox.exec();
        return;
    }

    if (!txRef) {
        return;
    }

    TxDetails detailsDialog;
    detailsDialog.SetTransaction(*txRef);

    detailsDialog.exec();
}

void OverviewPage::on_tableViewNews_doubleClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    QString strNews = index.data(NewsTableModel::NewsRole).toString();

    QMessageBox messageBox;
    messageBox.setWindowTitle("News");
    messageBox.setText(strNews);
    messageBox.exec();
}

void OverviewPage::on_comboBoxNewsType_currentIndexChanged(int index)
{
    newsModel->setFilter(index);
}

void OverviewPage::contextualMenuNews(const QPoint &point)
{
    QModelIndex index = ui->tableViewNews->indexAt(point);
    if (index.isValid())
        contextMenuNews->popup(ui->tableViewNews->viewport()->mapToGlobal(point));
}

void OverviewPage::contextualMenuMempool(const QPoint &point)
{
    QModelIndex index = ui->tableViewMempool->indexAt(point);
    if (index.isValid())
        contextMenuMempool->popup(ui->tableViewMempool->viewport()->mapToGlobal(point));
}

void OverviewPage::contextualMenuBlocks(const QPoint &point)
{
    QModelIndex index = ui->tableViewBlocks->indexAt(point);
    if (index.isValid())
        contextMenuBlocks->popup(ui->tableViewBlocks->viewport()->mapToGlobal(point));
}

void OverviewPage::showDetailsNews()
{
    if (!ui->tableViewNews->selectionModel())
        return;

    QModelIndexList selection = ui->tableViewNews->selectionModel()->selectedRows();
    if (!selection.isEmpty())
        on_tableViewNews_doubleClicked(selection.front());
}

void OverviewPage::showDetailsMempool()
{
    if (!ui->tableViewMempool->selectionModel())
        return;

    QModelIndexList selection = ui->tableViewMempool->selectionModel()->selectedRows();
    if (!selection.isEmpty())
        on_tableViewMempool_doubleClicked(selection.front());
}

void OverviewPage::showDetailsBlock()
{
    if (!ui->tableViewBlocks->selectionModel())
        return;

    QModelIndexList selection = ui->tableViewBlocks->selectionModel()->selectedRows();
    if (!selection.isEmpty())
        on_tableViewBlocks_doubleClicked(selection.front());
}

void OverviewPage::updateNewsTypes()
{
    ui->comboBoxNewsType->clear();

    // Setup news type combo box options
    // Start with preset types
    ui->comboBoxNewsType->addItem("All OP_RETURN data");
    ui->comboBoxNewsType->addItem("Tokyo Daily News");
    ui->comboBoxNewsType->addItem("US Daily News");
    // Now add custom news types
    std::vector<CustomNewsType> vCustom;
    popreturndb->GetCustomTypes(vCustom);
    for (const CustomNewsType c : vCustom)
        ui->comboBoxNewsType->addItem(QString::fromStdString(c.title));
}
