// Settings.swift - madeline.ini, the saved games, and where things live.
//
// The engine (src/main.c) reads and writes madeline.ini as "key=value" lines.
// The launcher edits the same file, changing only the keys it knows and
// leaving every other line as it found it.

import Foundation

enum Paths {
    /// ~/Library/Application Support/MadelineEuropeanAdventures: settings, saves, screenshots.
    static let support: URL = {
        if let over = ProcessInfo.processInfo.environment["MADELINE_SAVE_DIR"], !over.isEmpty {
            return URL(fileURLWithPath: over)
        }
        return FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/MadelineEuropeanAdventures")
    }()
    static let ini = support.appendingPathComponent("madeline.ini")
    static let saves = support.appendingPathComponent("CWONDERS/MADEURO")
    static let screenshots = support.appendingPathComponent("screenshots")
    /// Where a disc image chosen in the launcher is unpacked.
    static let disc = support.appendingPathComponent("disc")

    /// The checkout this app was built from (written into the app by `make app`):
    /// where the recompiler and the host's source are, for building the engine.
    static var sourceRoot: URL? {
        guard let file = Bundle.main.resourceURL?.appendingPathComponent("source-root"),
              let text = try? String(contentsOf: file, encoding: .utf8) else { return nil }
        let root = URL(fileURLWithPath: text.trimmingCharacters(in: .whitespacesAndNewlines))
        return FileManager.default.fileExists(atPath: root.appendingPathComponent("Makefile").path) ? root : nil
    }

    /// The game engine: beside the launcher inside the app, else a development build.
    static var engine: URL? {
        var candidates: [URL] = []
        if let over = ProcessInfo.processInfo.environment["MADELINE_ENGINE"] { candidates.append(URL(fileURLWithPath: over)) }
        if let exe = Bundle.main.executableURL { candidates.append(exe.deletingLastPathComponent().appendingPathComponent("madeline")) }
        candidates.append(URL(fileURLWithPath: FileManager.default.currentDirectoryPath).appendingPathComponent("build/madeline"))
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }
}

/// A "key=value" file that keeps its unknown lines and comments.
struct IniFile {
    private(set) var lines: [String] = []

    init(url: URL) {
        if let text = try? String(contentsOf: url, encoding: .utf8) {
            lines = text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
            if lines.last == "" { lines.removeLast() }
        }
    }

    private static func parse(_ line: String) -> (key: String, value: String)? {
        let body = line.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false).first.map(String.init) ?? ""
        guard let eq = body.firstIndex(of: "=") else { return nil }
        let key = body[..<eq].trimmingCharacters(in: .whitespaces)
        let value = body[body.index(after: eq)...].trimmingCharacters(in: .whitespaces)
        return key.isEmpty ? nil : (key, value)
    }

    func string(_ key: String) -> String? {
        lines.reversed().lazy.compactMap(IniFile.parse).first { $0.key == key }?.value
    }
    func flag(_ key: String, default fallback: Bool) -> Bool {
        guard let v = string(key)?.lowercased() else { return fallback }
        return (Int(v) ?? 0) != 0 || v == "yes" || v == "true" || v == "on"
    }

    mutating func set(_ key: String, _ value: String) {
        let entry = "\(key)=\(value)"
        if let i = lines.firstIndex(where: { IniFile.parse($0)?.key == key }) {
            lines[i] = entry
            lines = lines.enumerated().filter { $0.offset == i || IniFile.parse($0.element)?.key != key }.map(\.element)
        } else {
            lines.append(entry)
        }
    }

    func write(to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try (lines.joined(separator: "\n") + "\n").write(to: url, atomically: true, encoding: .utf8)
    }
}

/// One of the game's ten save slots.
struct SavedGame: Identifiable {
    let slot: Int
    let name: String
    let saved: Date
    var id: Int { slot }

