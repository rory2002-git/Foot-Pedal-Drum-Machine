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
#include <QLabel>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QHeaderView>
#include <QDebug>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QUndoView>
#include <QDrag>
#include <QTreeWidget>
#include <QAbstractProxyModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>
#include <iostream>

#include "projectexplorerpanel.h"
#include "../workspace/workspace.h"
#include "../workspace/contentlibrary.h"
#include "../workspace/libcontent.h"
#include "drmlistmodel.h"
#include "../model/tree/project/beatsprojectmodel.h"
#include "../model/tree/abstracttreeitem.h"
#include "../model/filegraph/song.h"  // to get MAX_BPM
#include "../workspace/settings.h"
#include "../model/filegraph/midiParser.h" // for MAIN_DRUM_LOOP, etc.

// Define missing constants
enum ItemType {
    DRUMSET_FILE_ITEM = 100,
    SONG_PART_ITEM = 101
};

class QTreeViewDragFixed : public QTreeView
{
protected:
    void startDrag(Qt::DropActions supportedActions)
    {
        QModelIndexList indexes = selectedIndexes();
        QList<QPersistentModelIndex> persistentIndexes;

        if (indexes.count() > 0) {
            QMimeData *data = model()->mimeData(indexes);
            if (!data)
                return;
            for (int i = 0; i<indexes.count(); i++){
                QModelIndex idx = indexes.at(i);
                qDebug() << "\tDragged item to delete" << i << " is: \"" << idx.data().toString() << "\"";
                qDebug() << "Row is: " << idx.row();
                persistentIndexes.append(QPersistentModelIndex(idx));
            }

            QPixmap pixmap = indexes.first().data(Qt::DecorationRole).value<QPixmap>();
            QDrag *drag = new QDrag(this);
            drag->setPixmap(pixmap);
            drag->setMimeData(data);
            drag->setHotSpot(QPoint(pixmap.width()/2, pixmap.height()/2));

            Qt::DropAction defaultDropAction = Qt::IgnoreAction;
            if (supportedActions & Qt::MoveAction && dragDropMode() != QAbstractItemView::InternalMove)
                defaultDropAction = Qt::MoveAction; 

            if ( drag->exec(supportedActions, defaultDropAction) & Qt::MoveAction ){
                //when we get here any copying done in dropMimeData has messed up our selected indexes
                //that's why we use persistent indexes
                for (int i = 0; i<indexes.count(); i++){
                    QPersistentModelIndex idx = persistentIndexes.at(i);
                    qDebug() << "\tDragged item to delete" << i << " is: " << idx.data().toString();
                    qDebug() << "Row is: " << idx.row();
                    if (idx.isValid()){ //the item is not top level
                        model()->removeRow(idx.row(), idx.parent());
                    }
                    else{
                        model()->removeRow(idx.row(), QModelIndex());
                    }
                }
            }
        }
    }

public:
    explicit QTreeViewDragFixed(QWidget* parent = nullptr) : QTreeView(parent) {}
};

void NoInputSpinBox::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
        return QSpinBox::keyPressEvent(event);
    event->ignore();
}


ProjectExplorerPanel::ProjectExplorerPanel(QWidget *parent)
    : QWidget(parent)
    , m_clean(true)
#ifndef DEBUG_TAB_ENABLED
    , mp_beatsModel(nullptr)
#endif
{
   createLayout();
}

ProjectExplorerPanel::~ProjectExplorerPanel() {

}

