#include <QMainWindow>
#include <QToolBar>
#include <QDebug>
#include <QAction>
#include <QStatusBar>
#include <QMenuBar>
#include <QTextEdit>
#include <QDateTime>
#include <QIcon>
#include <QDesktopServices>
#include <QInputDialog>
#include <QList>

#include "pluginattachview.h"

#include "robodk_interface.h"
#include "iitem.h"


static Mat camabs_2_vp(Mat camabs){
    return Mat(camabs * rotx(M_PI)).inv();
}

static Mat vp_2_camabs(Mat vp){
    return camabs_2_vp(vp).inv();
}

// Get the list of parents of an Item up to the Station, with type filtering (i.e. [ITEM_TYPE_FRAME, ITEM_TYPE_ROBOT, ..]).
static QList<Item> getAncestors(Item item, QList<int> filters = {}){
    Item parent = item;
    QList<Item> parents;
    while (parent != nullptr && parent->Type() != IItem::ITEM_TYPE_STATION && parent->Type() != IItem::ITEM_TYPE_ANY) {
        parent = parent->Parent();

        if (filters.empty()){
            parents.push_back(parent);
            continue;
        }

        for (const auto &filter : filters) {
            if (parent->Type() == filter){
                parents.push_back(parent);
                break;
            }
        }
    }
    return parents;
}


// Finds the lowest common ancestor (LCA) between two Items in the Station's tree.
static Item getLowestCommonAncestor(Item item1, Item item2){

    // Make an ordered list of parents (backwards). Iter on it until the parent differs.. and you get the lowest common ancestor (LCA)
    QList<Item> parents1 = getAncestors(item1);
    QList<Item> parents2 = getAncestors(item2);

    Item lca = nullptr;
    int size = std::min(parents1.size(), parents2.size());
    for (int i = 0; i < size; ++i){
        if (parents1.back() != parents2.back()){
            break;
        }

        lca = parents1.back();
        parents1.pop_back();
        parents2.pop_back();
    }

    if (lca == nullptr){
        qDebug() << item1->Name() << " does not share an ancestor with " << item2->Name();
    }

    return lca;
}


// Gets the pose between two Items that have a hierarchical relationship in the Station's tree.
static Mat getAncestorPose(Item item_child, Item item_parent){
    if (item_child == item_parent){
        return Mat();
    }

    QList<Item> parents = getAncestors(item_child);
    if (!parents.contains(item_parent)){
        qDebug() << item_child->Name() << " is not a child of " << item_parent->Name();
        return Mat(false);
    }

    QList<Item> items;
    items.append(item_child);
    items.append(parents);
    int idx = items.indexOf(item_parent);
    Mat pose = Mat();
    for (int i = idx - 1; i >= 0; --i){
        if (items[i]->Type() == IItem::ITEM_TYPE_TOOL){
            pose *= items[i]->PoseTool();
        }
        else if (items[i]->Type() == IItem::ITEM_TYPE_ROBOT){
            pose *= items[i]->SolveFK(items[i]->Joints());
        }
        else{
            pose *= items[i]->Pose();
        }
    }

    return pose;
}


// Gets the pose of an Item (item1) with respect to an another Item (item2).
static Mat getPoseWrt(Item item1, Item item2){
    if (item1 == item2){
        return Mat();
    }

    QList<Item> parents1 = getAncestors(item1);
    if (parents1.contains(item2)){
        return getAncestorPose(item1, item2);
    }

    QList<Item> parents2 = getAncestors(item2);
    if (parents2.contains(item1)){
        return getAncestorPose(item2, item1).inv();
    }

    Item lca = getLowestCommonAncestor(item1, item2);
    Mat pose1 = getAncestorPose(item1, lca);
    Mat pose2 = getAncestorPose(item2, lca);
    return pose2.inv() * pose1;
}


// Set the pose of the item with respect to the absolute reference frame, accounting for inverse kinematics.
static void setPoseAbsIK(Item item, Mat pose_abs, Item station){
    if (item->Type() == IItem::ITEM_TYPE_STATION){
        return;
    }

    QList<Item> parents = getAncestors(item);
    if (parents.size() == 1){
        item->setPose(pose_abs);
        return;
    }

    if (item->Type() == IItem::ITEM_TYPE_TOOL){
        pose_abs = pose_abs * item->PoseTool().inv() * item->Parent()->PoseTool();
        item = item->Parent();
        parents.pop_front();
    }

    item->setPose(getAncestorPose(parents[0], station).inv() * pose_abs);
}

//------------------------------- RoboDK Plug-in commands ------------------------------

PluginAttachView::PluginAttachView(){
}


QString PluginAttachView::PluginName(){
    return "Plugin View Camera";
}


QString PluginAttachView::PluginLoad(QMainWindow *mw, QMenuBar *menubar, QStatusBar *statusbar, RoboDK *rdk, const QString &settings){
    RDK = rdk;
    MainWindow = mw;
    StatusBar = statusbar;

    qDebug() << "Loading plugin " << PluginName();
    qDebug() << "Using settings: " << settings; // reserved for future compatibility

    // it is highly recommended to use the statusbar for debugging purposes (pass /DEBUG as an argument to see debug result in RoboDK)
    qDebug() << "Setting up the status bar";
    StatusBar->showMessage(tr("RoboDK Plugin %1 is being loaded").arg(PluginName()));

    // Here you can add all the "Actions": these actions are callbacks from buttons selected from the menu or the toolbar
    action_slave_view_to_anchor = new QAction(tr("Slave the View to this Item"));
    action_slave_view_to_anchor->setCheckable(true);

    action_slave_anchor_to_view = new QAction(tr("Slave the Item to the View"));
    action_slave_anchor_to_view->setCheckable(true);

    // Make sure to connect the action to your callback (slot)
    connect(action_slave_view_to_anchor, SIGNAL(triggered(bool)), this, SLOT(callback_activate_slave_view_to_anchor(bool)));
    connect(action_slave_anchor_to_view, SIGNAL(triggered(bool)), this, SLOT(callback_activate_slave_anchor_to_view(bool)));

    // return string is reserverd for future compatibility
    return "";
}


