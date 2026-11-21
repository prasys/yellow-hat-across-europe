// LauncherApp.swift - the window: choose the disc, set how the game looks and
// starts, see the saved games, press Play.
//
// All drawing here is the launcher's own (a yellow hat made of two shapes);
// nothing is taken from the game.

import SwiftUI
import AppKit
import UniformTypeIdentifiers

let projectTitle = "The Little Girl in the Yellow Hat Across Europe"

@main
struct LauncherApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var delegate
    @StateObject private var model = LauncherModel()

    var body: some Scene {
        WindowGroup(projectTitle) {
            LauncherView()
                .environmentObject(model)
                .frame(width: 560)
                .fixedSize(horizontal: true, vertical: true)
        }
        .windowResizability(.contentSize)
        .commands { CommandGroup(replacing: .newItem) {} }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        // --snapshot FILE: write a picture of the window and quit (for checking the layout).
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--snapshot"), i + 1 < args.count {
            let file = args[i + 1]
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                if let view = NSApp.windows.first?.contentView,
                   let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                    view.cacheDisplay(in: view.bounds, to: rep)
                    try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: file))
                }
                NSApp.terminate(nil)
            }
        }
    }
}

// MARK: colours

extension Color {
    static let hatYellow = Color(red: 0.98, green: 0.80, blue: 0.16)
    static let ink = Color(red: 0.10, green: 0.12, blue: 0.22)
    static let coatBlue = Color(red: 0.16, green: 0.30, blue: 0.62)
}

// MARK: the hat

/// A straw boater seen from the side: brim, crown, and a dark ribbon with two tails.
struct HatMark: View {
    var body: some View {
        Canvas { ctx, size in
            let w = size.width, h = size.height
            var tails = Path()
            tails.move(to: CGPoint(x: w * 0.70, y: h * 0.50))
            tails.addLine(to: CGPoint(x: w * 0.93, y: h * 0.80))
            tails.move(to: CGPoint(x: w * 0.66, y: h * 0.52))
            tails.addLine(to: CGPoint(x: w * 0.80, y: h * 0.90))
            ctx.stroke(tails, with: .color(.ink), style: StrokeStyle(lineWidth: w * 0.055, lineCap: .round))

            let brim = Path(ellipseIn: CGRect(x: w * 0.04, y: h * 0.46, width: w * 0.92, height: h * 0.24))
            ctx.fill(brim, with: .color(.hatYellow))
            ctx.stroke(brim, with: .color(.ink), lineWidth: w * 0.03)

            let crown = Path(roundedRect: CGRect(x: w * 0.27, y: h * 0.20, width: w * 0.46, height: h * 0.38),
                             cornerRadius: w * 0.10)
            ctx.fill(crown, with: .color(.hatYellow))
            ctx.stroke(crown, with: .color(.ink), lineWidth: w * 0.03)

            let band = Path(CGRect(x: w * 0.27, y: h * 0.44, width: w * 0.46, height: h * 0.09))
            ctx.fill(band, with: .color(.ink))
        }
        .accessibilityHidden(true)
    }
}

// MARK: the window

struct LauncherView: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        VStack(spacing: 0) {
            header
            Form {
                discSection
                Section("Starting up") {
                    Toggle("Skip the opening movies", isOn: $model.skipIntro)
                    Toggle("Fill in my name on the sign-in book", isOn: $model.rememberName)
                    Toggle("Start in full screen", isOn: $model.fullscreen)
                    Toggle("Play with a game controller", isOn: $model.gamepad)
                    Toggle("Use the game's own mouse pointers", isOn: $model.gamePointers)
                }
                Section {
                    Toggle("Smooth the painting", isOn: $model.smooth)
                    Toggle("Redraw edges at twice the size (xBR)", isOn: $model.xbr)
                    Picker("Scaling", selection: $model.scaling) {
                        ForEach(Scaling.allCases) { Text($0.label).tag($0) }
                    }
                    Picker("Beside the picture", selection: $model.blurBars) {
                        Text("Soft blur").tag(true)
                        Text("Black").tag(false)
                    }
                } header: {
                    Text("Picture")
                } footer: {
                    Text("Smoothing melts the 256-colour speckle into washes of paint; xBR takes the stair-steps out of slanted lines.")
                        .multilineTextAlignment(.leading)
                .frame(maxWidth: .infinity, alignment: .leading)
                }
                savesSection
            }
            .formStyle(.grouped)
            .scrollDisabled(true)
            footer
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var header: some View {
        HStack(alignment: .center, spacing: 16) {
            HatMark()
                .frame(width: 84, height: 70)
            VStack(alignment: .leading, spacing: 4) {
                Text("The Little Girl in the Yellow Hat")
                    .font(.system(size: 23, weight: .bold, design: .serif))
                    .foregroundStyle(Color.ink)
                Text("Across Europe")
                    .font(.system(size: 17, weight: .semibold, design: .serif))
                    .italic()
                    .foregroundStyle(Color.coatBlue)
                Text("A 1996 adventure, running natively. Bring your own disc.")
                    .font(.callout)
                    .foregroundStyle(Color.ink.opacity(0.7))
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 18)
        .frame(maxWidth: .infinity)
        // The same sunny paper in light and dark mode, so the ink on it always reads.
        .background(
            LinearGradient(colors: [Color(red: 1.0, green: 0.90, blue: 0.52), Color(red: 1.0, green: 0.96, blue: 0.80)],
                           startPoint: .top, endPoint: .bottom)
        )
        .overlay(alignment: .bottom) { Divider() }
    }

    private var discSection: some View {
        Section("Game disc") {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Image(systemName: model.discIsValid ? "checkmark.circle.fill" : "opticaldisc")
                    .foregroundStyle(model.discIsValid ? Color.green : Color.secondary)
                VStack(alignment: .leading, spacing: 2) {
                    Text(model.discIsValid ? "Disc found" : "No disc chosen yet")
                    Text(model.discPath.isEmpty
                         ? "Choose your own disc image (.iso) or a folder with the disc's files."
                         : (model.discPath as NSString).abbreviatingWithTildeInPath)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(2)
                        .truncationMode(.middle)
                }
                Spacer()
                if model.busy { ProgressView().controlSize(.small) }
                Menu("Choose…") {
                    Button("Disc image (.iso)…") { chooseImage() }
                    Button("Folder with the disc's files…") { chooseFolder() }
                }
                .fixedSize()
                .disabled(model.busy || model.building || model.playing)
            }
            if model.discIsValid { engineRow }
        }
    }

