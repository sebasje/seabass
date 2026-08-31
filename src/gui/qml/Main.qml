import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Layouts
import SeabassGui

ApplicationWindow {
    id: window
    width: 1100
    height: 720
    visible: true
    title: "Seabass"

    // "seabass" palette (see the app icon/watermark's source artifact):
    // Current is the accent, Abyss is the app-bar primary, both fixed
    // brand colors, deliberately not theme-dependent (see Theme.qml).
    Material.theme: appSettingsCtrl.useSystemTheme ? Material.System : Material.Dark
    Material.accent: Theme.accent
    Material.primary: Theme.primary
    color: Theme.background

    MediaController {
        id: mediaCtrl
    }

    PlaybackController {
        id: playbackCtrl
    }

    AppSettingsController {
        id: appSettingsCtrl
    }

    RekordboxGuardController {
        id: rekordboxGuardCtrl
    }

    // Pushes the resolved Material colors + the theme toggle into the
    // Theme singleton. A pure-QML singleton has no place in the visual
    // tree of its own, so it can't read the Material attached properties
    // itself (they're resolved relative to an Item's ancestors); `window`
    // here is the one Item that actually has Material.theme set, so its
    // resolved values are correct and just get copied across as data.
    Binding { target: Theme; property: "useSystemTheme"; value: appSettingsCtrl.useSystemTheme }
    Binding { target: Theme; property: "materialBackground"; value: window.Material.background }
    Binding { target: Theme; property: "materialForeground"; value: window.Material.foreground }
    Binding { target: Theme; property: "materialDivider"; value: window.Material.dividerColor }

    // Explicit, guaranteed background fill. The Material style's own
    // window-background handling doesn't reliably respect a plain
    // `color:` on ApplicationWindow (observed: it kept rendering the
    // Qt default white regardless), so paint it ourselves instead of
    // depending on that interaction.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RekordboxRunningWarning {
            visible: rekordboxGuardCtrl.rekordboxRunning
        }

        StackView {
            id: stackView
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: stickListPageComponent
        }

        PlayerBar {
            Layout.fillWidth: true
            visible: playbackCtrl.hasTrack
            controller: playbackCtrl
        }
    }

    // Large background watermark, normally the "Sound Bass" app mark,
    // anchored to the bottom-right corner, swapped for the playing
    // track's own cover art whenever there is one (falls back to the
    // brand mark the instant playback stops or the current track just
    // has no art, playbackCtrl.artworkPath is already a proper
    // file:// URL, same one PlayerBar.qml's own cover art uses directly).
    // Sits on top of the content (so it's visible regardless of which
    // page's opaque background is underneath) but at low opacity and
    // with no mouse handling of its own, so it never competes with or
    // blocks the real UI.
    // Two identically-positioned layers, alternating which is "front."
    // A plain source swap on a single Image is an instantaneous pixel
    // replacement, fading that single layer out and back in just reads
    // as a dip-to-black between old and new art, not a blend of the two.
    // Crossfading needs the old image to still be on screen, fading out,
    // while the new one fades in on top of it simultaneously; that needs
    // two separate Image/MultiEffect stacks.
    component WatermarkLayer: Item {
        id: layer
        property alias source: img.source
        property bool isArtwork: false
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -width * 0.08
        width: Math.min(window.width, window.height) * 0.75
        height: width
        opacity: 0

        Behavior on opacity {
            NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
        }

        Image {
            id: img
            // Only ever used as MultiEffect's pixel source below, never
            // rendered directly. Qt Quick still grabs a hidden item's
            // texture for an effect source, same as layer.enabled does.
            visible: false
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            smooth: true
            // Without this, the vector brand SVG still gets rasterized
            // once at whatever small default size the source reports
            // (not "crisp at any size" as the comment above assumes),
            // then that raster is upscaled to ~0.75x the window's
            // shorter side -- soft/blurry despite being vector source.
            // Binding sourceSize to the actual on-screen size makes Qt's
            // SVG renderer rasterize at target resolution instead.
            // Harmless for the artwork-cover-art case too: that path
            // already relies on MultiEffect's blur, not sharpness.
            sourceSize: Qt.size(layer.width, layer.height)
        }

        // Cover art is a small source image (a rekordbox/Engine
        // thumbnail, often well under 300px) stretched to ~0.75x the
        // window's shorter side. Upscaled that far, its own pixel grid
        // becomes visible ("scaled up a lot... shows artifacts").
        // Blurred here rather than just relying on Image.smooth's
        // bilinear filtering, which softens edges slightly but doesn't
        // hide a real resolution mismatch at this scale factor. The
        // brand SVG watermark is vector, crisp at any size, so blur
        // only actually applies when this layer is showing real artwork.
        MultiEffect {
            // Fills this layer (its actual parent). Anchoring straight
            // to the Image sibling-of-a-different-item instead is not a
            // legal QML anchor target (only parent/sibling) and was
            // silently resolving to a zero-size effect in an earlier
            // version of this watermark, which is why it disappeared
            // entirely for a while.
            anchors.fill: parent
            source: img
            blurEnabled: layer.isArtwork
            blur: 1.0
            blurMax: 64
        }
    }

    WatermarkLayer { id: watermarkLayerA }
    WatermarkLayer { id: watermarkLayerB }
    property bool watermarkFrontIsA: true

    function updateWatermark() {
        // Read straight off playbackCtrl rather than through an
        // intermediate readonly property bound to it. That property's
        // own binding refreshes off the very same trackChanged signal
        // this function is called from, and QML doesn't guarantee this
        // Connections handler runs after that binding's re-evaluation.
        // When it ran first, this read back the *previous* value, one
        // track behind. A direct property read here always gets the
        // live current value regardless of connection order.
        var isArt = playbackCtrl.hasTrack && playbackCtrl.artworkPath.length > 0;
        var src = isArt ? playbackCtrl.artworkPath
            : "qrc:/qt/qml/SeabassGui/qml/icons/seabass_soundbass.svg";
        var front = watermarkFrontIsA ? watermarkLayerA : watermarkLayerB;
        var back = watermarkFrontIsA ? watermarkLayerB : watermarkLayerA;
        if (front.source === src) {
            return;
        }
        back.source = src;
        back.isArtwork = isArt;
        back.opacity = 0.18;
        front.opacity = 0;
        watermarkFrontIsA = !watermarkFrontIsA;
    }

    // hasTrack/artworkPath both share NOTIFY trackChanged (see
    // playback_controller.hpp). There's no separate hasTrackChanged/
    // artworkPathChanged signal to listen for; an earlier version of
    // this Connections block named those two anyway, which QML just
    // silently never fires, so this never re-ran on an actual track
    // change (the crossfade wasn't skipping frames, it just never
    // started. Whatever visual change was visible came from something
    // else jumping straight to the new state).
    Connections {
        target: playbackCtrl
        function onTrackChanged() { window.updateWatermark(); }
    }

    Component.onCompleted: window.updateWatermark()

    Component {
        id: stickListPageComponent
        StickListPage {
            mediaController: mediaCtrl
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
            onBrowseRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(scanPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onDuplicateTracksHubRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(duplicatesHubPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onLibraryHealthRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(libraryConsistencyPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onStickStatisticsRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(stickStatisticsPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onEngineLibraryCreatorRequested: (stickLabel, rekordboxPath) => stackView.push(engineLibraryCreatorPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
            })
            onSettingsRequested: (stickLabel, pioneerRoot) => stackView.push(settingsPageComponent, {
                stickLabel: stickLabel,
                pioneerRoot: pioneerRoot,
            })
            onSyncRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(syncPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onAppSettingsRequested: stackView.push(appSettingsPageComponent)
            onBackupsHubRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(backupsHubPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onAboutRequested: stackView.push(aboutPageComponent)
        }
    }

    Component {
        id: scanPageComponent
        ScanPage {
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: duplicatesHubPageComponent
        DuplicatesHubPage {
            onDuplicatesStatsRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(duplicatesPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onCleanupRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(cleanupPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onPendingDeletionsRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(pendingDeletionsPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onJunkCueCleanupRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(junkCuePageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
        }
    }

    Component {
        id: junkCuePageComponent
        JunkCuePage {
        }
    }

    Component {
        id: duplicatesPageComponent
        DuplicatesPage {
            playbackController: playbackCtrl
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: cleanupPageComponent
        CleanupPage {
            appSettingsController: appSettingsCtrl
            playbackController: playbackCtrl
        }
    }

    Component {
        id: pendingDeletionsPageComponent
        PendingDeletionsPage {
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: libraryConsistencyPageComponent
        LibraryConsistencyPage {
            playbackController: playbackCtrl
        }
    }

    Component {
        id: stickStatisticsPageComponent
        StickStatisticsPage {
            playbackController: playbackCtrl
            onSyncRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(syncPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
        }
    }

    Component {
        id: engineLibraryCreatorPageComponent
        EngineLibraryCreatorPage {}
    }

    Component {
        id: settingsPageComponent
        SettingsPage {}
    }

    Component {
        id: syncPageComponent
        SyncPage {
            playbackController: playbackCtrl
        }
    }

    Component {
        id: appSettingsPageComponent
        AppSettingsPage {
            appSettingsController: appSettingsCtrl
            onAnonymizeLibraryRequested: stackView.push(anonymizeLibraryPageComponent)
        }
    }

    Component {
        id: anonymizeLibraryPageComponent
        AnonymizeLibraryPage {
            mediaController: mediaCtrl
        }
    }

    Component {
        id: backupsHubPageComponent
        BackupsHubPage {
            onLocalCueRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(localCuePageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
            onManageBackupsRequested: (stickLabel, rekordboxPath, enginePath) => stackView.push(backupsPageComponent, {
                stickLabel: stickLabel,
                rekordboxPath: rekordboxPath,
                enginePath: enginePath,
            })
        }
    }

    Component {
        id: backupsPageComponent
        BackupsPage {}
    }

    Component {
        id: localCuePageComponent
        LocalCuePage {
            appSettingsController: appSettingsCtrl
        }
    }

    Component {
        id: aboutPageComponent
        AboutPage {}
    }
}