void PluginAttachView::PluginUnload(){
    // Cleanup the plugin
    qDebug() << "Unloading plugin " << PluginName();

    view_anchor.clear();
    last_clicked_item = nullptr;

    if (nullptr != action_slave_view_to_anchor)
    {
        action_slave_view_to_anchor->deleteLater();
        action_slave_view_to_anchor = nullptr;
    }
    if (nullptr != action_slave_anchor_to_view)
    {
        action_slave_anchor_to_view->deleteLater();
        action_slave_anchor_to_view = nullptr;
    }
}


void PluginAttachView::PluginLoadToolbar(QMainWindow *mw, int icon_size){
}


bool PluginAttachView::PluginItemClick(Item item, QMenu *menu, TypeClick click_type){
    qDebug() << "Selected item: " << item->Name() << " of type " << item->Type() << " click type: " << click_type;

    last_clicked_item = nullptr;

    if (click_type != ClickRight){
        return false;
    }

    if (processItem(item)){
        last_clicked_item = item;

        bool active = (view_anchor.anchor == item);
        bool is_master = (view_anchor.is_master);

        // Create the menu option, or update if it already exist
        menu->addSeparator();
        action_slave_view_to_anchor->blockSignals(true);
        action_slave_view_to_anchor->setChecked(active && !is_master);
        action_slave_view_to_anchor->blockSignals(false);
        menu->addAction(action_slave_view_to_anchor);

        action_slave_anchor_to_view->blockSignals(true);
        action_slave_anchor_to_view->setChecked(active && is_master);
        action_slave_anchor_to_view->blockSignals(false);
        menu->addAction(action_slave_anchor_to_view);

        return true;
    }

    return false;
}


QString PluginAttachView::PluginCommand(const QString &command, const QString &value){
    qDebug() << "Sent command: " << command << "    With value: " << value;
    return "";
}


void PluginAttachView::PluginEvent(TypeEvent event_type){
    switch (event_type) {
    case EventChangedStation:
    case EventChanged:
    {
        cleanupRemovedItems();
        updatePose();
        break;
    }
    case EventMoved:
        updateViewPose(); // Update the View when something has moved
        break;
    case EventRender:
        updateAnchorPose(); // Update the anchor all the time as the camera can move any time
        break;
    default:
        break;

    }
}


//----------------------------------------------------------------------------------
// Define your own button callbacks here (and other slots)

void PluginAttachView::callback_activate_slave_view_to_anchor(bool activate){
    if (last_clicked_item == nullptr){
        return;
    }

    view_anchor.clear();
    if (!activate){   
        return;
    }

    view_anchor.anchor = last_clicked_item;
    view_anchor.is_master = false;
    view_anchor.station = RDK->getActiveStation();
}

void PluginAttachView::callback_activate_slave_anchor_to_view(bool activate){
    if (last_clicked_item == nullptr){
        return;
    }

    view_anchor.clear();
    if (!activate){
        return;
    }

    view_anchor.anchor = last_clicked_item;
    view_anchor.is_master = true;
    view_anchor.station = RDK->getActiveStation();
}


//----------------------------------------------------------------------------------
bool PluginAttachView::processItem(Item item){
    if (item == nullptr){
        return false;
    }

    if (QList<int>({IItem::ITEM_TYPE_ROBOT, IItem::ITEM_TYPE_FRAME, IItem::ITEM_TYPE_TOOL}).contains(item->Type())){
        qDebug() << "Found valid anchor: " << item->Name();
        return true;
    }
    return false;
}


void PluginAttachView::updateViewPose(){
    if (view_anchor.anchor == nullptr){
        return;
    }

    if (view_anchor.station != RDK->getActiveStation()){
        return;
    }

    if (!view_anchor.is_master){
        // Set the view using the anchor
        Mat pose_abs = getPoseWrt(view_anchor.anchor, view_anchor.station);
        Mat view_pose = camabs_2_vp(pose_abs);
        RDK->setViewPose(view_pose);
    }

    RDK->Render(RoboDK::RenderUpdateOnly);
}


void PluginAttachView::updateAnchorPose(){
    if (view_anchor.anchor == nullptr){
        return;
    }

    if (view_anchor.station != RDK->getActiveStation()){
        return;
    }

    if (view_anchor.is_master){
        // Set the anchor using the View
        Mat view_pose = vp_2_camabs(RDK->ViewPose().inv());
        setPoseAbsIK(view_anchor.anchor, view_pose, view_anchor.station);
    }

    RDK->Render(RoboDK::RenderUpdateOnly);
}


void PluginAttachView::updatePose(){
    updateAnchorPose();
    updateViewPose();
}


void PluginAttachView::cleanupRemovedItems() {
    if (view_anchor.anchor == nullptr){
        return;
    }

    Item station = RDK->getActiveStation();
    QList<Item> stations = RDK->getOpenStations();

    // Remove deleted stations
    if (!stations.contains(view_anchor.station)) {
        qDebug() << "Station closed. Removing affected items.";
        view_anchor.clear();
        return;
    }

    // Remove deleted items from the current station
    // Note: RDK->Valid(item) return false for items in other stations
    if (view_anchor.station == station) {
        if (!RDK->Valid(view_anchor.anchor)) {
            qDebug() << "Item deleted. Removing affected items.";
            view_anchor.clear();
            return;
        }
    }
}
