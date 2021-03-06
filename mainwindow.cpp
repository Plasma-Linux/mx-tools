/**********************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of MX Tools.
 *
 * MX Tools is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Tools is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Tools.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScreen>
#include <QTextEdit>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "flatbutton.h"
#include "version.h"

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    qDebug().noquote() << qApp->applicationName() << "version:" << VERSION;
    ui->setupUi(this);
    setConnections();
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    // detect if tools are displayed in the menu (check for only one since all are set at the same time)
    if (system("grep -q \"NoDisplay=true\" /home/$USER/.local/share/applications/mx-user.desktop >/dev/null 2>&1") == 0)
        ui->checkHide->setChecked(true);

    QString search_folder = "/usr/share/applications";
    live_list = listDesktopFiles("MX-Live", search_folder);
    maintenance_list = listDesktopFiles("MX-Maintenance", search_folder);
    setup_list = listDesktopFiles("MX-Setup", search_folder);
    software_list = listDesktopFiles("MX-Software", search_folder);
    utilities_list = listDesktopFiles("MX-Utilities", search_folder);

    QVector<QStringList *> lists {
                &live_list,
                &maintenance_list,
                &setup_list,
                &software_list,
                &utilities_list };

    QString test = getCmdOut("df -T / |tail -n1 |awk '{print $2}'");

    // remove mx-remastercc and live-kernel-updater from list if not running Live
    bool live = (test == "aufs" || test == "overlay");
    if (!live) {
        const QStringList live_list_copy = live_list;
        for (const QString &item : live_list_copy)
            if (item.contains("mx-remastercc.desktop") || item.contains("live-kernel-updater.desktop"))
                live_list.removeOne(item);
    }

    for (auto &list : lists)
        removeEnvExclusive(*list, live);

    // remove item from list if it is only meant for XFCE
    if (qgetenv("XDG_CURRENT_DESKTOP") != "XFCE")
        for (auto &list : lists)
            removeXfceOnly(*list);

    // remove item from list if it is only meant for FLUXBOX
    if (qgetenv("XDG_SESSION_DESKTOP") != "fluxbox")
        for (auto &list : lists)
            removeFLUXBOXonly(*list);

    category_map.insert("MX-Live", live_list);
    category_map.insert("MX-Maintenance", maintenance_list);
    category_map.insert("MX-Setup", setup_list);
    category_map.insert("MX-Software", software_list);
    category_map.insert("MX-Utilities", utilities_list);

    readInfo(category_map);
    addButtons(info_map);
    ui->textSearch->setFocus();
    this->adjustSize();
    QSize size = this->size();
    restoreGeometry(settings.value("geometry").toByteArray());
    if (this->isMaximized()) {  // if started maximized give option to resize to normal window size
        this->resize(size);
        QRect screenGeometry = qApp->primaryScreen()->geometry();
        int x = (screenGeometry.width() - this->width()) / 2;
        int y = (screenGeometry.height() - this->height()) / 2;
        this->move(x, y);
    }
    icon_size = settings.value("icon_size", icon_size).toInt();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setConnections()
{
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->checkHide, &QCheckBox::clicked, this, &MainWindow::checkHide_clicked);
    connect(ui->textSearch, &QLineEdit::textChanged, this, &MainWindow::textSearch_textChanged);
}

QString MainWindow::getCmdOut(const QString &cmd)
{
    proc = new QProcess(this);
    proc->start("/bin/bash", {"-c", cmd});
    proc->setReadChannel(QProcess::StandardOutput);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->waitForFinished(-1);
    auto result = proc->readAllStandardOutput().trimmed();
    delete proc;
    return result;
}

// List .desktop files that contain a specific string
QStringList MainWindow::listDesktopFiles(const QString &search_string, const QString &location)
{
    QStringList listDesktop;
    QString cmd = QString("grep -Elr %1 %2 | sort").arg(search_string, location);
    QString out = getCmdOut(cmd);
    if (!out.isEmpty())
        listDesktop = out.split("\n");
    return listDesktop;
}

// Load info (name, comment, exec, icon_name, category, terminal) to the info_map
void MainWindow::readInfo(const QMultiMap<QString, QStringList> &category_map)
{
    QString name;
    QString comment;
    QString exec;
    QString icon_name;
    QString terminal_switch;
    QStringList list;
    QLocale locale;
    QString lang = locale.name().split('_').first();
    QString lang_region = locale.name();
    QMultiMap<QString, QStringList> map;

    QRegularExpression re;
    re.setPatternOptions(QRegularExpression::MultilineOption);
    QMapIterator<QString, QStringList> it(category_map);
    QString category;
    while (it.hasNext()) {
        category = it.next().key();
        list = category_map.value(category);
        for (const QString &file_name : qAsConst(list)) {
            QFile file(file_name);
            if (!file.open(QFile::Text | QFile::ReadOnly))
                continue;
            QString text = file.readAll();
            file.close();
            name.clear();
            comment.clear();
            if (lang != QLatin1String("en")) {
                re.setPattern(QLatin1String("^Name\\[") + lang_region + QLatin1String("\\]=(.*)$"));
                name = re.match(text).captured(1);
                if (name.isEmpty()) { // check lang
                    re.setPattern(QLatin1String("^Name\\[") + lang + QLatin1String("\\]=(.*)$"));
                    name = re.match(text).captured(1);
                }
                re.setPattern(QLatin1String("^Comment\\[") + lang_region + QLatin1String("\\]=(.*)$"));
                comment = re.match(text).captured(1);
                if (comment.isEmpty()) { // check lang
                    re.setPattern(QLatin1String("^Comment\\[") + lang + QLatin1String("\\]=(.*)$"));
                    comment = re.match(text).captured(1);
                }
            }
            if (lang_region == QLatin1String("pt_BR")) { // not using Portuguese [pt] for Brazilian Portuguese [pt_BR]
                re.setPattern(QLatin1String("^Name\\[") + lang_region + QLatin1String("\\]=(.*)$"));
                name = re.match(text).captured(1);
                re.setPattern(QLatin1String("^Comment\\[") + lang_region + QLatin1String("\\]=(.*)$"));
                comment = re.match(text).captured(1);
            }
            if (name.isEmpty()) { // backup if Name is not translated
                re.setPattern(QLatin1String("^Name=(.*)$"));
                name = re.match(text).captured(1);
                name = name.remove(QRegularExpression(QLatin1String("^MX "))).replace(QLatin1Char('&'), QLatin1String("&&"));
            }
            if (comment.isEmpty()) { // backup if Comment is not translated
                re.setPattern(QLatin1String("^Comment=(.*)$"));
                comment = re.match(text).captured(1);
            }
            re.setPattern(QLatin1String("^Exec=(.*)$"));
            exec = re.match(text).captured(1);
            re.setPattern(QLatin1String("^Icon=(.*)$"));
            icon_name = re.match(text).captured(1);
            re.setPattern(QLatin1String("^Terminal=(.*)$"));
            terminal_switch = re.match(text).captured(1);
            QStringList info;
            map.insert(file_name, info << name << comment << icon_name << exec << category << terminal_switch);
        }
        info_map.insert(category, map);
        map.clear();
    }
}

// read the info_map and add the buttons to the UI
void MainWindow::addButtons(const QMultiMap<QString, QMultiMap<QString, QStringList> > &info_map)
{
    int col = 0;
    int row = 0;
    const int max  = this->width() / 200;

    max_elements = 0;
    QMapIterator<QString, QMultiMap<QString, QStringList>> it(info_map);
    QString category;
    while (it.hasNext()) {
        category = it.next().key();
        if (info_map.value(category).keys().count() > max_elements)
            max_elements = info_map.value(category).keys().count();
    }

    QString name;
    QString comment;
    QString exec;
    QString icon_name;
    QString terminal_switch;

    it.toFront();
    while (it.hasNext()) {
        category = it.next().key();
        if (!info_map.value(category).isEmpty()) {
            // add empty row and delimiter except for the first row
            if (row != 0) {
                ++row;
                auto *line = new QFrame();
                line->setFrameShape(QFrame::HLine);
                line->setFrameShadow(QFrame::Sunken);
                ui->gridLayout_btn->addWidget(line, row, 0, 1, -1);
            }
            auto *label = new QLabel();
            QFont font;
            font.setBold(true);
            font.setUnderline(true);
            label->setFont(font);
            QString label_txt = category;
            label_txt.remove(QRegularExpression("^MX-"));
            label->setText(label_txt);
            ++row;
            ui->gridLayout_btn->addWidget(label, row, 0);
            ++row;
            col = 0;
            QMapIterator<QString, QStringList> it(info_map.value(category));
            QString file_name;
            while (it.hasNext()) {
                file_name = it.next().key();
                if (col >= col_count)
                    col_count = col + 1;
                QStringList file_info = info_map.value(category).value(file_name);
                name = file_info.at(Info::Name);
                comment = file_info.at(Info::Comment);
                icon_name = file_info.at(Info::IconName);
                exec = file_info.at(Info::Exec);
                terminal_switch = file_info.at(Info::Terminal);
                btn = new FlatButton(name);
                btn->setToolTip(comment);
                btn->setAutoDefault(false);
                btn->setIcon(findIcon(icon_name));
                btn->setIconSize(icon_size, icon_size);
                ui->gridLayout_btn->addWidget(btn, row, col);
                //ui->gridLayout_btn->setRowStretch(row, 0);
                ++col;
                if (col >= max) {
                    col = 0;
                    ++row;
                }
                QString cmd = "x-terminal-emulator -e ";
                if (terminal_switch == "true")
                    btn->setObjectName(cmd + exec); // add the command to be executed to the object name
                else
                    btn->setObjectName(exec); // add the command to be executed to the object name
                QObject::connect(btn, &FlatButton::clicked, this, &MainWindow::btn_clicked);
            }
        }
    }
    ui->gridLayout_btn->setRowStretch(row + 2, 1);
}

QIcon MainWindow::findIcon(QString icon_name)
{
    if (icon_name.isEmpty())
        return QIcon();
    if (QFileInfo::exists("/" + icon_name))
        return QIcon(icon_name);

    QString search_term = icon_name;
    if (!icon_name.endsWith(".png") && !icon_name.endsWith(".svg") && !icon_name.endsWith(".xpm"))
        search_term = icon_name + ".*";

    icon_name.remove(QRegularExpression(R"(\.png$|\.svg$|\.xpm$)"));

    // return the icon from the theme if it exists
    if (QIcon::hasThemeIcon(icon_name))
        return QIcon::fromTheme(icon_name);

    // Try to find in most obvious places
    QStringList search_paths { QDir::homePath() + "/.local/share/icons/",
                               "/usr/share/pixmaps/",
                               "/usr/local/share/icons/",
                               "/usr/share/icons/hicolor/48x48/apps/" };
    for (const QString &path : search_paths) {
        if (!QFileInfo::exists(path)) {
            search_paths.removeOne(path);
            continue;
        }
        for (const QString &ext : {".png", ".svg", ".xpm"} ) {
            QString file = path + icon_name + ext;
            if (QFileInfo::exists(file))
                return QIcon(file);
        }
    }

    // Search recursive
    search_paths.append("/usr/share/icons/hicolor/48x48/");
    search_paths.append("/usr/share/icons/hicolor/");
    search_paths.append("/usr/share/icons/");
    QString out = getCmdOut("find " + search_paths.join(" ") + " -iname \"" + search_term
                                   + "\" -print -quit 2>/dev/null");
    return (!out.isEmpty()) ? QIcon(out) : QIcon();
}

void MainWindow::btn_clicked()
{
    this->hide();
    system(sender()->objectName().toUtf8());
    this->show();
}

void MainWindow::closeEvent(QCloseEvent *)
{
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    if (event->oldSize().width() == event->size().width())
        return;
    int new_count = this->width() / 200;
    if (this->width() / 200 != col_count) {
        if (new_count > max_elements && col_count == max_elements)
            return;
        col_count = 0;
        if (ui->textSearch->text().isEmpty()) {
            QLayoutItem *child = nullptr;
            while ((child = ui->gridLayout_btn->takeAt(0)) != nullptr) {
                delete child->widget();
                delete child;
            }
            addButtons(info_map);
        } else {
            textSearch_textChanged(ui->textSearch->text());
        }
    }
}

// hide icons in menu checkbox
void MainWindow::checkHide_clicked(bool checked) {
    for (const QStringList &list : qAsConst(category_map))
        for (const QString &file_name : qAsConst(list))
            hideShowIcon(file_name, checked);
    system("sh -c 'which xfce4-panel >/dev/null 2>/dev/null && xfce4-panel --restart'");
}

// hide or show icon for .desktop file
void MainWindow::hideShowIcon(const QString &file_name, bool hide)
{
    QFileInfo file(file_name);
    QString file_name_home = QDir::homePath() + "/.local/share/applications/" + file.fileName();
    if (!hide) {
        QFile::remove(file_name_home);
    } else {
        QFile::copy(file_name, file_name_home);
        QString cmd  = "sed -i -r -e '/^(NoDisplay|Hidden)=/d' ";
        cmd += "-e '/Exec/aNoDisplay=true' " ;
        cmd += file_name_home;
        system(cmd.toUtf8());
    }
}

void MainWindow::pushAbout_clicked()
{
    this->hide();
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Tools"), "<p align=\"center\"><b><h2>" +
                       tr("MX Tools") + "</h2></b></p><p align=\"center\">" + tr("Version: ") +
                       VERSION + "</p><p align=\"center\"><h3>" + tr("Configuration Tools for MX Linux") +
                       R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    QPushButton *btnLicense = msgBox.addButton(tr("License"), QMessageBox::HelpRole);
    QPushButton *btnChangelog = msgBox.addButton(tr("Changelog"), QMessageBox::HelpRole);
    QPushButton *btnCancel = msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        system("mx-viewer file:///usr/share/doc/mx-tools/license.html 'MX Tools License'");
    } else if (msgBox.clickedButton() == btnChangelog) {
        auto *changelog = new QDialog(this);
        changelog->resize(600, 500);

        auto *text = new QTextEdit;
        text->setReadOnly(true);
        text->setText(getCmdOut("zless /usr/share/doc/" +
                                QFileInfo(QCoreApplication::applicationFilePath()).fileName()  + "/changelog.gz"));

        auto *btnClose = new QPushButton(tr("&Close"));
        btnClose->setIcon(QIcon::fromTheme("window-close"));
        connect(btnClose, &QPushButton::clicked, changelog, &QDialog::close);

        auto *layout = new QVBoxLayout;
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
    }
    this->show();
}

void MainWindow::pushHelp_clicked()
{
    QString cmd;

    if (QFile::exists("/usr/bin/mx-manual"))
        cmd = QString("mx-manual");
    else
        cmd = QString("mx-viewer file:///usr/local/share/doc/mxum.html#toc-Subsection-3.2");

    system(cmd.toUtf8());
}

void MainWindow::textSearch_textChanged(const QString &arg1)
{
    // Remove all items from the layout
    QLayoutItem *child = nullptr;
    while ((child = ui->gridLayout_btn->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    QMultiMap<QString, QMultiMap<QString, QStringList> > new_map;
    QMultiMap<QString, QStringList> map;

    // Create a new_map with items that match the search argument
    QMapIterator<QString, QMultiMap<QString, QStringList>> it(info_map);
    QString category;
    while(it.hasNext()) {
        category = it.next().key();
        QMultiMap<QString, QStringList> file_info =  info_map.value(category);
        for (const QString &file_name : category_map.value(category)) {
            //qDebug() << file_name;
            QString name = file_info.value(file_name)[0];
            QString comment = file_info.value(file_name)[1];
            QString category = file_info.value(file_name)[4];
            if (name.contains(arg1, Qt::CaseInsensitive) || comment.contains(arg1, Qt::CaseInsensitive)
                    || category.contains(arg1, Qt::CaseInsensitive)) {
                map.insert(file_name, info_map.value(category).value(file_name));
            }
        }
        if (!map.isEmpty()) {
            new_map.insert(category, map);
            map.clear();
        }
    }
    if (!new_map.isEmpty()) {
        arg1.isEmpty() ? addButtons(info_map) : addButtons(new_map);
    }
}

// Remove Xfce-only apps from the list
void MainWindow::removeXfceOnly(QStringList &list)
{
    const QStringList list_copy = list;
    for (const QString &file_name : list_copy) {
        QFile file(file_name);
        if (!file.open(QFile::Text | QFile::ReadOnly))
            continue;
        QString text = file.readAll();
        file.close();
        if (text.contains(QLatin1String("OnlyShowIn=XFCE")))
            list.removeOne(file_name);
    }
}

// Remove FLUXBOX-only apps from the list
void MainWindow::removeFLUXBOXonly(QStringList &list)
{
    const QStringList list_copy = list;
    for (const QString &file_name : list_copy) {
        QFile file(file_name);
        if (!file.open(QFile::Text | QFile::ReadOnly))
            continue;
        QString text = file.readAll();
        file.close();
        if (text.contains(QLatin1String("OnlyShowIn=FLUXBOX")))
            list.removeOne(file_name);
    }
}

// When running live remove programs meant only for installed environments and the other way round
void MainWindow::removeEnvExclusive(QStringList &list, bool live)
{
    const QString term = live ? "MX-OnlyInstalled" : "MX-OnlyLive";
    const QStringList list_copy = list;
    for (const QString &file_name : list_copy) {
        QFile file(file_name);
        if (!file.open(QFile::Text | QFile::ReadOnly))
            continue;
        QString text = file.readAll();
        file.close();
        if (text.contains(term))
            list.removeOne(file_name);
    }
}
