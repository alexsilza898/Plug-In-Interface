#include "installerdialog.h"
#include "ui_installerdialog.h"
#include "apploader.h"
#include "unzipper.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryFile>
#include <QSettings>
#include <QComboBox>
#include <QMap>
#include <QDebug>


InstallerDialog::InstallerDialog(AppLoader* apploader, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::InstallerDialog),
    pAppLoader(apploader)
{
    ui->setupUi(this);
    setModal(true);
    setAttribute(Qt::WA_DeleteOnClose);

    Qt::WindowFlags flags = windowFlags();
    flags &= ~Qt::WindowContextHelpButtonHint;
    flags |= Qt::CustomizeWindowHint;
    flags |= Qt::Window;
    setWindowFlags(flags);

    QStringList header;
    header << tr("Application");
    header << tr("Storage");
    header << tr("Installed Version");
    header << tr("Proposed Version");
    header << tr("Action");

    ui->tableWidget->setColumnCount(header.size());
    ui->tableWidget->setHorizontalHeaderLabels(header);
    ui->tableWidget->verticalHeader()->setMinimumWidth(25);

    ui->buttonBox->addButton(tr("Install"), QDialogButtonBox::AcceptRole);
    ui->buttonBox->addButton(QDialogButtonBox::Cancel);
}

InstallerDialog::~InstallerDialog(){
    delete ui;
}

bool InstallerDialog::processPackage(const QString& package){
    packageName = package;
    Unzipper unzipper(packageName);
    if (!unzipper.open()){
        QMessageBox::critical(parentWidget(), tr("Error"),
            tr("Unable to open package file:<br><b>%1</b>").arg(packageName),
            QMessageBox::Close);
        return false;
    }

    QFileInfo fileInfo(packageName);
    ui->labelPackage->setText(tr("Package Name: <b>%1</b>").arg(fileInfo.fileName()));

    QDir globalFolder(pAppLoader->PathApps);
    QDir userFolder(pAppLoader->PathUserApps);

    QTemporaryFile temporaryFile;
    QString temporaryFileName;
    if (!temporaryFile.open()) {
        QMessageBox::critical(parentWidget(), tr("Error"),
            tr("Unable to create temporary file"),
            QMessageBox::Close);
        return false;
    }
    temporaryFileName = temporaryFile.fileName();
    temporaryFile.close();

    ApplicationRecord record;
    int installedCount = 0;
    int newCount = 0;

    for (quint32 i = 0; i < unzipper.entriesCount(); ++i) {
        if (!unzipper.selectEntry(i))
            break;

        QString name = unzipper.entryName();
        int slash = name.indexOf('/');
        if (slash < 0 || name.midRef(slash) != "/AppConfig.ini")
            continue;

        name.truncate(slash);

        if (!unzipper.entryExtract(temporaryFileName))
            continue;

        QSettings manifest(temporaryFileName, QSettings::IniFormat);

        record.clear();
        record.name = name;
        record.proposedVersion = QVersionNumber::fromString(manifest.value("Version").toString());

        bool installed = false;

        if (manifest.value("Type").toString().compare("Plugin", Qt::CaseInsensitive) == 0) {
            record.plugin = true;

            if (addExistingPlugin(record)) {
                installedCount++;
                installed = true;
            }
        } else {
            record.plugin = false;

            if (addExistingApp(record, globalFolder, true)) {
                installedCount++;
                installed = true;
            }

            if (addExistingApp(record, userFolder, false)) {
                installedCount++;
                installed = true;
            }
        }

        if (!installed) {
            record.path.clear();
            newCount++;
            records.append(record);
        }
    }

    populateTable(installedCount, true);
    populateTable(newCount, false);

    ui->tableWidget->resizeRowsToContents();
    ui->tableWidget->resizeColumnsToContents();
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    return true;
}

bool InstallerDialog::addExistingPlugin(ApplicationRecord& record)
{
    QDir folder(pAppLoader->PathPlugins);
    record.path = folder.filePath(record.name + ".ini");
    if (!QFile::exists(record.path)) {
        record.path.clear();
        return false;
    }

    QSettings currentManifest(record.path, QSettings::IniFormat);
    QString version = currentManifest.value("Version").toString();
    record.installedVersion = QVersionNumber::fromString(version);
    record.global = true;
    records.append(record);

    return true;
}

bool InstallerDialog::addExistingApp(ApplicationRecord& record, const QDir& folder, bool global){
    if (!folder.exists(record.name))
        return false;

    record.path = folder.filePath(record.name + "/AppConfig.ini");
    if (!QFile::exists(record.path)) {
        record.path = folder.filePath(record.name + "/Settings.ini");
        if (!QFile::exists(record.path)) {
            record.path.clear();
            return false;
        }
    }

    QSettings currentManifest(record.path, QSettings::IniFormat);
    QString version = currentManifest.value("Version").toString();
    record.installedVersion = QVersionNumber::fromString(version);
    record.global = global;
    records.append(record);

    return true;
}

