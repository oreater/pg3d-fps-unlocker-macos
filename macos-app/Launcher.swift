import AppKit
import SwiftUI
import CoreText

/// Pixel Gun 3D palette: navy lobby panels, sky-blue and yellow buttons with
/// dark outlines, green for "go", red for trouble, gold for highlights.
private enum Palette {
    static let backgroundTop = Color(red: 0.086, green: 0.157, blue: 0.267)
    static let background = Color(red: 0.047, green: 0.086, blue: 0.157)
    static let heroTop = Color(red: 0.118, green: 0.255, blue: 0.459)
    static let heroBottom = Color(red: 0.071, green: 0.149, blue: 0.290)
    static let panelTop = Color(red: 0.106, green: 0.188, blue: 0.318)
    static let panel = Color(red: 0.078, green: 0.141, blue: 0.247)
    static let panelEdge = Color(red: 0.173, green: 0.286, blue: 0.451)
    static let raised = Color(red: 0.122, green: 0.212, blue: 0.353)
    static let raisedHover = Color(red: 0.149, green: 0.259, blue: 0.424)
    static let inset = Color(red: 0.035, green: 0.067, blue: 0.125)
    static let outline = Color(red: 0.020, green: 0.031, blue: 0.063)
    static let gold = Color(red: 1.0, green: 0.816, blue: 0.227)
    static let sky = Color(red: 0.420, green: 0.800, blue: 1.0)
    static let green = Color(red: 0.420, green: 0.859, blue: 0.227)
    static let red = Color(red: 1.0, green: 0.302, blue: 0.302)
    static let text = Color.white
    static let secondary = Color(red: 0.663, green: 0.737, blue: 0.851)
    static let tertiary = Color(red: 0.427, green: 0.510, blue: 0.639)
}

@MainActor final class LauncherModel: ObservableObject {
    @Published var profile = UserDefaults.standard.string(forKey: "profile") ?? "balanced"
    @Published var optimizerStatus = "Not active"
    @Published var cameraStatus = "Camera audit appears after launch."
    private var liveLogPath: String?
    @Published var fps = UserDefaults.standard.string(forKey: "fps") ?? "uncapped"
    @Published var hud = UserDefaults.standard.bool(forKey: "hud")
    @Published var gfxJobs = UserDefaults.standard.bool(forKey: "gfxJobs")
    @Published var fastMouse = UserDefaults.standard.bool(forKey: "fastMouse")
    /// Unity job worker threads: "auto" keeps Unity's default (one per core minus one).
    @Published var jobWorkers = UserDefaults.standard.string(forKey: "jobWorkers") ?? "auto"
    static let jobWorkerChoices = ["auto", "2", "3", "4", "5", "6", "8", "12"]
    /// Live fast-mouse readback; empty when no session telemetry is available.
    @Published var mouseStatus = ""
    @Published var mouseWarning = false
    @Published var gamePath = UserDefaults.standard.string(forKey: "gamePath") ??
        NSHomeDirectory() + "/Library/Application Support/Steam/steamapps/common/Pixel Gun 3D PC Edition/Pixel Gun 3D.app"
    @Published var steamPath = UserDefaults.standard.string(forKey: "steamPath") ??
        NSHomeDirectory() + "/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS"
    @Published var steamRunning = false
    @Published var gameRunning = false
    @Published var busy = false
    @Published var gameFound = false
    @Published var version = "Not found"
    @Published var buildSupported = true
    private var supportedBuilds: [String] = []
    @Published var message = "Open Steam, then launch your game."
    @Published var console = "Launch output will appear here. Your installed game files stay untouched."
    @Published var engineFPS = "—"
    @Published var metalFPS = "—"
    @Published var overlay = "Not active"
    @Published var measurementNote = "Live measurements appear after launch."
    @Published var hasFailure = false
    private var timer: Timer?
    private var process: Process?
    private var runningGamePIDs = Set<Int32>()
    private var lastLog = ""
    private let logDirectory = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Logs/OptimizerUnlocker", isDirectory: true)

    var backend: URL? {
        Bundle.main.resourceURL?.appendingPathComponent("Launcher/testificateunlocker")
    }
    var ready: Bool { steamRunning && gameFound && buildSupported && !busy && !gameRunning }
    var statusTitle: String {
        if busy { return "Preparing your session" }
        if gameRunning { return "Game is running" }
        if !gameFound { return "Locate Pixel Gun 3D" }
        if !buildSupported { return "Game update needs review" }
        if !steamRunning { return "Steam is required" }
        return "Ready to launch"
    }

