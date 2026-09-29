import AppKit

/// Original pixel-art app icon: a sky with a cloud and a crosshair, and a pixel
/// pistol standing on grass and dirt blocks, inside a dark outlined tile. Drawn
/// on a 34x34 grid, rendered once at 1024 px and scaled down for smaller sizes.
/// Nothing here is traced from the game's art or logos.
let directory = CommandLine.arguments[1]
let grid = 34, pixel = 24, canvas = 1024
let margin = (canvas - grid * pixel) / 2

struct RGB { var r, g, b: CGFloat }
func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> RGB { RGB(r: r, g: g, b: b) }

let outline = rgb(0.05, 0.07, 0.12)
let sky = [rgb(0.11, 0.36, 0.80), rgb(0.16, 0.47, 0.91), rgb(0.24, 0.60, 0.98), rgb(0.38, 0.73, 1.0), rgb(0.58, 0.85, 1.0)]
let grassLight = rgb(0.58, 0.90, 0.30), grass = rgb(0.40, 0.78, 0.20), grassDark = rgb(0.26, 0.58, 0.12)
let dirt = rgb(0.55, 0.35, 0.19), dirtDark = rgb(0.42, 0.25, 0.12), dirtLight = rgb(0.67, 0.46, 0.26)
let seam = rgb(0.30, 0.18, 0.09), grassSeam = rgb(0.18, 0.42, 0.08)
let palette: [Character: RGB] = [
    "K": outline, "W": rgb(1, 1, 1), "L": rgb(0.82, 0.93, 1.0),
    "S": rgb(0.72, 0.77, 0.84), "D": rgb(0.42, 0.47, 0.55),
    "N": rgb(0.55, 0.33, 0.16), "M": rgb(0.74, 0.49, 0.25), "R": rgb(0.93, 0.22, 0.20)]

// One character per pixel, colors from `palette`; "." is transparent.
let pistol = [
    "....KK..........KKK...",
    "..KKKKKKKKKKKKKKKKKKKK",
    "..KWWWWWWWWWWWWWWWWWSK",
    "..KSSSSSSSSSSSSSSSSSSK",
    "..KSDSDSDSSSSSSSSSSSSK",
    "..KSDSDSDSSSSSSSSSSSSK",
    "..KDDDDDDDDDDDDDDDDDDK",
    "..KKKKKKKKKKKKKKKKKKKK",
    "..KNNNNNNNKSSSSSK.....",
    ".KNNMNNNNNK..D..K.....",
    ".KNMNNNNNNK.D...K.....",
    "KNMNNNNNNKKKKKKKK.....",
    "KNNNNNNNK.............",
    "KMNNNNNNK.............",
    "KNNNNNNNK.............",
    "KKKKKKKKK............."]