void InstallerDialog::populateTable(int limit, bool installed){
    if (limit < 1)
        return;

#ifdef Q_OS_WIN
    extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
    qt_ntfs_permission_lookup++; // turn checking on
#endif

    QFileInfo globalFolderInfo(pAppLoader->PathApps);
    bool isGlobalWritable = globalFolderInfo.isWritable();

    QFileInfo pluginsFolderInfo(pAppLoader->PathPlugins);
    bool isPluginsWritable = pluginsFolderInfo.isWritable();

#ifdef Q_OS_WIN
    qt_ntfs_permission_lookup--; // turn it off again
#endif

    int row = ui->tableWidget->rowCount();
    ui->tableWidget->setRowCount(row + limit + 1);

    QTableWidgetItem* itemGroup = new QTableWidgetItem();
    if (installed) {
        itemGroup->setText(tr("Already installed (%1)").arg(limit));
    } else {
        itemGroup->setText(tr("Newly installed (%1)").arg(limit));
    }
    itemGroup->setTextAlignment(Qt::AlignCenter);
    QFont font = itemGroup->font();
    font.setBold(true);
    itemGroup->setFont(font);

    ui->tableWidget->setVerticalHeaderItem(row, new QTableWidgetItem());
    ui->tableWidget->setItem(row, 0, itemGroup);
    ui->tableWidget->setSpan(row, 0, 1, ui->tableWidget->columnCount());
    row++;

    int line = 1;

    for (int i = 0; i < records.count() && line <= limit; ++i) {
        const ApplicationRecord& entity = records.at(i);
        if ((installed && entity.path.isEmpty()) ||
            (!installed && !entity.path.isEmpty()))
            continue;

        QTableWidgetItem* itemNumber = new QTableWidgetItem(QString::number(line));
        itemNumber->setTextAlignment(Qt::AlignCenter);

        QTableWidgetItem* itemName = new QTableWidgetItem(entity.name);
        if (entity.plugin) {
            itemName->setText(tr("Plugin: %1").arg(entity.name));
            QFont font = itemName->font();
            font.setBold(true);
            itemName->setFont(font);
        }

        QTableWidgetItem* itemStorage = new QTableWidgetItem(
                                            entity.global ? tr("Global") : tr("User"));
        itemStorage->setTextAlignment(Qt::AlignCenter);
        if (!installed) {
            itemStorage->setText(tr("not installed"));
            QFont font = itemStorage->font();
            font.setItalic(true);
            itemStorage->setFont(font);
        }

        QTableWidgetItem* itemIVersion = new QTableWidgetItem(entity.installedVersion.toString());
        itemIVersion->setTextAlignment(Qt::AlignCenter);
        if (entity.installedVersion.isNull()) {
            itemIVersion->setText(tr("unknown"));
            QFont font = itemIVersion->font();
            font.setItalic(true);
            itemIVersion->setFont(font);
        }

        QTableWidgetItem* itemPVersion = new QTableWidgetItem(entity.proposedVersion.toString());
        itemPVersion->setTextAlignment(Qt::AlignCenter);
        if (entity.proposedVersion.isNull()) {
            itemPVersion->setText(tr("unknown"));
            QFont font = itemPVersion->font();
            font.setItalic(true);
            itemPVersion->setFont(font);
        }

        QComboBox* comboBox = new QComboBox();
        comboBox->setProperty("action-record", i);
        if (entity.plugin) {
            if (isPluginsWritable) {
                comboBox->addItem(tr("Do nothing"));
                if (entity.path.isEmpty()) {
                    comboBox->addItem(tr("Install"));
                } else {
                    comboBox->addItem(tr("Update"));
                }
                comboBox->setCurrentIndex(1);
            } else {
                comboBox->addItem(tr("Access denied"));
                comboBox->setCurrentIndex(0);
                comboBox->setDisabled(true);
            }

        } else {
            comboBox->addItem(tr("Do nothing"));
            if (entity.global || entity.path.isEmpty()) {
                comboBox->addItem(tr("Install (user)"));
            } else {
                comboBox->addItem(tr("Update (user)"));
            }
            comboBox->setCurrentIndex(1);

            if (isGlobalWritable) {
                if (entity.global && !entity.path.isEmpty()) {
                    comboBox->addItem(tr("Update (global)"));
                    comboBox->setCurrentIndex(2);
                } else {
                    comboBox->addItem(tr("Install (global)"));
                }
                if (entity.path.isEmpty())
                    comboBox->setCurrentIndex(2);
            }

        }

        if (!entity.path.isEmpty() &&
            ((!entity.installedVersion.isNull() && entity.proposedVersion.isNull()) ||
             (entity.installedVersion >= entity.proposedVersion))) {
            comboBox->setCurrentIndex(0);
        }

        ui->tableWidget->setVerticalHeaderItem(row, itemNumber);
        ui->tableWidget->setItem(row, 0, itemName);
        ui->tableWidget->setItem(row, 1, itemStorage);
        if (!installed) {
            ui->tableWidget->setSpan(row, 1, 1, 2);
        } else {
            ui->tableWidget->setItem(row, 2, itemIVersion);
        }
        ui->tableWidget->setItem(row, 3, itemPVersion);
        ui->tableWidget->setCellWidget(row, 4, comboBox);

        row++;
        line++;
    }
}