    init() {
        let choices = ["uncapped", "120", "144", "165", "240", "360", "500", "600", "1000"]
        if !choices.contains(fps) { fps = "uncapped" }
        if !["original", "balanced", "performance"].contains(profile) { profile = "balanced" }
        if !Self.jobWorkerChoices.contains(jobWorkers) { jobWorkers = "auto" }
        supportedBuilds = loadSupportedBuilds()
        refresh()
        if steamRunning && gameFound { message = "Ready when you are. Pick a profile and launch." }
        if gameFound && !buildSupported {
            message = "This Pixel Gun 3D version hasn’t been reviewed yet. Play it from Steam until the optimizer is updated."
        }
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    /// Reviewed builds come from the bundled backend so the list lives in one place.
    /// An empty result never blocks launch; the backend still refuses unknown builds.
    private func loadSupportedBuilds() -> [String] {
        guard let backend, FileManager.default.fileExists(atPath: backend.path) else { return [] }
        let task = Process(), pipe = Pipe()
        task.executableURL = URL(fileURLWithPath: "/bin/zsh")
        task.arguments = [backend.path, "--supported-builds"]
        task.standardOutput = pipe; task.standardError = FileHandle.nullDevice
        task.standardInput = FileHandle.nullDevice
        guard (try? task.run()) != nil else { return [] }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        guard task.terminationStatus == 0 else { return [] }
        return String(decoding: data, as: UTF8.self).split(separator: "\n")
            .map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    func save() {
        UserDefaults.standard.set(profile, forKey: "profile")
        UserDefaults.standard.set(fps, forKey: "fps")
        UserDefaults.standard.set(hud, forKey: "hud")
        UserDefaults.standard.set(gfxJobs, forKey: "gfxJobs")
        UserDefaults.standard.set(fastMouse, forKey: "fastMouse")
        UserDefaults.standard.set(jobWorkers, forKey: "jobWorkers")
        UserDefaults.standard.set(gamePath, forKey: "gamePath")
        UserDefaults.standard.set(steamPath, forKey: "steamPath")
    }

    func refresh() {
        let apps = NSWorkspace.shared.runningApplications
        steamRunning = apps.contains { $0.bundleIdentifier == "com.valvesoftware.steam" ||
            $0.executableURL?.lastPathComponent == "steam_osx" }
        runningGamePIDs = Set(apps.filter { $0.bundleIdentifier == "com.pgcompany.pixelgun3d" }
            .map(\.processIdentifier))
        gameRunning = !runningGamePIDs.isEmpty
        gameFound = FileManager.default.isExecutableFile(atPath: gamePath + "/Contents/MacOS/Pixel Gun 3D")
        if let info = NSDictionary(contentsOfFile: gamePath + "/Contents/Info.plist"), gameFound {
            let short = "\(info["CFBundleShortVersionString"] ?? "Unknown")"
            let build = "\(info["CFBundleVersion"] ?? "unknown")"
            buildSupported = supportedBuilds.isEmpty || supportedBuilds.contains("\(short)/\(build)")
            version = "\(short) · build \(build)" + (buildSupported ? "" : " · not yet supported")
        } else { version = "Not found"; buildSupported = true }
        refreshMeasurements()
    }

    private func refreshMeasurements() {
        guard let record = try? String(contentsOf: logDirectory.appendingPathComponent("latest"), encoding: .utf8) else {
            resetMeasurements(); return
        }
        let path = record.trimmingCharacters(in: .whitespacesAndNewlines)
        let url = URL(fileURLWithPath: path).standardizedFileURL
        guard url.deletingLastPathComponent() == logDirectory.standardizedFileURL,
              url.lastPathComponent.hasPrefix("run-"), url.pathExtension == "log",
              let pidText = try? String(contentsOfFile: path + ".pid", encoding: .utf8),
              let pid = Int32(pidText.trimmingCharacters(in: .whitespacesAndNewlines)),
              runningGamePIDs.contains(pid) else { resetMeasurements(); return }
        guard let handle = try? FileHandle(forReadingFrom: url) else { return }
        defer { try? handle.close() }
        let end = (try? handle.seekToEnd()) ?? 0
        try? handle.seek(toOffset: end > 32768 ? end - 32768 : 0)
        let data = (try? handle.readToEnd()) ?? Data()
        let text = String(decoding: data, as: UTF8.self)
        lastLog = text
        liveLogPath = path
        let lines = text.split(separator: "\n").map(String.init)
        let attrs = try? FileManager.default.attributesOfItem(atPath: path)
        let modified = attrs?[.modificationDate] as? Date ?? .distantPast
        guard Date().timeIntervalSince(modified) < 20 else {
            engineFPS = "—"; metalFPS = "—"; overlay = "Unconfirmed"
            optimizerStatus = "Awaiting fresh readback"
            measurementNote = "Waiting for fresh measurements…"; return
        }
        if let line = lines.last(where: { $0.contains("optimizer status:") }) {
            optimizerStatus = line.components(separatedBy: "optimizer status: ").last?.components(separatedBy: ". Readback").first ?? "Unconfirmed"
        }
        if let line = lines.last(where: { $0.contains("optimizer postfx status:") }) {
            let disabled = number(after: "disabled=", in: line) ?? "0"
            optimizerStatus += " · Post FX off: " + disabled
        }
        if let line = lines.last(where: { $0.contains("optimizer frame guard:") }) {
            optimizerStatus += line.contains("installed=yes") && (Double(number(after: "calls=", in: line) ?? "0") ?? 0) > 0 ? " · Respawn guard active" : " · Respawn guard unconfirmed"
        }
        if let line = lines.last(where: { $0.contains("optimizer camera audit:") }) {
            cameraStatus = line.contains("occlusion_flag=on") ? "Main camera: occlusion flag already on. Map visibility data is unverified." :
                line.contains("occlusion_flag=off") ? "Main camera: occlusion flag off. Check any active culling diagnostic." : "No main camera available yet."
        }
        if let line = lines.last(where: { $0.contains("engine measurement:") }) {
            engineFPS = number(after: "rendered=", in: line) ?? "—"
            measurementNote = line.contains("focused=yes") ?
                "Metal presentation rate is not the screen’s refresh rate." :
                "Game is in the background; FPS may be reduced."
        }
        if let line = lines.last(where: { $0.contains("presentation sample:") }) {
            // Hidden or minimized windows keep rendering but macOS presents nothing;
            // every drawable is then reported as dropped rather than shown.
            let presented = Double(number(after: "presented_fps=", in: line) ?? "") ?? 0
            let dropped = Double(number(after: "dropped=", in: line) ?? "") ?? 0
            if presented == 0 && dropped > 0 {
                metalFPS = "—"
                measurementNote = "Game window isn’t visible, so nothing is being presented on screen."
            } else {
                metalFPS = number(after: "presented_fps=", in: line) ?? "—"
            }
        }
        if let line = lines.last(where: { $0.contains("Steam overlay:") }) {
            overlay = line.contains("IsOverlayEnabled=yes") ? "Enabled" : "Unconfirmed"
        }
        refreshMouseStatus(lines)
    }

    private func refreshMouseStatus(_ lines: [String]) {
        guard let status = lines.last(where: { $0.contains("fast mouse look status:") }) else {
            mouseStatus = ""; mouseWarning = false; return
        }
        mouseWarning = false
        if status.contains("requested=no") {
            mouseStatus = "Off in game · normal macOS routing"
        } else if status.contains("available=no") {
            let reason = status.components(separatedBy: "reason=").last?.trimmingCharacters(in: CharacterSet(charactersIn: ". ")) ?? "unknown"
            mouseStatus = "Unavailable: \(reason)"; mouseWarning = true
        } else if let sample = lines.last(where: { $0.contains("input sample:") }),
                  let moves = Double(number(after: "motion_events=", in: sample) ?? ""),
                  let fast = Double(number(after: "fast_events=", in: sample) ?? ""), moves > 0 {
            let cost = number(after: "fast_mean_us=", in: sample) ?? "—"
            mouseStatus = fast > 0 ? "Active on \(Int((fast / moves * 100).rounded()))% of moves · \(cost) µs each" :
                "On · waits for aiming (cursor locked)"
        } else {
            mouseStatus = "On · waits for aiming (cursor locked)"
        }
    }

    var jobWorkersClash: Bool { jobWorkers != "auto" && gfxJobs }

    var mouseDetail: String {
        if gameRunning && !mouseStatus.isEmpty { return mouseStatus }
        return fastMouse ? "Skips macOS window work when aiming." : "Off: normal macOS mouse routing."
    }

    func applyMouse() {
        save()
        guard let path = liveLogPath, gameRunning else {
            message = fastMouse ? "Fast mouse look saved for your next launch." : "Fast mouse look off for your next launch."
            return
        }
        do {
            try ((fastMouse ? "fast" : "game") + "\n").write(toFile: path + ".profile.mouse", atomically: true, encoding: .utf8)
            message = fastMouse ? "Fast mouse look requested. Readback within five seconds." :
                "Fast mouse look off. Readback within five seconds."
        } catch { fail(error.localizedDescription) }
    }

    private func number(after marker: String, in text: String) -> String? {
        guard let range = text.range(of: marker) else { return nil }
        let raw = text[range.upperBound...].prefix { $0.isNumber || $0 == "." }
        guard let value = Double(raw), value.isFinite else { return nil }
        return String(format: "%.0f", value)
    }

    private func resetMeasurements() {
        engineFPS = "—"; metalFPS = "—"; overlay = "Not active"; lastLog = ""
        liveLogPath = nil; optimizerStatus = "Not active"
        mouseStatus = ""; mouseWarning = false
        cameraStatus = "Camera audit appears after launch."
        measurementNote = gameRunning ? "This game session has no matching launcher telemetry." :
            "Live measurements appear after launch."
    }

    var profileDescription: String {
        switch profile {
        case "original": return "Testificate FPS unlock with the game’s rendering settings. Restores settings managed by this session."
        case "performance": return "Disables camera post-processing and the depth/HDR work it leaves behind, real-time shadows, MSAA and costly effects; lowers distant mesh detail and skinning cost. Baked shadows remain."
        default: return "Caps shadow distance, cascades, MSAA and lights; uses existing lower settings when available."
        }
    }
    func applyProfile() {
        save(); refresh()
        guard let path = liveLogPath, gameRunning else {
            message = "Profile saved for your next launch."; return
        }
        do {
            try (profile + "\n").write(toFile: path + ".profile", atomically: true, encoding: .utf8)
            message = "Requested \(profile). Live Unity readback updates within five seconds."
        } catch { fail(error.localizedDescription) }
    }

    func chooseGame() {
        let panel = NSOpenPanel()
        panel.title = "Choose Pixel Gun 3D.app"
        panel.canChooseFiles = true; panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false; panel.allowedContentTypes = [.application]
        panel.directoryURL = URL(fileURLWithPath: gamePath).deletingLastPathComponent()
        if panel.runModal() == .OK, let url = panel.url {
            guard FileManager.default.isExecutableFile(atPath: url.path + "/Contents/MacOS/Pixel Gun 3D") else {
                fail("Choose the actual Pixel Gun 3D.app installation."); return
            }
            gamePath = url.path; save(); refresh()
        }
    }

    func chooseSteam() {
        let panel = NSOpenPanel()
        panel.title = "Choose Steam's binary directory"
        panel.message = "Choose Contents/MacOS containing steamloader.dylib and gameoverlayrenderer.dylib."
        panel.canChooseFiles = false; panel.canChooseDirectories = true
        panel.directoryURL = URL(fileURLWithPath: steamPath)
        if panel.runModal() == .OK, let url = panel.url {
            let valid = ["steamloader.dylib", "gameoverlayrenderer.dylib"].allSatisfy {
                FileManager.default.fileExists(atPath: url.appendingPathComponent($0).path)
            }
            guard valid else { fail("That folder does not contain Steam's overlay libraries."); return }
            steamPath = url.path; save(); refresh()
        }
    }

    func openSteam() {
        guard let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.valvesoftware.steam") else {
            fail("Steam wasn't found. Install Steam, sign in, then return here."); return
        }
        NSWorkspace.shared.openApplication(at: url, configuration: .init()) { _, error in
            if let error { Task { @MainActor in self.fail(error.localizedDescription) } }
        }
        message = "Open Steam and sign in, then return to launch."
    }

    func showLogs() {
        guard FileManager.default.fileExists(atPath: logDirectory.path) else {
            message = "No launcher logs yet. They are created when a game is launched."; return
        }
        NSWorkspace.shared.open(logDirectory)
    }

    func showLiveLog() {
        console = lastLog.isEmpty ? "No matching live session. Previous runs are available in the logs folder." : lastLog
    }

    func fail(_ reason: String) { message = reason; hasFailure = true }

    func run(checkOnly: Bool) {
        guard !busy else { return }
        refresh(); save()
        guard let backend, FileManager.default.fileExists(atPath: backend.path) else {
            fail("The bundled launcher is missing. Rebuild or reinstall this app."); return
        }
        guard gameFound else { fail("Choose your Pixel Gun 3D.app first."); return }
        guard steamRunning else { fail("Open Steam and sign in first."); return }
        if !checkOnly && gameRunning { fail("Quit the current game before launching a new session."); return }
        let task = Process()
        let pipe = Pipe()
        task.executableURL = URL(fileURLWithPath: "/bin/zsh")
        var args = [backend.path, "--game", gamePath, "--steam-dir", steamPath, "--fps", fps, "--profile", profile]
        if checkOnly { args.append("--dry-run") }
        else if hud { args.append("--hud") }
        if gfxJobs { args.append("--gfx-jobs") }
        args += ["--mouse", fastMouse ? "fast" : "game", "--job-workers", jobWorkers]
        task.arguments = args
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        task.environment = env
        task.standardOutput = pipe; task.standardError = pipe
        task.standardInput = FileHandle.nullDevice
        busy = true; hasFailure = false
        message = checkOnly ? "Checking installation and bundled components…" : "Starting Pixel Gun 3D and checking rendering settings + FPS + Steam overlay…"
        console = ""; process = task
        do { try task.run() }
        catch { busy = false; process = nil; fail(error.localizedDescription); return }
        // Drain off the UI thread, so long output cannot deadlock the child.
        DispatchQueue.global(qos: .userInitiated).async {
            let output = pipe.fileHandleForReading.readDataToEndOfFile()
            task.waitUntilExit()
            let code = task.terminationStatus
            let text = String(decoding: output, as: UTF8.self)
            Task { @MainActor in
                self.busy = false; self.process = nil
                self.console = text.isEmpty ? "Launcher exited with status \(code)." : text
                // Exit statuses are defined at the top of the backend launcher script.
                self.hasFailure = code != 0 && code != 5
                switch code {
                case 0:
                    self.message = checkOnly ? "Setup checks passed. No game was launched." :
                        "FPS and Steam overlay are active. Check the rendering readback below."
                case 3:
                    self.message = "This Pixel Gun 3D version hasn’t been reviewed yet. Play it from Steam until the optimizer is updated."
                case 4:
                    self.message = "This game build can’t load the optimizer. See the output below; the game was not changed."
                case 5:
                    self.message = "The game is still loading. Live readback appears here once it’s ready."
                default:
                    self.message = "Launcher needs attention. See the output below."
                }
                self.refresh()
            }
        }
    }
}

// MARK: - Pixel Gun style

/// Russo One is the UI font Pixel Gun 3D ships (RussoOneUIFont in its assets).
/// The OFL-licensed copy from Google Fonts is bundled and registered at launch.
private func pgFont(_ size: CGFloat) -> Font { .custom("RussoOne-Regular", size: size) }

/// The app's name lives in Info.plist (CFBundleName) only.
private let appName = Bundle.main.object(forInfoDictionaryKey: "CFBundleName") as? String ?? "PG3D FPS Unlock"

/// Original pixel-art sprites drawn from character grids. Each character is one
/// pixel: K outline, W white, S steel, D dark steel, N wood, Y yellow, O orange,
/// R red, B blue, C cyan, G green; anything else is transparent.
private enum Sprite {
    static let pistol = [
        "..KKKKKKKKKKK",
        ".KSSSSSSSSSSK",
        ".KSWWSSSSSSSK",
        ".KDDDDDDDDDKK",
        ".KNNKDKKKKK..",
        "KNNNKKDK.....",
        "KNNNK.KK.....",
        "KNNNK........",
        "KKKKK........"]
    static let medkit = [
        "..KKKKK..",
        "..K...K..",
        "KKKKKKKKK",
        "KWWWRWWWK",
        "KWWWRWWWK",
        "KWRRRRRWK",
        "KWWWRWWWK",
        "KSWWRWWSK",
        "KKKKKKKKK"]
    static let gamepad = [
        ".KKKKKKKKKK.",
        "KSSSSSSSSSSK",
        "KSKSSSSSSRSK",
        "KKKKSSSSRSGK",
        "KSKSSSSSSBSK",
        "KSSSKKKKSSSK",
        ".KKK....KKK."]
    static let scroll = [
        ".KKKKKKKK.",
        "KNWWWWWWNK",
        ".KWKKKKWK.",
        ".KWWWWWWK.",
        ".KWKKKKWK.",
        ".KWWWWWWK.",
        ".KWKKKWWK.",
        "KNWWWWWWNK",
        ".KKKKKKKK."]
    static let chest = [
        ".KKKKKKKKK.",
        "KNNNNNNNNNK",
        "KNONNNNNONK",
        "KKKKKYKKKKK",
        "KNNNKYKNNNK",
        "KNONNNNNONK",
        "KNNNNNNNNNK",
        "KKKKKKKKKKK"]
    static let magnifier = [
        "..KKKK....",
        ".KCCWCK...",
        "KCCCCWCK..",
        "KCCCCCCK..",
        "KCCCCCCK..",
        ".KCCCCK...",
        "..KKKKSK..",
        "......KSK.",
        ".......KSK",
        "........KK"]
    static let bolt = [
        "....KKKK",
        "...KYYYK",
        "..KYYYK.",
        ".KYYYK..",
        "KYYYYYKK",
        "KKKYYYYK",
        "..KYYYK.",
        ".KYYK...",
        ".KYK....",
        "KKK....."]
    static let shield = [
        "KKKKKKKKK",
        "KCCCCBBBK",
        "KCWCCBBBK",
        "KCCCCBBBK",
        "KCCCCBBBK",
        ".KCCCBBK.",
        ".KCCCBBK.",
        "..KCCBK..",
        "...KBK...",
        "....K...."]
    static let eye = [
        "...KKKKK...",
        ".KKWWWWWKK.",
        "KWWWKKKWWWK",
        "KWWKBCBKWWK",
        "KWWKBKBKWWK",
        ".KKWKKKWKK.",
        "...KKKKK..."]
    // White, not outline, so it reads on the dark metric tiles.
    static let crosshair = [
        ".....W.....",
        ".....W.....",
        "...WWWWW...",
        "..W.....W..",
        ".W.......W.",
        "WWW..R..WWW",
        ".W.......W.",
        "..W.....W..",
        "...WWWWW...",
        ".....W.....",
        ".....W....."]
    static let heart = [
        ".KK...KK.",
        "KRRK.KRRK",
        "KRWRKRRRK",
        "KRRRRRRRK",
        ".KRRRRRK.",
        "..KRRRK..",
        "...KRK...",
        "....K...."]
    static let gear = [
        "...KKK...",
        ".KKSSSKK.",
        ".KSSSSSK.",
        "KSSKKKSSK",
        "KSSK.KSSK",
        "KSSKKKSSK",
        ".KSSSSSK.",
        ".KKSSSKK.",
        "...KKK..."]
    static let check = [
        ".......KK",
        "......KGK",
        "KK...KGK.",
        "KGK.KGK..",
        ".KGKGK...",
        "..KGK....",
        "...K....."]
    static let warning = [
        "....K....",
        "...KYK...",
        "...KYK...",
        "..KYKYK..",
        "..KYKYK..",
        ".KYYKYYK.",
        ".KYYYYYK.",
        "KYYYKYYYK",
        "KKKKKKKKK"]
}

private struct PixelSprite: View {
    var rows: [String]
    var pixel: CGFloat = 2
    var outline: Color = Color(red: 0.05, green: 0.07, blue: 0.12)
    private static let colors: [Character: Color] = [
        "W": .white, "S": Color(red: 0.72, green: 0.77, blue: 0.84), "D": Color(red: 0.42, green: 0.47, blue: 0.55),
        "N": Color(red: 0.55, green: 0.33, blue: 0.16), "Y": Color(red: 1.0, green: 0.84, blue: 0.20),
        "O": Color(red: 1.0, green: 0.58, blue: 0.10), "R": Color(red: 0.93, green: 0.22, blue: 0.20),
        "B": Color(red: 0.12, green: 0.45, blue: 0.90), "C": Color(red: 0.36, green: 0.80, blue: 1.0),
        "G": Color(red: 0.40, green: 0.84, blue: 0.23)]
    var body: some View {
        let width = rows.map(\.count).max() ?? 0
        Canvas { context, _ in
            for (y, row) in rows.enumerated() {
                for (x, character) in row.enumerated() {
                    let color: Color? = character == "K" ? outline : Self.colors[character]
                    guard let color else { continue }
                    context.fill(Path(CGRect(x: CGFloat(x) * pixel, y: CGFloat(y) * pixel, width: pixel, height: pixel)), with: .color(color))
                }
            }
        }
        .frame(width: CGFloat(width) * pixel, height: CGFloat(rows.count) * pixel)
        .accessibilityHidden(true)
    }
}

/// Pixel Gun's chunky buttons: bright face, dark outline, a darker "lip" under
/// the face that the button presses into, and white outlined uppercase text.
private enum Chunk {
    case yellow, blue, green, steel
    var face: [Color] {
        switch self {
        case .yellow: return [Color(red: 1.0, green: 0.86, blue: 0.29), Color(red: 1.0, green: 0.65, blue: 0.10)]
        case .blue: return [Color(red: 0.25, green: 0.66, blue: 1.0), Color(red: 0.11, green: 0.43, blue: 0.86)]
        case .green: return [Color(red: 0.46, green: 0.87, blue: 0.24), Color(red: 0.24, green: 0.66, blue: 0.13)]
        case .steel: return [Color(red: 0.24, green: 0.33, blue: 0.47), Color(red: 0.17, green: 0.24, blue: 0.36)]
        }
    }
    var lip: Color {
        switch self {
        case .yellow: return Color(red: 0.74, green: 0.42, blue: 0.0)
        case .blue: return Color(red: 0.05, green: 0.27, blue: 0.60)
        case .green: return Color(red: 0.13, green: 0.44, blue: 0.06)
        case .steel: return Color(red: 0.09, green: 0.13, blue: 0.21)
        }
    }
}

private struct OutlinedText: View {
    var text: String
    var size: CGFloat
    var body: some View {
        ZStack {
            ForEach([(-1.2, 0.0), (1.2, 0.0), (0.0, -1.2), (0.0, 1.2), (1.2, 1.8), (0.0, 2.2)], id: \.0.description.hashValue) { offset in
                Text(text).font(pgFont(size)).foregroundColor(Palette.outline).offset(x: offset.0, y: offset.1)
            }
            Text(text).font(pgFont(size)).foregroundColor(.white)
        }
    }
}

struct ChunkyButton: ButtonStyle {
    fileprivate var kind: Chunk = .blue
    var large = false
    @Environment(\.isEnabled) private var enabled
    func makeBody(configuration: Configuration) -> some View {
        let lip: CGFloat = large ? 5 : 4
        let pressed = configuration.isPressed && enabled
        let face = enabled ? kind.face : Chunk.steel.face
        configuration.label
            .padding(.horizontal, large ? 22 : 12).padding(.vertical, large ? 13 : 7)
            .background(
                RoundedRectangle(cornerRadius: large ? 10 : 7)
                    .fill(LinearGradient(colors: face, startPoint: .top, endPoint: .bottom))
                    .overlay(alignment: .top) {
                        RoundedRectangle(cornerRadius: large ? 10 : 7).fill(Color.white.opacity(0.22))
                            .frame(height: large ? 10 : 7).padding(.horizontal, 4).padding(.top, 3)
                    }
            )
            .overlay(RoundedRectangle(cornerRadius: large ? 10 : 7).stroke(Palette.outline, lineWidth: 2))
            .opacity(enabled ? 1 : 0.55)
            .offset(y: pressed ? lip : 0)
            .background(
                RoundedRectangle(cornerRadius: large ? 10 : 7).fill(enabled ? kind.lip : Chunk.steel.lip)
                    .overlay(RoundedRectangle(cornerRadius: large ? 10 : 7).stroke(Palette.outline, lineWidth: 2))
                    .offset(y: lip)
            )
            .padding(.bottom, lip)
            .animation(.easeOut(duration: 0.06), value: pressed)
    }
}

/// Button label: a pixel sprite plus outlined Russo One text, like the game's buttons.
private struct ChunkLabel: View {
    var sprite: [String]
    var title: String
    var size: CGFloat = 12
    var pixel: CGFloat = 2
    var body: some View {
        HStack(spacing: 8) {
            PixelSprite(rows: sprite, pixel: pixel)
            OutlinedText(text: title.uppercased(), size: size)
        }
    }
}

private struct Card<Content: View>: View {
    var title: String
    var sprite: [String]
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack(spacing: 8) {
                PixelSprite(rows: sprite, pixel: 1.5)
                Text(title.uppercased()).font(pgFont(12)).tracking(1.2).foregroundColor(Palette.gold)
            }
            content
        }
        .padding(18).frame(maxWidth: .infinity, alignment: .topLeading)
        .background(RoundedRectangle(cornerRadius: 14).fill(LinearGradient(colors: [Palette.panelTop, Palette.panel], startPoint: .top, endPoint: .bottom)))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Palette.outline, lineWidth: 2))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Palette.panelEdge, lineWidth: 1).padding(2))
    }
}

