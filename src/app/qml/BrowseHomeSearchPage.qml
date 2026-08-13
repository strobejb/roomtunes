import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// All-sources search opened from BrowseHome.qml. Each source still searches
// through its own MusicService object; this page only fans out the request and
// groups the first few results per service so QML never has to know whether the
// source is SMAPI, the Sonos library, or something else added later.
Item {
    id: root

    property StackView stack
    property Component pageComponent
    property var zone
    property string term: ""
    property var searchServices: []
    readonly property bool isSearchResults: true
    property int finishedCount: 0

    function openServiceSearch(serviceItem) {
        if (!serviceItem.serviceObject)
            return

        root.stack.pushSearchResults(root.pageComponent, {
            title: root.term,
            service: serviceItem.serviceObject,
            searchTerm: root.term,
            stack: root.stack,
            pageComponent: root.pageComponent
        })
    }

    function openResult(serviceItem, item) {
        if (item.container) {
            root.stack.pushFolder(root.pageComponent, {
                title: item.title,
                service: serviceItem.serviceObject,
                objectId: item.browseId || item.id,
                browseItem: item,
                stack: root.stack,
                pageComponent: root.pageComponent,
                folderItem: item
            })
        } else if (root.zone && item.uri) {
            root.zone.playItem(item)
        }
    }

    function isSonosFavourite(item) {
        return item && String(item.id || "").indexOf("FV:2/") === 0
    }

    function favouriteActionText(item) {
        return isSonosFavourite(item) ? qsTr("Remove from Favourites") : qsTr("Favourite")
    }

    function rowMenuItems(item) {
        const favouriteAction = root.favouriteActionText(item)
        if (!item || !item.uri)
            return [favouriteAction]
        return [favouriteAction, "-", qsTr("Play Now"), qsTr("Play Next"),
                qsTr("Add to End of Queue"), qsTr("Replace Queue")]
    }

    ActionMenu {
        id: rowMenu
        property var currentItem: ({})
        items: root.rowMenuItems(currentItem)

        onItemClicked: (text) => {
            if (!root.zone)
                return
            if (text === qsTr("Favourite")) {
                root.zone.addItemToSonosFavourites(rowMenu.currentItem)
                return
            }
            if (text === qsTr("Remove from Favourites")) {
                root.zone.removeItemFromSonosFavourites(rowMenu.currentItem)
                return
            }
            if (!rowMenu.currentItem.uri)
                return
            if (text === qsTr("Play Now"))
                root.zone.playItem(rowMenu.currentItem)
            else if (text === qsTr("Play Next"))
                root.zone.playItemNext(rowMenu.currentItem)
            else if (text === qsTr("Add to End of Queue"))
                root.zone.addItemToQueue(rowMenu.currentItem)
            else if (text === qsTr("Replace Queue"))
                root.zone.replaceQueueWithItem(rowMenu.currentItem)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 8

        Item {
            Layout.fillWidth: true
            implicitHeight: 32

            Item {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    antialiasing: true
                    color: backMouseArea.containsMouse ? "#F0F0F0" : "transparent"
                }

                Image {
                    anchors.centerIn: parent
                    source: "../resources/icons/chevron_left.svg"
                    sourceSize.width: 26
                    sourceSize.height: 26
                }

                MouseArea {
                    id: backMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.stack.goBack()
                }
            }

            Label {
                anchors.left: backButton.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.term
                font.pixelSize: Math.round(16 * UiScale.factor)
                font.weight: Typography.emphasisWeight
                color: "#212121"
                elide: Text.ElideRight
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: flickable
                anchors.fill: parent
                anchors.leftMargin: -5
                anchors.rightMargin: -5
                contentWidth: width
                contentHeight: resultsColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                ColumnLayout {
                    id: resultsColumn
                    width: flickable.width
                    spacing: 14

                    Repeater {
                        id: serviceRepeater
                        model: root.searchServices

                        ColumnLayout {
                            id: section
                            required property var modelData
                            property var service: modelData.serviceObject
                            property string requestToken: "home-search:" + root.term + ":" + modelData.serviceKey + "|" + Math.random()
                            property bool loading: false
                            property bool finished: false
                            property string errorMessage: ""
                            property var results: []

                            Layout.fillWidth: true
                            spacing: 4
                            visible: !!service && service.canSearch && (loading || errorMessage.length > 0 || results.length > 0)

                            Component.onCompleted: {
                                if (!service || !service.canSearch || root.term.trim().length === 0) {
                                    finished = true
                                    root.finishedCount += 1
                                    return
                                }
                                loading = true
                                service.searchPreview(requestToken, root.term, 3)
                            }

                            Connections {
                                target: section.service

                                function onBrowseFinished(token, ok, message, results) {
                                    if (token !== section.requestToken)
                                        return

                                    section.loading = false
                                    section.finished = true
                                    root.finishedCount += 1
                                    if (ok) {
                                        section.results = results
                                        section.errorMessage = ""
                                    } else {
                                        section.results = []
                                        section.errorMessage = ""
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 10
                                Layout.rightMargin: 5
                                spacing: 10

                                Item {
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    Layout.alignment: Qt.AlignVCenter

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: width / 2
                                        antialiasing: true
                                        color: "#BDBDBD"
                                        visible: serviceIcon.status !== RoundedImage.Ready
                                    }

                                    RoundedImage {
                                        id: serviceIcon
                                        anchors.fill: parent
                                        source: section.modelData.imageUrl ? section.modelData.imageUrl : ""
                                        radius: width / 2
                                        visible: status === RoundedImage.Ready
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: serviceIcon.status !== RoundedImage.Ready
                                        text: "♪"
                                        font.pixelSize: 14
                                        color: "#7A7A7A"
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: section.modelData.title
                                    font.pixelSize: 14
                                    font.weight: Typography.emphasisWeight
                                    color: "#212121"
                                    elide: Text.ElideRight
                                }

                                BusySpinner {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    running: section.loading
                                    visible: section.loading
                                }

                                IconButton {
                                    visible: !section.loading && section.results.length > 0
                                    iconSource: "../resources/icons/chevron_right.svg"
                                    iconSize: 16
                                    onClicked: root.openServiceSearch(section.modelData)
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: 48
                                Layout.rightMargin: 12
                                visible: section.errorMessage.length > 0
                                text: section.errorMessage
                                color: "#D32F2F"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }

                            Repeater {
                                model: section.results.slice(0, 3)

                                MusicServiceRow {
                                    id: rowItem
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: implicitHeight
                                    title: modelData.title
                                    imageUrl: modelData.imageUrl
                                    showChevron: !!modelData.container
                                    showMenu: true
                                    showPlayOverlay: !modelData.container && !!modelData.uri
                                    menuOpen: rowMenu.visible && rowMenu.parent === rowItem
                                    circularIcon: false

                                    onClicked: root.openResult(section.modelData, modelData)

                                    onMenuRequested: {
                                        rowMenu.currentItem = modelData
                                        rowMenu.parent = rowItem
                                        rowMenu.x = rowItem.width - rowMenu.width - 8
                                        rowMenu.y = rowItem.height
                                        rowMenu.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: root.finishedCount < root.searchServices.length
                         && serviceRepeater.count === 0

                BusySpinner {
                    Layout.alignment: Qt.AlignHCenter
                    running: parent.visible
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Searching...")
                    color: "#9E9E9E"
                    font.pixelSize: 13
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.searchServices.length > 0
                         && root.finishedCount >= root.searchServices.length
                         && resultsColumn.visibleChildren.length === 0
                text: qsTr("No results")
                color: "#9E9E9E"
                font.pixelSize: 13
            }
        }
    }
}
