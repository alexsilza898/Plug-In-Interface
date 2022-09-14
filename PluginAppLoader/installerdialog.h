#ifndef INSTALLERDIALOG_H
#define INSTALLERDIALOG_H


#include <QDialog>
#include <QVersionNumber>
#include <QList>
#include <QDir>


class AppLoader;
class Unzipper;

namespace Ui {
class InstallerDialog;
}


struct ApplicationRecord
{
    QString name;
    QString path;
    QVersionNumber installedVersion;
    QVersionNumber proposedVersion;
    bool global;
    bool plugin;

    void clear(){
        name.clear();
        path.clear();
        installedVersion = QVersionNumber();
        proposedVersion = QVersionNumber();
        global = false;
        plugin = false;
    }
};


class InstallerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstallerDialog(AppLoader *apploader, QWidget* parent = nullptr);
    ~InstallerDialog();

    bool processPackage(const QString& package);
    bool addExistingPlugin(ApplicationRecord& record);
    bool addExistingApp(ApplicationRecord& record, const QDir& folder, bool global);
    void populateTable(int limit, bool installed);
    void installApp(Unzipper& unzipper, const ApplicationRecord& entity, const QDir& folder);
    void installPlugin(Unzipper& unzipper, const ApplicationRecord& entity);

private slots:
    void on_buttonBox_accepted();

private:
    Ui::InstallerDialog* ui;
    AppLoader* pAppLoader;
    QString packageName;
    QList<ApplicationRecord> records;
};

#endif // INSTALLERDIALOG_H