void InstallerDialog::installApp(Unzipper& unzipper, const ApplicationRecord& entity,
    const QDir& folder){
    for (quint32 i = 0; i < unzipper.entriesCount(); ++i) {
        if (!unzipper.selectEntry(i))
            break;

        if (unzipper.entryIsDirectory())
            continue;

        QString name = unzipper.entryName();
        if (!name.startsWith(entity.name + "/"))
            continue;

        QFileInfo fileInfo = folder.absoluteFilePath(name);
        folder.mkpath(fileInfo.absolutePath());
        unzipper.entryExtract(fileInfo.absoluteFilePath());
    }
}

void InstallerDialog::installPlugin(Unzipper& unzipper, const ApplicationRecord& entity)
{
    QDir pluginsFolder(pAppLoader->PathPlugins);
    QDir rootFolder(pAppLoader->PathRoot);

    QMap<QString, quint32> fileMap;
    QString pathManifest;
    for (quint32 i = 0; i < unzipper.entriesCount(); ++i) {
        if (!unzipper.selectEntry(i))
            break;

        if (unzipper.entryIsDirectory())
            continue;

        QString name = unzipper.entryName();
        int slash = name.indexOf('/');
        if (slash < 0 || name.leftRef(slash) != entity.name)
            continue;

        name = name.mid(slash + 1);

        if (name == "AppConfig.ini") {
            pathManifest = pluginsFolder.absoluteFilePath(entity.name + ".ini");
            unzipper.entryExtract(pathManifest);
        } else {
            fileMap.insert(name, i);
        }
    }

    if (pathManifest.isEmpty())
        return;

    QSettings manifest(pathManifest, QSettings::IniFormat);

    QString section("Default");
#ifdef Q_OS_WIN
    section = "Windows";
#elif Q_OS_DARWIN
    section = "Darwin";
#elif Q_OS_LINUX
    section = "Linux";
#endif

    manifest.beginGroup(section);
    int count = manifest.value("Count", 0).toInt();
    for (int i = 0; i < count; ++i) {
        QStringList fileItem = manifest.value(QString::number(i)).toStringList();
        if (fileItem.isEmpty())
            continue;

        qDebug() << "AppLoader FileItem: " << fileItem;

        auto iterator = fileMap.find(fileItem[0]);
        if (iterator == fileMap.end() || !unzipper.selectEntry(iterator.value()))
            continue;

        if (unzipper.entryIsDirectory())
            continue;

        QString destination;
        if (fileItem.size() > 1) {
            destination = fileItem[1];
            if (destination.endsWith('/'))
                destination += fileItem[0];
            destination = QDir::cleanPath(destination);
            if (destination.startsWith('/'))
                destination.remove(0, 1);
            destination = rootFolder.absoluteFilePath(destination);
        } else {
            destination = pluginsFolder.absoluteFilePath(QDir::cleanPath(fileItem[0]));
        }
        unzipper.entryExtract(destination);
        qDebug() << "AppLoader FileItem extracted to: " << destination;
    }
    manifest.endGroup();
}

void InstallerDialog::on_buttonBox_accepted(){
    Unzipper unzipper(packageName);
    if (!unzipper.open()) {
        QMessageBox::critical(this, tr("Error"),
            tr("Unable to open package file:<br><b>%1</b>").arg(packageName),
            QMessageBox::Close);
        reject();
        return;
    }

    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QComboBox* comboBox = qobject_cast<QComboBox*>(ui->tableWidget->cellWidget(row, 4));
        if (!comboBox || !comboBox->property("action-record").isValid())
            continue;

        // 0 - Do nothing
        // 1 - Install/Update (user)
        // 2 - Install/Update (global)
        if (comboBox->currentIndex() < 1)
            continue;

        QDir folder;
        if (comboBox->currentIndex() < 2) {
            folder.setPath(pAppLoader->PathUserApps);
        } else {
            folder.setPath(pAppLoader->PathApps);
        }

        const ApplicationRecord& entity = records.at(comboBox->property("action-record").toInt());

        if (entity.plugin) {
            installPlugin(unzipper, entity);
        } else {
            installApp(unzipper, entity, folder);

            if (entity.path.isEmpty())
                pAppLoader->EnableApp(folder.absoluteFilePath(entity.name), true);
        }
    }
    accept();
}

