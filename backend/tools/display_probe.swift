import AppKit
import CoreGraphics
import CoreVideo

for screen in NSScreen.screens {
    let id = (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as! NSNumber).uint32Value
    print("Display: \(screen.localizedName), id=\(id), maximumFramesPerSecond=\(screen.maximumFramesPerSecond)")
    if let mode = CGDisplayCopyDisplayMode(id) {
        print("Active mode: \(mode.width)x\(mode.height), pixels=\(mode.pixelWidth)x\(mode.pixelHeight), refreshRate=\(mode.refreshRate) Hz (0 can mean variable)")
    }
    var link: CVDisplayLink?
    if CVDisplayLinkCreateWithCGDisplay(id, &link) == kCVReturnSuccess, let link {
        let nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link)
        print("CoreVideo nominal period: \(nominal.timeValue)/\(nominal.timeScale), flags=\(nominal.flags)")
        print("CoreVideo actual period: \(CVDisplayLinkGetActualOutputVideoRefreshPeriod(link)) seconds")
    }
}