private struct Pill: View {
    var text: String
    var active: Bool
    var body: some View {
        HStack(spacing: 6) {
            Rectangle().fill(active ? Palette.green : Palette.tertiary).frame(width: 7, height: 7)
                .overlay(Rectangle().stroke(Palette.outline, lineWidth: 1))
            Text(text.uppercased()).font(pgFont(10))
        }
        .padding(.horizontal, 10).padding(.vertical, 6)
        .foregroundColor(active ? .white : Palette.secondary)
        .background(RoundedRectangle(cornerRadius: 6).fill(active ? Palette.green.opacity(0.18) : Palette.inset))
        .overlay(RoundedRectangle(cornerRadius: 6).stroke(active ? Palette.green.opacity(0.7) : Palette.panelEdge, lineWidth: 1.5))
    }
}

private struct ProfileOption: View {
    var id: String, name: String, tagline: String, sprite: [String]
    @Binding var selection: String
    @State private var hovering = false
    var body: some View {
        let selected = selection == id
        Button { selection = id } label: {
            VStack(alignment: .leading, spacing: 8) {
                HStack(alignment: .top) {
                    PixelSprite(rows: sprite, pixel: 3)
                    Spacer()
                    if selected { PixelSprite(rows: Sprite.check, pixel: 2) }
                }
                Text(name.uppercased()).font(pgFont(14)).foregroundColor(selected ? Palette.gold : .white)
                Text(tagline).font(.system(size: 10, weight: .medium)).foregroundColor(Palette.secondary)
                    .lineLimit(2).fixedSize(horizontal: false, vertical: true)
            }
            .padding(12).frame(maxWidth: .infinity, minHeight: 118, alignment: .topLeading)
            .background(RoundedRectangle(cornerRadius: 10).fill(selected ? Palette.gold.opacity(0.12) : (hovering ? Palette.raisedHover : Palette.raised)))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(selected ? Palette.gold : Palette.outline, lineWidth: selected ? 3 : 2))
            .contentShape(RoundedRectangle(cornerRadius: 10))
        }
        .buttonStyle(.plain).onHover { hovering = $0 }
        .animation(.easeOut(duration: 0.12), value: selected)
    }
}

