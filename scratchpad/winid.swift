import CoreGraphics
import Foundation
let want = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ""
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in list {
    let name = (w[kCGWindowOwnerName as String] as? String) ?? ""
    let p = (w[kCGWindowOwnerPID as String] as? Int) ?? 0
    let id = (w[kCGWindowNumber as String] as? Int) ?? 0
    let b = (w[kCGWindowBounds as String] as? [String: Any]) ?? [:]
    let wd = (b["Width"] as? Int) ?? 0
    if want == "" || name.lowercased().contains(want) || String(p) == want {
        if true { print("\(id) pid=\(p) \(name) w=\(wd)") }
    }
}