    /// The engine is the game's own code, recompiled from the disc's MADEURO.EXE.
    private var engineRow: some View {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
            if model.building {
                ProgressView().controlSize(.small)
            } else {
                Image(systemName: model.engineMatches == true ? "checkmark.circle.fill"
                                  : model.engineMatches == false ? "hammer.circle.fill" : "circle.dotted")
                    .foregroundStyle(model.engineMatches == true ? Color.green
                                     : model.engineMatches == false ? Color.orange : Color.secondary)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(model.building ? "Recompiling the game from your disc…"
                     : model.engineMatches == true ? "Engine built for this disc"
                     : model.engineMatches == false ? "This disc needs its engine built" : "Checking the engine…")
                Text("The game's program is translated to native code from your disc, on this Mac. It takes about 15 seconds.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            Spacer()
            if model.engineMatches == false && !model.building {
                Button("Build") { model.buildEngine() }
                    .disabled(!model.canBuild)
            }
        }
    }

    private var savesSection: some View {
        Section {
            if model.saves.isEmpty {
                Text("None yet. In the game, MENU then QUIT saves.")
                    .foregroundStyle(.secondary)
            } else {
                ForEach(model.saves) { save in
                    HStack {
                        Image(systemName: "book.closed.fill").foregroundStyle(Color.coatBlue)
                        Text(save.name)
                        Spacer()
                        Text(save.saved, format: .dateTime.day().month().year().hour().minute())
                            .foregroundStyle(.secondary)
                    }
                }
            }
        } header: {
            HStack {
                Text("Saved games")
                Spacer()
                Button("Show in Finder") { NSWorkspace.shared.open(Paths.saves) }
                    .buttonStyle(.link)
                    .font(.caption)
            }
        } footer: {
            Text("Each name signed into the book has its own game. Sign in with the same name and choose \u{201C}old game\u{201D} to carry on.")
                .multilineTextAlignment(.leading)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var footer: some View {
        HStack(spacing: 12) {
            VStack(alignment: .leading, spacing: 2) {
                Text(model.status.isEmpty ? "F8 xBR  ·  F9 screenshot  ·  F10 smoothing  ·  F11 full screen  ·  F12 scaling" : model.status)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
            }
            Spacer()
            Button {
                model.play()
            } label: {
                Label(model.playing ? "Playing" : "Play", systemImage: "play.fill")
                    .font(.title3.weight(.semibold))
                    .padding(.horizontal, 18)
                    .padding(.vertical, 4)
                    .foregroundStyle(Color.ink)
            }
            .buttonStyle(.borderedProminent)
            .tint(.hatYellow)
            .controlSize(.large)
            .keyboardShortcut(.defaultAction)
            .disabled(!model.canPlay)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 14)
        .overlay(alignment: .top) { Divider() }
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.prompt = "Use This Folder"
        panel.message = "Choose the folder that holds the disc's files (it contains MADEURO.EXE)."
        if panel.runModal() == .OK, let url = panel.url { model.useFolder(url) }
    }

    private func chooseImage() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [UTType(filenameExtension: "iso") ?? .data, .diskImage]
        panel.prompt = "Unpack"
        panel.message = "Choose your own disc image of the game. It is unpacked into the app's folder; the image is not changed."
        if panel.runModal() == .OK, let url = panel.url { model.useImage(url) }
    }
}