let crosshair = [
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
let bigCloud = [
    "...WW.....",
    ".WWWWW.WW.",
    "WWWWWWWWWW",
    ".LLLLLLLL."]
let smallCloud = [
    "..WWW..",
    ".WWWWWW",
    "LLLLLLL"]

// Rounded tile: the rows nearest the top and bottom are inset to cut corners.
let cornerInset = [5, 3, 2, 1]
func inTile(_ x: Int, _ y: Int) -> Bool {
    guard (0..<grid).contains(x), (0..<grid).contains(y) else { return false }
    let edge = min(y, grid - 1 - y)
    let cut = edge < cornerInset.count ? cornerInset[edge] : 0
    return x >= cut && x < grid - cut
}
func isOutline(_ x: Int, _ y: Int) -> Bool {
    inTile(x, y) && [(1, 0), (-1, 0), (0, 1), (0, -1)].contains { !inTile(x + $0.0, y + $0.1) }
}
func interior(_ x: Int, _ y: Int) -> Bool { inTile(x, y) && !isOutline(x, y) }

let horizon = 22                 // first grass row; dirt starts three rows lower
let blockSeams: Set = [11, 22]   // columns between the grass-and-dirt blocks
let tufts: Set = [11, 12, 17, 23, 27, 28, 32]
let drips: Set = [4, 14, 15, 19, 25, 30]

func scene(_ x: Int, _ y: Int) -> RGB {
    // Sky bands, lighter toward the ground.
    if y < horizon { return sky[min(sky.count - 1, (y - 1) * sky.count / (horizon - 1))] }
    if y < horizon + 3 {
        if blockSeams.contains(x) { return grassSeam }
        return [grassLight, grass, grassDark][y - horizon]
    }
    if blockSeams.contains(x) { return seam }
    if y == horizon + 3 && drips.contains(x) { return grassDark }
    // Scattered specks from a fixed hash, so every build draws the same icon.
    var h = UInt32(x) &* 374_761_393 &+ UInt32(y) &* 668_265_263
    h = (h ^ (h >> 13)) &* 1_274_126_177
    switch (h ^ (h >> 16)) % 9 {
    case 0: return dirtDark
    case 1: return dirtLight
    default: return dirt
    }
}

var cells = [[RGB?]](repeating: [RGB?](repeating: nil, count: grid), count: grid)
for y in 0..<grid {
    for x in 0..<grid where inTile(x, y) { cells[y][x] = isOutline(x, y) ? outline : scene(x, y) }
}
func stamp(_ rows: [String], _ ox: Int, _ oy: Int, as color: RGB? = nil) {
    for (dy, row) in rows.enumerated() {
        for (dx, character) in row.enumerated() where character != "." {
            let x = ox + dx, y = oy + dy
            guard interior(x, y) else { continue }
            cells[y][x] = color ?? palette[character]
        }
    }
}
for x in tufts where interior(x, horizon - 1) { cells[horizon - 1][x] = grassLight }
stamp(bigCloud, 2, 2)
stamp(smallCloud, 25, 16)
stamp(crosshair, 22, 1)
for x in 2...10 { cells[horizon][x] = grassDark }   // the pistol's shadow on the grass
stamp(pistol, 1, 6)

// Master image at 1024 px with whole-number pixel edges, so no seams or blur.
let master = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: canvas, pixelsHigh: canvas,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: master)
NSGraphicsContext.current?.shouldAntialias = false
func cellRect(_ x: Int, _ y: Int) -> NSRect {
    NSRect(x: margin + x * pixel, y: canvas - margin - (y + 1) * pixel, width: pixel, height: pixel)
}
// Soft drop shadow under the tile, like other macOS app icons.
NSGraphicsContext.saveGraphicsState()
let shadow = NSShadow()
shadow.shadowColor = NSColor(calibratedWhite: 0, alpha: 0.35)
shadow.shadowOffset = NSSize(width: 0, height: -10); shadow.shadowBlurRadius = 22
shadow.set()
let silhouette = NSBezierPath()
for y in 0..<grid { for x in 0..<grid where inTile(x, y) { silhouette.appendRect(cellRect(x, y)) } }
NSColor(calibratedRed: outline.r, green: outline.g, blue: outline.b, alpha: 1).setFill()
silhouette.fill()
NSGraphicsContext.restoreGraphicsState()
for y in 0..<grid {
    for x in 0..<grid {
        guard let color = cells[y][x] else { continue }
        NSColor(calibratedRed: color.r, green: color.g, blue: color.b, alpha: 1).setFill()
        cellRect(x, y).fill()
    }
}
NSGraphicsContext.restoreGraphicsState()
let image = NSImage(size: NSSize(width: canvas, height: canvas))
image.addRepresentation(master)

for (name, pixels) in [("icon_16x16",16),("icon_16x16@2x",32),("icon_32x32",32),("icon_32x32@2x",64),("icon_128x128",128),("icon_128x128@2x",256),("icon_256x256",256),("icon_256x256@2x",512),("icon_512x512",512),("icon_512x512@2x",1024)] {
    let bitmap: NSBitmapImageRep
    if pixels == canvas {
        bitmap = master
    } else {
        bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        NSGraphicsContext.current?.imageInterpolation = .high
        image.draw(in: NSRect(x: 0, y: 0, width: pixels, height: pixels))
        NSGraphicsContext.restoreGraphicsState()
    }
    try bitmap.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: directory + "/" + name + ".png"))
}
