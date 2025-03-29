/*
  	This software and the content provided for use with it is Copyright © 2014-2020 Singular Sound 
 	BeatBuddy Manager is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published by
    the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <QTimer>
#include <QDebug>
#include <QUuid>

#include "bbmanagerapplication.h"
#include "debug.h"
#include "versioninfo.h"
#include "workspace/settings.h"
#include "workspace/libcontent.h"
#include "workspace/contentlibrary.h"

BBManagerApplication::BBManagerApplication(int &argc, char **argv)
    : QApplication(argc, argv)
    , m_projectBeingOpened(false)
{
    VersionInfo ravi = VersionInfo::RunningAppVersionInfo();
    setOrganizationName(ravi.companyName());
    setOrganizationDomain(ravi.companyDomain());
    setApplicationName(ravi.productName());
    setApplicationVersion(ravi.toQString());
    setupDebugging();
    qDebug() << "\t|---> Beatbuddy Manager Lean (Command-line Version) is Starting Up <---|";

    // Make sure the software is uniquely identified for current user
    if(!Settings::softwareUuidExists()){
        Settings::setSoftwareUuid(QUuid::createUuid());
    }
    
    // Command-line version doesn't use MainWindow
    if (arguments().size() > 1) {
        // Could process command line arguments here
        qDebug() << "Command line argument:" << arguments().at(1);
    }
}

BBManagerApplication::~BBManagerApplication()
{
    // No MainWindow to clean up
}

bool BBManagerApplication::event(QEvent *ev)
{
    // Simple event handling for command-line version
    if (ev->type() == QEvent::FileOpen) {
        qDebug() << "File open event received (not handled in command-line version)";
        return true;
    }
    return QApplication::event(ev);
}

void BBManagerApplication::slotOpenDefaultProject()
{
    // In command-line version, we don't automatically open projects
    // Could be implemented if needed
    if (Settings::lastProjectExists()) {
        qDebug() << "Could open last project:" << Settings::getLastProject();
    }
}