// MARK: - Main view

struct LauncherView: View {
    @ObservedObject var model: LauncherModel
    @State private var advanced = false

    private var statusSprite: [String] {
        if model.gameRunning { return Sprite.gamepad }
        if model.ready { return Sprite.check }
        return Sprite.warning
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            hero
            HStack(alignment: .top, spacing: 16) {
                profileCard
                settingsCard.frame(width: 340)
            }
            consoleCard
            HStack(spacing: 6) {
                Text("UI FONT: RUSSO ONE (SIL OFL)").font(pgFont(9))
                Spacer()
                Text("In-memory changes only · game files and saved settings stay untouched · not affiliated with Pixel Gun 3D")
                    .font(.system(size: 10, weight: .medium))
            }.foregroundColor(Palette.tertiary)
        }
        .padding(.horizontal, 24).padding(.top, 34).padding(.bottom, 16)
        .frame(minWidth: 960, maxWidth: .infinity, minHeight: 940, maxHeight: .infinity, alignment: .top)
        .background(backdrop)
        .foregroundColor(Palette.text).preferredColorScheme(.dark)
        .onChange(of: model.profile) { _ in model.save() }
        .onChange(of: model.fps) { _ in model.save() }
        .onChange(of: model.hud) { _ in model.save() }
        .onChange(of: model.gfxJobs) { _ in model.save() }
        .onChange(of: model.fastMouse) { _ in model.applyMouse() }
        .onChange(of: model.jobWorkers) { _ in model.save() }
    }

    private var backdrop: some View {
        ZStack {
            LinearGradient(colors: [Palette.backgroundTop, Palette.background], startPoint: .top, endPoint: .bottom)
            // Faint block grid, a nod to the game's voxel world.
            Canvas { context, size in
                let cell: CGFloat = 32
                var path = Path()
                var x: CGFloat = 0
                while x < size.width { path.move(to: CGPoint(x: x, y: 0)); path.addLine(to: CGPoint(x: x, y: size.height)); x += cell }
                var y: CGFloat = 0
                while y < size.height { path.move(to: CGPoint(x: 0, y: y)); path.addLine(to: CGPoint(x: size.width, y: y)); y += cell }
                context.stroke(path, with: .color(Color.white.opacity(0.025)), lineWidth: 1)
            }
        }.ignoresSafeArea()
    }

    private var header: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: 10).fill(LinearGradient(colors: Chunk.yellow.face, startPoint: .top, endPoint: .bottom))
                    .frame(width: 52, height: 52)
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(Palette.outline, lineWidth: 2.5))
                PixelSprite(rows: Sprite.pistol, pixel: 3)
            }
            VStack(alignment: .leading, spacing: 3) {
                OutlinedText(text: appName.uppercased(), size: 24)
                Text("FOR PIXEL GUN 3D · MACOS").font(pgFont(10)).tracking(2).foregroundColor(Palette.sky)
            }
            Spacer()
            Pill(text: "v\(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "—")", active: false)
            Pill(text: model.steamRunning ? "Steam online" : "Steam closed", active: model.steamRunning)
            Pill(text: model.gameRunning ? "In game" : "Game idle", active: model.gameRunning)
        }
    }

    private var hero: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .center, spacing: 20) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        PixelSprite(rows: statusSprite, pixel: 2)
                        Text("SESSION").font(pgFont(11)).tracking(1.6).foregroundColor(Palette.gold)
                    }
                    OutlinedText(text: model.statusTitle.uppercased(), size: 26)
                    Text("Unlocked frame rate, lighter rendering, Steam overlay intact.")
                        .font(.system(size: 12, weight: .medium)).foregroundColor(Palette.secondary)
                }
                Spacer(minLength: 12)
                VStack(alignment: .trailing, spacing: 8) {
                    Button(action: { model.run(checkOnly: false) }) {
                        HStack(spacing: 10) {
                            if model.busy { ProgressView().controlSize(.small) }
                            else { PixelSprite(rows: model.gameRunning ? Sprite.check : Sprite.pistol, pixel: 2.5) }
                            OutlinedText(text: model.busy ? "LOADING…" : model.gameRunning ? "IN GAME" : "PLAY", size: 20)
                        }.frame(minWidth: 200)
                    }.buttonStyle(ChunkyButton(kind: model.gameRunning ? .green : .yellow, large: true)).disabled(!model.ready)
                    HStack(spacing: 8) {
                        Button { model.run(checkOnly: true) } label: { ChunkLabel(sprite: Sprite.medkit, title: "Check setup") }
                            .buttonStyle(ChunkyButton(kind: .blue)).disabled(model.busy)
                        Button(action: model.openSteam) { ChunkLabel(sprite: Sprite.gamepad, title: "Open Steam") }
                            .buttonStyle(ChunkyButton(kind: .blue))
                    }
                }
            }
            HStack(spacing: 12) {
                metric(model.engineFPS, "Engine FPS", Sprite.bolt)
                metric(model.metalFPS, "Metal FPS", Sprite.crosshair)
                metric(model.overlay, "Steam overlay", Sprite.gamepad, small: true)
                metric(model.profile.capitalized, "Profile", Sprite.shield, small: true)
            }
            Text(model.measurementNote).font(.system(size: 10, weight: .medium)).foregroundColor(Palette.tertiary)
        }
        .padding(20)
        .background(
            RoundedRectangle(cornerRadius: 16).fill(LinearGradient(colors: [Palette.heroTop, Palette.heroBottom], startPoint: .top, endPoint: .bottom))
        )
        .overlay(RoundedRectangle(cornerRadius: 16).stroke(Palette.outline, lineWidth: 2.5))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(Palette.sky.opacity(0.45), lineWidth: 1).padding(2.5))
    }

    private var profileCard: some View {
        Card(title: "Rendering profile", sprite: Sprite.gear) {
            HStack(spacing: 10) {
                ProfileOption(id: "original", name: "Original", tagline: "Game visuals, FPS unlocked", sprite: Sprite.eye, selection: $model.profile)
                ProfileOption(id: "balanced", name: "Balanced", tagline: "Lighter shadows, AA and lights", sprite: Sprite.shield, selection: $model.profile)
                ProfileOption(id: "performance", name: "Performance", tagline: "Maximum frame rate", sprite: Sprite.bolt, selection: $model.profile)
            }.disabled(model.busy)
            Text(model.profileDescription).font(.system(size: 11, weight: .medium)).foregroundColor(Palette.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Button(action: model.applyProfile) {
                ChunkLabel(sprite: model.gameRunning ? Sprite.bolt : Sprite.chest, title: model.gameRunning ? "Apply in game" : "Save profile")
            }.buttonStyle(ChunkyButton(kind: model.gameRunning ? .green : .blue)).disabled(model.busy)
            VStack(alignment: .leading, spacing: 6) {
                Text(model.optimizerStatus).font(.system(size: 10, design: .monospaced)).foregroundColor(Palette.gold)
                    .fixedSize(horizontal: false, vertical: true).textSelection(.enabled)
                Text(model.cameraStatus).font(.system(size: 10)).foregroundColor(Palette.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(10).frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 8).fill(Palette.inset))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(Palette.outline, lineWidth: 1.5))
        }
    }

    private var settingsCard: some View {
        Card(title: "Session settings", sprite: Sprite.medkit) {
            Group {
                settingRow("Frame-rate cap", "A ceiling, not a guarantee.") {
                    Picker("Frame-rate cap", selection: $model.fps) {
                        Text("Uncapped").tag("uncapped")
                        ForEach(["120", "144", "165", "240", "360", "500", "600", "1000"], id: \.self) { Text("\($0) FPS").tag($0) }
                    }.labelsHidden().frame(width: 120).tint(Palette.gold)
                }
                divider
                settingRow("Metal performance HUD", "Apple’s on-screen FPS overlay.") {
                    Toggle("Metal performance HUD", isOn: $model.hud).labelsHidden().toggleStyle(.switch).tint(Palette.green)
                }
                divider
                settingRow("Multithreaded rendering", "Experimental. Applies at next launch.") {
                    Toggle("Multithreaded rendering", isOn: $model.gfxJobs).labelsHidden().toggleStyle(.switch).tint(Palette.green)
                }
                divider
                // Tested 2026-09-29: 4 workers plus native graphics jobs made the screen flicker.
                settingRow("Job worker threads", model.jobWorkersClash ? "Flickers with Multithreaded rendering." :
                           "Fewer is often faster. Next launch.", warn: model.jobWorkersClash) {
                    Picker("Job worker threads", selection: $model.jobWorkers) {
                        Text("Auto").tag("auto")
                        ForEach(LauncherModel.jobWorkerChoices.dropFirst(), id: \.self) { Text($0).tag($0) }
                    }.labelsHidden().frame(width: 80).tint(Palette.gold)
                }
            }.disabled(model.busy || model.gameRunning)
            divider
            // Works mid-match, so it stays enabled while the game runs.
            settingRow("Fast mouse look", model.mouseDetail, warn: model.mouseWarning) {
                Toggle("Fast mouse look", isOn: $model.fastMouse).labelsHidden().toggleStyle(.switch).tint(Palette.green)
            }.disabled(model.busy)
            divider
            Group {
                settingRow("Game installation", model.version, warn: !model.buildSupported) {
                    Button(action: model.chooseGame) { ChunkLabel(sprite: Sprite.magnifier, title: "Locate", size: 10, pixel: 1.5) }
                        .buttonStyle(ChunkyButton(kind: .blue))
                }
                Text(model.gamePath).font(.system(size: 10, design: .monospaced)).foregroundColor(Palette.tertiary)
                    .lineLimit(2).truncationMode(.middle).textSelection(.enabled)
                DisclosureGroup(isExpanded: $advanced) {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(model.steamPath).font(.system(size: 10, design: .monospaced)).lineLimit(2).foregroundColor(Palette.tertiary)
                        Button(action: model.chooseSteam) { ChunkLabel(sprite: Sprite.magnifier, title: "Steam libraries", size: 10, pixel: 1.5) }
                            .buttonStyle(ChunkyButton(kind: .blue))
                    }.padding(.top, 8)
                } label: {
                    Text("ADVANCED").font(pgFont(10)).foregroundColor(Palette.secondary)
                }.tint(Palette.gold)
            }.disabled(model.busy || model.gameRunning)
        }
    }

    private var consoleCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                PixelSprite(rows: model.hasFailure ? Sprite.warning : Sprite.heart, pixel: 2)
                Text(model.message).font(.system(size: 12, weight: .semibold)).lineLimit(2)
                Spacer()
                Button(action: model.showLiveLog) { ChunkLabel(sprite: Sprite.scroll, title: "Session log", size: 10, pixel: 1.5) }
                    .buttonStyle(ChunkyButton(kind: .steel))
                Button(action: model.showLogs) { ChunkLabel(sprite: Sprite.chest, title: "Logs folder", size: 10, pixel: 1.5) }
                    .buttonStyle(ChunkyButton(kind: .steel))
            }
            ScrollView {
                Text(model.console).font(.system(size: 10, design: .monospaced)).foregroundColor(Palette.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading).textSelection(.enabled).padding(12)
            }
            .frame(minHeight: 72, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: 8).fill(Palette.inset))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(Palette.outline, lineWidth: 1.5))
        }
        .padding(16)
        .background(RoundedRectangle(cornerRadius: 14).fill(Palette.panel))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(model.hasFailure ? Palette.red : Palette.outline, lineWidth: 2))
    }

    private var divider: some View { Rectangle().fill(Palette.panelEdge).frame(height: 1) }

    private func settingRow<Control: View>(_ title: String, _ detail: String, warn: Bool = false,
                                           @ViewBuilder control: () -> Control) -> some View {
        HStack(alignment: .center) {
            VStack(alignment: .leading, spacing: 3) {
                Text(title.uppercased()).font(pgFont(12)).foregroundColor(.white)
                HStack(spacing: 4) {
                    if warn { PixelSprite(rows: Sprite.warning, pixel: 1) }
                    Text(detail).font(.system(size: 11, weight: .medium))
                }.foregroundColor(warn ? Palette.gold : Palette.secondary)
            }
            Spacer()
            control()
        }
    }

    private func metric(_ value: String, _ label: String, _ sprite: [String], small: Bool = false) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                PixelSprite(rows: sprite, pixel: 1.5)
                Text(label.uppercased()).font(pgFont(9)).tracking(1).foregroundColor(Palette.sky)
            }
            Group {
                if value == "—" || value == "Not active" {
                    Text(value).font(pgFont(small ? 18 : 28)).foregroundColor(Palette.tertiary)
                } else {
                    OutlinedText(text: value, size: small ? 18 : 30)
                }
            }
            .lineLimit(1).minimumScaleFactor(0.6)
            .frame(height: 38, alignment: .bottomLeading)
        }
        .padding(12).frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(Palette.inset.opacity(0.85)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Palette.outline, lineWidth: 2))
    }
}