    /// A save begins with the player's name (NUL-terminated, 16 bytes); a
    /// non-zero word at 0xF0 marks a file the game discards (docs/NOTES.md).
    static func all() -> [SavedGame] {
        (0..<10).compactMap { slot -> SavedGame? in
            let url = Paths.saves.appendingPathComponent("\(slot).sav")
            guard let handle = try? FileHandle(forReadingFrom: url),
                  let head = try? handle.read(upToCount: 0xF2), head.count == 0xF2,
                  head[0xF0] == 0, head[0xF1] == 0, head[0] != 0 else { return nil }
            let bytes = head.prefix(15).prefix { $0 != 0 }
            let name = String(decoding: bytes, as: UTF8.self)
            let date = (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast
            return SavedGame(slot: slot, name: name, saved: date)
        }
        .sorted { $0.saved > $1.saved }
    }
}

enum Scaling: String, CaseIterable, Identifiable {
    case smooth, sharp, crt, nearest, integer
    var id: String { rawValue }
    var label: String {
        switch self {
        case .smooth: return "Smooth"
        case .sharp: return "Sharp"
        case .crt: return "CRT monitor"
        case .nearest: return "Blocky pixels"
        case .integer: return "Whole multiples only"
        }
    }
}

/// Everything the launcher shows, loaded from and saved to madeline.ini.
@MainActor
final class LauncherModel: ObservableObject {
    @Published var discPath: String { didSet { save() } }
    @Published var skipIntro: Bool { didSet { save() } }
    @Published var fullscreen: Bool { didSet { save() } }
    @Published var rememberName: Bool { didSet { save() } }
    @Published var smooth: Bool { didSet { save() } }
    @Published var scaling: Scaling { didSet { save() } }
    @Published var blurBars: Bool { didSet { save() } }

    @Published var saves: [SavedGame] = []
    @Published var status = ""
    @Published var busy = false          // unpacking a disc image, or building the engine
    @Published var playing = false
    /// Whether the engine in this app was recompiled from this disc's MADEURO.EXE.
    /// The engine *is* the game's code, lifted from one particular build of it;
    /// a disc with a different build needs its own engine. nil: not checked yet.
    @Published var engineMatches: Bool?
    @Published var building = false

    private var loading = true

    init() {
        let ini = IniFile(url: Paths.ini)
        discPath = ini.string("cd") ?? ""
        skipIntro = ini.flag("skip_intro", default: false)
        fullscreen = ini.flag("fullscreen", default: false)
        rememberName = ini.flag("remember_name", default: true)
        smooth = ini.flag("smooth", default: true)
        scaling = Scaling(rawValue: ini.string("scale") ?? "") ?? .smooth
        blurBars = (ini.string("bars") ?? "blur") != "black"
        loading = false
        saves = SavedGame.all()
        checkEngine()
    }

    /// Re-read what the game may have changed while it ran (F10, F11, F12) and the saves.
    func reload() {
        loading = true
        let ini = IniFile(url: Paths.ini)
        fullscreen = ini.flag("fullscreen", default: fullscreen)
        smooth = ini.flag("smooth", default: smooth)
        scaling = Scaling(rawValue: ini.string("scale") ?? "") ?? scaling
        blurBars = (ini.string("bars") ?? (blurBars ? "blur" : "black")) != "black"
        loading = false
        saves = SavedGame.all()
    }

    private func save() {
        guard !loading else { return }
        var ini = IniFile(url: Paths.ini)
        ini.set("cd", discPath)
        ini.set("skip_intro", skipIntro ? "1" : "0")
        ini.set("fullscreen", fullscreen ? "1" : "0")
        ini.set("remember_name", rememberName ? "1" : "0")
        ini.set("smooth", smooth ? "1" : "0")
        ini.set("scale", scaling.rawValue)
        ini.set("bars", blurBars ? "blur" : "black")
        do { try ini.write(to: Paths.ini) } catch { status = "Could not save settings: \(error.localizedDescription)" }
    }

    // MARK: the disc

    var discIsValid: Bool { LauncherModel.isDisc(discPath) }

    nonisolated static func isDisc(_ path: String) -> Bool {
        guard !path.isEmpty, let names = try? FileManager.default.contentsOfDirectory(atPath: path) else { return false }
        return names.contains { $0.uppercased() == "MADEURO.EXE" }
    }

    func useFolder(_ url: URL) {
        if LauncherModel.isDisc(url.path) {
            discPath = url.path
            status = "Disc folder set."
            checkEngine(thenBuild: true)
        } else {
            status = "That folder is not the game's disc: there is no MADEURO.EXE in it."
        }
    }

