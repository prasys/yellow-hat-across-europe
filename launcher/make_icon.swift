// make_icon.swift - draws the app icon (a yellow boater on a blue sky) and
// writes it as a 1024x1024 PNG:  swift launcher/make_icon.swift out.png
// The Makefile turns that into AppIcon.icns. The drawing is the project's own.

import AppKit

let size: CGFloat = 1024
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "icon.png"

let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size), pixelsHigh: Int(size), bitsPerSample: 8,
                           samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                           bytesPerRow: 0, bitsPerPixel: 0)!
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
let ctx = NSGraphicsContext.current!.cgContext
ctx.translateBy(x: 0, y: size)          // draw with the origin at the top left
ctx.scaleBy(x: 1, y: -1)

func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor { CGColor(red: r, green: g, blue: b, alpha: a) }
let ink = rgb(0.10, 0.12, 0.22), yellow = rgb(0.99, 0.82, 0.18), yellowDark = rgb(0.93, 0.66, 0.10)

// The tile: macOS's rounded square, inset as the system's icons are.
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tilePath = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: 14), blur: 28, color: rgb(0, 0, 0, 0.30))
ctx.addPath(tilePath)
ctx.setFillColor(rgb(0.20, 0.42, 0.78))
ctx.fillPath()
ctx.restoreGState()

ctx.saveGState()
ctx.addPath(tilePath)
ctx.clip()
let sky = CGGradient(colorsSpace: CGColorSpaceCreateDeviceRGB(),
                     colors: [rgb(0.36, 0.66, 0.95), rgb(0.13, 0.30, 0.66)] as CFArray, locations: [0, 1])!
ctx.drawLinearGradient(sky, start: CGPoint(x: 0, y: tile.minY), end: CGPoint(x: 0, y: tile.maxY), options: [])
// Rolling green country along the bottom, and a pale sun behind the hat.
ctx.setFillColor(rgb(1, 0.97, 0.80, 0.22))
ctx.fillEllipse(in: CGRect(x: 262, y: 200, width: 500, height: 500))
ctx.setFillColor(rgb(0.30, 0.62, 0.36))
ctx.fillEllipse(in: CGRect(x: -80, y: 760, width: 760, height: 380))
ctx.setFillColor(rgb(0.22, 0.52, 0.32))
ctx.fillEllipse(in: CGRect(x: 420, y: 790, width: 780, height: 360))
ctx.restoreGState()

// The hat, a little tilted.
ctx.saveGState()
ctx.translateBy(x: 512, y: 512)
ctx.rotate(by: -0.14)
ctx.translateBy(x: -512, y: -512)
ctx.setLineCap(.round)
ctx.setLineJoin(.round)

ctx.setStrokeColor(ink)                 // ribbon tails, behind the brim
ctx.setLineWidth(40)
ctx.move(to: CGPoint(x: 668, y: 560)); ctx.addQuadCurve(to: CGPoint(x: 800, y: 742), control: CGPoint(x: 770, y: 610))
ctx.move(to: CGPoint(x: 640, y: 575)); ctx.addQuadCurve(to: CGPoint(x: 704, y: 782), control: CGPoint(x: 700, y: 660))
ctx.strokePath()

ctx.setShadow(offset: CGSize(width: 0, height: 16), blur: 26, color: rgb(0, 0, 0, 0.28))
let brim = CGRect(x: 196, y: 500, width: 632, height: 176)
ctx.setFillColor(yellow)
ctx.fillEllipse(in: brim)
ctx.setShadow(offset: .zero, blur: 0, color: nil)
ctx.setFillColor(yellowDark)            // the brim's underside showing at the front
ctx.saveGState()
ctx.addEllipse(in: brim); ctx.clip()
ctx.fillEllipse(in: brim.offsetBy(dx: 0, dy: 60))
ctx.setFillColor(yellow)
ctx.fillEllipse(in: brim.insetBy(dx: 10, dy: 6).offsetBy(dx: 0, dy: -14))
ctx.restoreGState()
ctx.setStrokeColor(ink)
ctx.setLineWidth(22)
ctx.strokeEllipse(in: brim)

let crown = CGPath(roundedRect: CGRect(x: 356, y: 336, width: 312, height: 250), cornerWidth: 70, cornerHeight: 70, transform: nil)
ctx.addPath(crown); ctx.setFillColor(yellow); ctx.fillPath()
ctx.saveGState()                        // the band
ctx.addPath(crown); ctx.clip()
ctx.setFillColor(ink)
ctx.fill(CGRect(x: 340, y: 498, width: 344, height: 60))
ctx.setFillColor(rgb(1, 1, 1, 0.28))    // a highlight on the crown
ctx.fillEllipse(in: CGRect(x: 386, y: 352, width: 150, height: 70))
ctx.restoreGState()
ctx.addPath(crown); ctx.setStrokeColor(ink); ctx.setLineWidth(22); ctx.strokePath()

ctx.saveGState()                        // the front of the brim passes in front of the crown
ctx.clip(to: CGRect(x: 0, y: brim.midY, width: size, height: size))
ctx.setFillColor(yellow)
ctx.fillEllipse(in: brim)
ctx.saveGState()
ctx.addEllipse(in: brim); ctx.clip()
ctx.setFillColor(yellowDark)
ctx.fillEllipse(in: brim.offsetBy(dx: 0, dy: 60))
ctx.restoreGState()
ctx.setStrokeColor(ink)
ctx.setLineWidth(22)
ctx.strokeEllipse(in: brim)
ctx.restoreGState()
ctx.restoreGState()

NSGraphicsContext.current = nil
try rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: out))