@MainActor final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var model: LauncherModel!
    func applicationDidFinishLaunching(_ notification: Notification) {
        if let fonts = Bundle.main.resourceURL?.appendingPathComponent("Fonts/RussoOne-Regular.ttf") {
            CTFontManagerRegisterFontsForURL(fonts as CFURL, .process, nil)
        }
        model = LauncherModel()
        let menu = NSMenu()
        let appItem = NSMenuItem(); menu.addItem(appItem)
        let appMenu = NSMenu(); appItem.submenu = appMenu
        appMenu.addItem(withTitle: "About \(appName)", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit \(appName)", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        let editItem = NSMenuItem(); editItem.title = "Edit"; menu.addItem(editItem)
        let edit = NSMenu(title: "Edit"); editItem.submenu = edit
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        NSApp.mainMenu = menu
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1000, height: 980),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = appName
        window.titlebarAppearsTransparent = true
        window.backgroundColor = NSColor(calibratedRed: 0.047, green: 0.086, blue: 0.157, alpha: 1)
        window.titleVisibility = .hidden
        window.styleMask.insert(.fullSizeContentView)
        window.isMovableByWindowBackground = true
        window.appearance = NSAppearance(named: .darkAqua)
        window.contentMinSize = NSSize(width: 960, height: 940)
        window.contentView = NSHostingView(rootView: LauncherView(model: model))
        window.center(); window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        // Development aid: OPTIMIZER_SNAPSHOT=/path.png renders the window once and quits.
        if let snapshot = ProcessInfo.processInfo.environment["OPTIMIZER_SNAPSHOT"], let view = window.contentView {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                if let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) {
                    view.cacheDisplay(in: view.bounds, to: bitmap)
                    try? bitmap.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: snapshot))
                }
                NSApp.terminate(nil)
            }
        }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard model?.busy == true else { return .terminateNow }
        let alert = NSAlert()
        alert.messageText = "A launch check is still running"
        alert.informativeText = "Wait for it to finish before closing the launcher. Your game will remain open after the launcher quits."
        alert.addButton(withTitle: "Keep launcher open")
        alert.runModal()
        return .terminateCancel
    }
}

@main enum LauncherApplication {
    @MainActor static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.regular)
        let delegate = AppDelegate()
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }
}