    /// Unpack the player's own disc image with the system's bsdtar.
    func useImage(_ url: URL) {
        busy = true
        status = "Unpacking \(url.lastPathComponent)…"
        let dest = Paths.disc
        Task.detached {
            let result: String? = {
                do {
                    try? FileManager.default.removeItem(at: dest)
                    try FileManager.default.createDirectory(at: dest, withIntermediateDirectories: true)
                    let tar = Process()
                    tar.executableURL = URL(fileURLWithPath: "/usr/bin/bsdtar")
                    tar.arguments = ["-xf", url.path, "-C", dest.path]
                    tar.standardError = FileHandle.nullDevice
                    try tar.run()
                    tar.waitUntilExit()
                    guard tar.terminationStatus == 0 else { return "Could not read that disc image." }
                    let chmod = Process()
                    chmod.executableURL = URL(fileURLWithPath: "/bin/chmod")
                    chmod.arguments = ["-R", "u+rwX", dest.path]
                    try chmod.run()
                    chmod.waitUntilExit()
                    return LauncherModel.isDisc(dest.path) ? nil : "That image is not the game's disc: there is no MADEURO.EXE on it."
                } catch {
                    return error.localizedDescription
                }
            }()
            await MainActor.run {
                self.busy = false
                if let problem = result {
                    self.status = problem
                } else {
                    self.discPath = dest.path
                    self.status = "Disc unpacked."
                    self.checkEngine(thenBuild: true)
                }
            }
        }
    }

    // MARK: the engine

    /// Ask the engine whether it was built for this disc (`madeline --check`: 0 yes, 3 no).
    func checkEngine(thenBuild: Bool = false) {
        guard discIsValid else { engineMatches = nil; return }
        guard let engine = Paths.engine else {
            engineMatches = false
            if thenBuild { buildEngine() }
            return
        }
        let disc = discPath
        Task.detached {
            let check = Process()
            check.executableURL = engine
            check.arguments = ["--check", disc]
            check.standardOutput = FileHandle.nullDevice
            check.standardError = FileHandle.nullDevice
            let ok = (try? check.run()) != nil && { check.waitUntilExit(); return check.terminationStatus == 0 }()
            await MainActor.run {
                guard self.discPath == disc else { return }
                self.engineMatches = ok
                if !ok && thenBuild { self.buildEngine() }
            }
        }
    }

    var canBuild: Bool { Paths.sourceRoot != nil && discIsValid && !busy && !building && !playing }

    /// Recompile the game from this disc: `make engine-into-app CD=<disc>` in the
    /// source checkout lifts MADEURO.EXE to C, compiles it with the host, and
    /// puts the new engine in this app. About fifteen seconds.
    func buildEngine() {
        guard let root = Paths.sourceRoot else {
            status = "This disc needs its own engine, and this app does not know where its source is. Run: make app CD=<disc folder>"
            return
        }
        building = true
        status = "Recompiling the game from your disc…"
        let disc = discPath
        Task.detached {
            let make = Process()
            make.executableURL = URL(fileURLWithPath: "/usr/bin/make")
            make.arguments = ["-C", root.path, "engine-into-app", "CD=\(disc)"]
            var env = ProcessInfo.processInfo.environment
            env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"   // an app is not given the shell's PATH
            make.environment = env
            let log = root.appendingPathComponent("build/launcher-build.log")
            try? FileManager.default.createDirectory(at: log.deletingLastPathComponent(), withIntermediateDirectories: true)
            FileManager.default.createFile(atPath: log.path, contents: nil)
            let out = try? FileHandle(forWritingTo: log)
            make.standardOutput = out ?? FileHandle.nullDevice
            make.standardError = out ?? FileHandle.nullDevice
            let ran = (try? make.run()) != nil
            if ran { make.waitUntilExit() }
            let ok = ran && make.terminationStatus == 0
            await MainActor.run {
                self.building = false
                if ok {
                    self.status = "Engine built for this disc."
                    self.checkEngine()
                } else {
                    self.engineMatches = false
                    self.status = "The build failed. See \((log.path as NSString).abbreviatingWithTildeInPath)"
                }
            }
        }
    }

    // MARK: playing

    var canPlay: Bool { discIsValid && engineMatches == true && !busy && !building && !playing && Paths.engine != nil }

    func play() {
        guard let engine = Paths.engine else { status = "The game engine is missing from this app."; return }
        let game = Process()
        game.executableURL = engine
        game.arguments = [discPath]
        game.currentDirectoryURL = URL(fileURLWithPath: discPath)
        game.standardOutput = FileHandle.nullDevice
        game.standardError = FileHandle.nullDevice
        game.terminationHandler = { finished in
            let code = finished.terminationStatus
            Task { @MainActor in
                self.playing = false
                self.reload()
                self.status = code == 0 ? "" : "The game stopped unexpectedly (code \(code))."
            }
        }
        do {
            try game.run()
            playing = true
            status = "Playing…"
        } catch {
            status = "Could not start the game: \(error.localizedDescription)"
        }
    }
}