void showInGraphicalShell(const QString &path)
{
    // Mac, Windows support folder or file.
#if defined(Q_OS_WIN)
    QProcess::startDetached(QString("explorer /select, \"%1\"").arg(QDir::toNativeSeparators(path)));
#elif defined(Q_OS_MAC)
    QProcess::execute("/usr/bin/osascript", QStringList() << QString::fromLatin1("-e") << QString::fromLatin1("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path));
    QProcess::execute("/usr/bin/osascript", QStringList() << QString::fromLatin1("-e") << QString::fromLatin1("tell application \"Finder\" to activate"));
#else
    // Silence unused parameter warning
    Q_UNUSED(path);
#endif
}

void ProjectExplorerPanel::createLayout()
{
   QVBoxLayout * p_VBoxLayout = new QVBoxLayout();
   setLayout(p_VBoxLayout);
   p_VBoxLayout->setContentsMargins(0,0,0,0);

    auto title = new QLabel(tr("Project Explorer%1%2").arg("").arg(m_clean ? "" : "*"), this);
    mp_Title = title;
    mp_Title->setObjectName(QStringLiteral("titleBar"));

   p_VBoxLayout->addWidget(mp_Title, 0);

   mp_MainContainer = new QStackedWidget(this);
   p_VBoxLayout->addWidget(mp_MainContainer, 1);
   mp_MainContainer->setLayout(new QVBoxLayout());

   auto p_tabWidget = new QTabWidget(mp_MainContainer);
   mp_MainContainer->layout()->addWidget(p_tabWidget);

   mp_beatsTreeView = new QTreeView(this);
   mp_beatsTreeView->setAnimated(true);
   mp_beatsTreeView->setExpandsOnDoubleClick(false);
   mp_beatsTreeView->setSelectionMode(QAbstractItemView::SingleSelection);
   mp_beatsTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
   mp_beatsTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
   mp_beatsTreeView->setDragEnabled(true);
   mp_beatsTreeView->setAcceptDrops(true);
   p_tabWidget->addTab(mp_beatsTreeView, tr("Songs"));
   connect(mp_beatsTreeView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(slotOnDoubleClick(QModelIndex)));

   mp_drmListView = new QListView();
   mp_drmListView->setEditTriggers(QAbstractItemView::SelectedClicked);

   p_tabWidget->addTab(mp_drmListView, tr("Drum Sets"));
   //TODO: MS CONNECT BUG
   connect(mp_drmListView->selectionModel(), SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(slotCurrentItemChanged(QListWidgetItem*,QListWidgetItem*)));
   connect(mp_drmListView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(slotOnDoubleClick(QModelIndex)));

#ifdef DEBUG_TAB_ENABLED
   mp_beatsDebugTreeView = new QTreeView(this);
   p_tabWidget->addTab(mp_beatsDebugTreeView, tr("Debug"));
   mp_beatsDebugTreeView->setAlternatingRowColors(true);
#endif
   mp_viewUndoRedo = new QUndoView(this);
   p_tabWidget->addTab(mp_viewUndoRedo, tr("Undo/Redo"));

   connect(p_tabWidget, SIGNAL(currentChanged(int)), this, SLOT(slotOnCurrentTabChanged(int)));

   // Delete Key will delete drumset if allowed
   connect(new QShortcut(QKeySequence(Qt::Key_Delete), mp_drmListView), SIGNAL(activated()), this, SLOT(slotDrmDeleteRequestShortcut()));
}

bool ProjectExplorerPanel::doesUserConsentToImportDrumset()
{
    return QMessageBox::Yes == QMessageBox::question(this, tr("Edit Drumset"), tr("Project drum sets must be copied to workspace.\n\nContinue?"));
}




// Required to apply stylesheet
void ProjectExplorerPanel::paintEvent(QPaintEvent *)
{
   QStyleOption opt;
   opt.init(this);
   QPainter p(this);
   style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ProjectExplorerPanel::slotOnCurrentTabChanged(int index)
{
   if(index <= 1){
      emit sigCurrentTabChanged(index);
   }
}

// Used for debug only
void ProjectExplorerPanel::slotDebugOnCurrentChanged(const QModelIndex & current, const QModelIndex & previous)
{
   qDebug() << "ProjectExplorerPanel::slotDebugOnCurrentChanged from " << previous.data().toString() << " to " << current.data().toString();
}

// Used for debug only
void ProjectExplorerPanel::slotDebugOnSelectionChanged(const QItemSelection & selected, const QItemSelection & deselected)
{
   qDebug() << "ProjectExplorerPanel::slotDebugOnSelectionChanged deselected: ";
   foreach (const QItemSelectionRange & range, deselected) {
      qDebug() << "   topLeft = " << range.topLeft().data().toString();
      qDebug() << "   bottomRight = " << range.bottomRight().data().toString();
   }
   qDebug() << "ProjectExplorerPanel::slotDebugOnSelectionChanged selected: ";
   foreach (const QItemSelectionRange & range, selected) {
      qDebug() << "   topLeft = " << range.topLeft().data().toString();
      qDebug() << "   bottomRight = " << range.bottomRight().data().toString();
   }
}

// Comment out slotBeginEdit method for command-line version
void ProjectExplorerPanel::slotBeginEdit(class SongPart*)
{
    // This is a simplified empty implementation for the command-line version
    // The original UI-based code is not needed
}

void ProjectExplorerPanel::slotCleanChanged(bool clean)
{
    
    m_clean = clean;
    QFileInfo project;
#ifdef DEBUG_TAB_ENABLED
    if (mp_beatsDebugTreeView->model()) project = ((BeatsProjectModel*)mp_beatsDebugTreeView->model())->projectFileFI();
#else
    if (mp_beatsModel) project = mp_beatsModel->projectFileFI();
#endif
    mp_Title->setText(tr("Project Explorer%1%2").arg(project.exists() ? " - " + project.fileName() : "").arg(m_clean ? "" : "*"));
}

void ProjectExplorerPanel::slotCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    emit sigCurrentItemChanged(current, previous);
}

void ProjectExplorerPanel::setProxyBeatsModel(QAbstractProxyModel *p_model)
{
    mp_beatsTreeView->setModel(p_model);

    // Without the dialog in the CLI version, just connect the headers
    if (p_model) {
        mp_beatsTreeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    }
}

void ProjectExplorerPanel::setMidiColumn() {
    mp_beatsTreeView->setColumnHidden(AbstractTreeItem::LOOP_COUNT,!Settings::midiIdEnabled());
    if (Settings::midiIdEnabled()) {
        mp_beatsTreeView->header()->resizeSection(0,200);
        mp_beatsTreeView->header()->resizeSection(1,40);
    }
}

void ProjectExplorerPanel::setProxyBeatsRootIndex(QModelIndex rootIndex)
{
   mp_beatsTreeView->setRootIndex(rootIndex);
}

void ProjectExplorerPanel::setProxyBeatsSelectionModel(QItemSelectionModel *selectionModel)
{
   mp_beatsTreeView->setSelectionModel(selectionModel);
}

QItemSelectionModel * ProjectExplorerPanel::beatsSelectionModel()
{
   return mp_beatsTreeView->selectionModel();
}


void ProjectExplorerPanel::setDrmListModel(QAbstractItemModel * p_model)
{
   mp_drmListView->setModel(p_model);
}

QItemSelectionModel * ProjectExplorerPanel::drmListSelectionModel()
{
   return mp_drmListView->selectionModel();
}


void ProjectExplorerPanel::setBeatsModel(BeatsProjectModel *p_model, QUndoGroup* undo)
{
#ifdef DEBUG_TAB_ENABLED
   mp_beatsDebugTreeView->setModel(p_model);
#else
    mp_beatsModel = p_model;
#endif
    mp_viewUndoRedo->setGroup(undo);

   slotCleanChanged(m_clean);
}

void ProjectExplorerPanel::setBeatsDebugSelectionModel(QItemSelectionModel *selectionModel)
{
#ifdef DEBUG_TAB_ENABLED
   mp_beatsDebugTreeView->setSelectionModel(selectionModel);
#elif _MSC_VER
    selectionModel; // warning C4100: 'selectionModel' : unreferenced formal parameter
#endif
}

DrmListModel *ProjectExplorerPanel::drmListModel()
{
   return static_cast<DrmListModel *>(mp_drmListView->model());
}

// Simplify the slotOnDoubleClick method to skip dialog for command-line version
void ProjectExplorerPanel::slotOnDoubleClick(const QModelIndex &index)
{
    if (index.isValid()) {
        // Get the type of the item
        const int type = index.data(Qt::UserRole).toInt();
        const int type2 = index.data(Qt::UserRole+1).toInt();
        
        // For drumsets, we can set the path directly
        if (DRUMSET_FILE_ITEM == type || DRUMSET_FILE_ITEM == type2) {
            const QString path = index.data(Qt::UserRole+2).toString();
            qDebug() << "ProjectExplorerPanel::slotOnDoubleClick drumset" << path;
            if (!path.isEmpty()) {
                emit sigOpenDrm(index.data().toString(), path);
            }
        } 
        // For songs, we can modify the name and ID directly in command line version
        else if (SONG_PART_ITEM == type || SONG_PART_ITEM == type2) {
            const QString name = index.data().toString();
            const int midiId = index.sibling(index.row(), AbstractTreeItem::Column::LOOP_COUNT).data().toInt();

            qDebug() << "ProjectExplorerPanel::slotOnDoubleClick" << type2 << name << midiId;
            
            // In command-line version, we don't need to update the model
            // Just output to console
            std::cout << "Editing song part: " << name.toStdString() << " (ID: " << midiId << ")" << std::endl;
            m_clean = false;
        } else {
            qWarning() << "ProjectExplorerPanel::slotOnDoubleClick - ERROR 7 - unknown type for " << index.data().toString() << "(" << type << ") or (" << type2 << ")";
        }
    }
}


void ProjectExplorerPanel::slotDrumListContextMenu(QPoint pnt)
{

    // index of the selected item for context menu
    QModelIndex index = mp_drmListView->indexAt(pnt);

    QMenu contextMenu(this);

    // drumsetListview model contaning the drumsets
    // verify that the position of the right click is on a drumset
    if (!index.isValid()) return;

    // If the selected drumset is opened
    bool isDrumSetOpen = drmListModel()->item(index.row(), DrmListModel::OPENED)->data(Qt::DisplayRole).toBool();
    mp_ContextCloseDrumsetAction->setEnabled(isDrumSetOpen);

    // Get the Path of the drumset and save it for when the action will be triggered
    m_currentContextSelectionPath =  drmListModel()->item(index.row(), DrmListModel::ABSOLUTE_PATH)->data(Qt::DisplayRole).toString();

    mp_ContextDeleteDrumsetAction->setEnabled(true);

    QList<QAction *> actionList;
    actionList.append(mp_ContextDeleteDrumsetAction);
    actionList.append(mp_ContextCloseDrumsetAction);
    QMenu::exec(actionList,mp_drmListView->mapToGlobal(pnt));
}



/**
 * @brief ProjectExplorerPanel::slotDrmDeleteRequestShortcut
 *
 * Makes sure the mp_drmListView has the focus before qctually deleting
 * This is done in order for the delete key being pressed somewhere else not to trigger a deletion
 */
void ProjectExplorerPanel::slotDrmDeleteRequestShortcut()
{
   // Make sure mp_drmListView has focus when delete is being performed through shortcut
   if(!mp_drmListView->hasFocus()){
      return;
   }
   slotDrmDeleteRequest();

}

/**
 * @brief ProjectExplorerPanel::slotDrmDeleteRequest
 *
 * Slot that validates that indexes are valid, then calls for the
 * deletion on the current drumset
 */
void ProjectExplorerPanel::slotDrmDeleteRequest()
{

   // if no project
   if(!drmListModel() || !drmListSelectionModel()){
      qWarning() << "ProjectExplorerPanel::slotDrmDeleteRequest - ERROR 1 - drmListModel or drmListSelectionModel don't exist";
      return;
   }

   // Make sure there is a project selected
   QModelIndex currentIndex = mp_drmListView->selectionModel()->currentIndex();
   QItemSelection selection = mp_drmListView->selectionModel()->selection();
   if(selection.isEmpty() || !currentIndex.isValid()){
      return;
   }

   drmListModel()->deleteDrmModal(currentIndex);
}

void ProjectExplorerPanel::slotTempoChanged(int i) {
    qDebug() << "slot tempo changed:" << i;

    slotCleanChanged(false);

}



