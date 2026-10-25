// nts3ctl: talk to a KORG NTS-3 kaoss pad kit over USB-MIDI (CoreMIDI), using
// the system exclusive messages in KORG's NTS-3 MIDI implementation
// (NTS-3_kaosspad_kit_MIDIimp.txt).
//
//   nts3ctl probe                  device, user API version, slot list
//   nts3ctl slot <n>               one user slot (with -v: raw messages)
//   nts3ctl load <file> <slot>     upload a .nts3unit into an empty user slot
//   nts3ctl clear <slot>           clear a user slot holding one of our units
//   nts3ctl fetch <slot> <prefix>  save the device's slot data messages
//   nts3ctl program                current program: the unit in each FX slot
//   nts3ctl cc <cc> <value> ...    send control changes (global channel)
//
// USER SLOT DATA goes as numbered messages, which the MIDI implementation
// (2024.09.03) does not show; this framing is copied from what firmware 1.04
// itself sends back for a slot (fetch): F0 42 3g 00 01 72 4A 07 <slot>
// <message index> <last index> <7-bit data> F7, at most 4096 bytes each,
// every message packed on its own. The data is TABLE 5: size, CRC (0 in the
// device's copy), then the unit file. load and clear only touch empty slots
// and slots holding our developer ID (30784D43) unless --force is given.
//
// Build: swiftc -O tools/nts3ctl.swift -o tools/nts3ctl

import CoreMIDI
import Foundation

// MARK: - CoreMIDI plumbing

func endpointName(_ e: MIDIEndpointRef) -> String {
  var s: Unmanaged<CFString>?
  MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s)
  return (s?.takeRetainedValue() as String?) ?? "?"
}

final class Device {
  var client = MIDIClientRef()
  var outPort = MIDIPortRef()
  var inPort = MIDIPortRef()
  var destinations: [MIDIEndpointRef] = []
  let lock = NSLock()
  var buffer: [UInt8] = []
  var messages: [[UInt8]] = []
  let arrived = DispatchSemaphore(value: 0)

  init() {
    MIDIClientCreateWithBlock("nts3ctl" as CFString, &client, nil)
    MIDIOutputPortCreate(client, "out" as CFString, &outPort)
    MIDIInputPortCreateWithBlock(client, "in" as CFString, &inPort) { [weak self] list, _ in
      self?.receive(list)
    }
    for i in 0..<MIDIGetNumberOfSources() {
      let s = MIDIGetSource(i)
      if endpointName(s).contains("NTS-3") { MIDIPortConnectSource(inPort, s, nil) }
    }
    for i in 0..<MIDIGetNumberOfDestinations() {
      let d = MIDIGetDestination(i)
      if endpointName(d).contains("NTS-3") { destinations.append(d) }
    }
  }

  private func receive(_ list: UnsafePointer<MIDIPacketList>) {
    var packet = list.pointee.packet
    for _ in 0..<list.pointee.numPackets {
      let bytes = withUnsafeBytes(of: packet.data) { Array($0.prefix(Int(packet.length))) }
      lock.lock()
      for b in bytes {
        if b >= 0xF8 { continue }  // real-time bytes (the NTS-3 sends clock) may sit inside SysEx
        if b == 0xF0 { buffer = [b] }
        else if !buffer.isEmpty {
          buffer.append(b)
          if b == 0xF7 {
            messages.append(buffer)
            buffer = []
            arrived.signal()
          }
        }
      }
      lock.unlock()
      packet = MIDIPacketNext(&packet).pointee
    }
  }

  // Sends bytes in chunks (64 by default, NTS3_CHUNK) per packet list, paced
  // NTS3_PAUSE_US apart on long messages; CoreMIDI reassembles a
  // long system exclusive message. The packet list gets its own buffer: a bare
  // MIDIPacketList struct only holds one small packet, and MIDIPacketListAdd
  // fails silently when the data does not fit.
  func send(_ bytes: [UInt8], to destination: MIDIEndpointRef) {
    let capacity = 1024
    let raw = UnsafeMutableRawPointer.allocate(byteCount: capacity, alignment: 8)
    defer { raw.deallocate() }
    let list = raw.bindMemory(to: MIDIPacketList.self, capacity: 1)
    let chunk = Int(ProcessInfo.processInfo.environment["NTS3_CHUNK"] ?? "64") ?? 64
    let pause = UInt32(ProcessInfo.processInfo.environment["NTS3_PAUSE_US"] ?? "3000") ?? 3000
    var offset = 0
    while offset < bytes.count {
      let n = min(chunk, bytes.count - offset)
      var packet = MIDIPacketListInit(list)
      let added = bytes[offset..<offset + n].withUnsafeBufferPointer { chunk -> Bool in
        packet = MIDIPacketListAdd(list, capacity, packet, 0, n, chunk.baseAddress!)
        return true
      }
      precondition(added, "MIDIPacketListAdd failed")
      MIDISend(outPort, destination, list)
      offset += n
      if bytes.count > 1024 { usleep(pause) }  // pace long dumps for the device's MIDI parser
    }
  }

  // Waits for the next system exclusive message matching `match`.
  func wait(timeout: Double, _ match: ([UInt8]) -> Bool) -> [UInt8]? {
    let deadline = Date().addingTimeInterval(timeout)
    while true {
      lock.lock()
      if let i = messages.firstIndex(where: match) {
        let m = messages.remove(at: i)
        lock.unlock()
        return m
      }
      lock.unlock()
      let left = deadline.timeIntervalSinceNow
      if left <= 0 || arrived.wait(timeout: .now() + left) == .timedOut { return nil }
    }
  }
}

// MARK: - KORG system exclusive helpers

// 7-bit data (MIDI implementation, NOTE 1): each group of up to 7 bytes comes
// as one byte of their top bits followed by the 7 low-bit bytes.
func pack7(_ data: ArraySlice<UInt8>) -> [UInt8] {
  var out: [UInt8] = []
  var i = data.startIndex
  while i < data.endIndex {
    let group = data[i..<min(i + 7, data.endIndex)]
    var msbs: UInt8 = 0
    for (k, b) in group.enumerated() where b & 0x80 != 0 { msbs |= 1 << UInt8(k) }
    out.append(msbs)
    out.append(contentsOf: group.map { $0 & 0x7F })
    i += 7
  }
  return out
}


func unpack7(_ data: ArraySlice<UInt8>) -> [UInt8] {
  var out: [UInt8] = []
  var i = data.startIndex
  while i < data.endIndex {
    let msbs = data[i]
    let group = data[(i + 1)..<min(i + 8, data.endIndex)]
    for (k, b) in group.enumerated() { out.append(b | ((msbs >> UInt8(k)) & 1) << 7) }
    i += 8
  }
  return out
}


func le32(_ b: [UInt8], _ o: Int) -> UInt32 {
  UInt32(b[o]) | UInt32(b[o + 1]) << 8 | UInt32(b[o + 2]) << 16 | UInt32(b[o + 3]) << 24
}

func ascii(_ b: ArraySlice<UInt8>) -> String {
  String(bytes: b.prefix { $0 != 0 }, encoding: .ascii) ?? "?"
}

let statusNames: [UInt8: String] = [
  0x23: "OPERATION COMPLETED", 0x24: "OPERATION ERROR", 0x26: "DATA FORMAT ERROR",
  0x27: "USER DATA SIZE ERROR", 0x28: "USER DATA CRC ERROR", 0x29: "USER TARGET ERROR",
  0x2A: "USER API ERROR", 0x2B: "USER LOAD SIZE ERROR", 0x2C: "USER MODULE ERROR",
  0x2D: "USER SLOT ERROR", 0x2E: "USER FORMAT ERROR", 0x2F: "USER INTERNAL ERROR",
]

let genericfx: UInt8 = 0x07

final class NTS3 {
  let device = Device()
  var destination = MIDIEndpointRef()
  var channel: UInt8 = 0

  // F0 42 3g 00 01 72 <function ...> F7
  func header() -> [UInt8] { [0xF0, 0x42, 0x30 | channel, 0x00, 0x01, 0x72] }
  func isReply(_ m: [UInt8], _ function: UInt8) -> Bool {
    m.count > 7 && m[1] == 0x42 && m[2] & 0xF0 == 0x30 && m[3] == 0 && m[4] == 1 && m[5] == 0x72 && m[6] == function
  }
  func isStatus(_ m: [UInt8]) -> Bool {
    m.count == 8 && m[1] == 0x42 && m[5] == 0x72 && (0x23...0x2F).contains(m[6])
  }

  // Finds the NTS-3 with a SEARCH DEVICE REQUEST on each NTS-3 destination.
  func connect() -> Bool {
    for d in device.destinations {
      let echo = UInt8.random(in: 1...0x7F)
      device.send([0xF0, 0x42, 0x50, 0x00, echo, 0xF7], to: d)
      if let r = device.wait(timeout: 1.0, { $0.count >= 14 && $0[1] == 0x42 && $0[2] == 0x50 && $0[3] == 0x01 && $0[5] == echo }) {
        destination = d
        channel = r[4] & 0x0F
        let minor = Int(r[10]) | Int(r[11]) << 7
        let major = Int(r[12]) | Int(r[13]) << 7
        print("NTS-3 found on \"\(endpointName(d))\", global channel \(channel + 1), firmware \(major).\(String(format: "%02d", minor))")
        return true
      }
    }
    print("no NTS-3 answered (destinations: \(device.destinations.map(endpointName)))")
    return false
  }

  var verbose = false

  func request(_ body: [UInt8], reply function: UInt8, timeout: Double = 2.0) -> [UInt8]? {
    device.send(header() + body + [0xF7], to: destination)
    let r = device.wait(timeout: timeout) { self.isReply($0, function) || self.isStatus($0) }
    if verbose {
      print("  > " + (header() + body + [0xF7]).map { String(format: "%02X", $0) }.joined(separator: " "))
      print("  < " + (r.map { $0.map { String(format: "%02X", $0) }.joined(separator: " ") } ?? "nothing"))
    }
    return r
  }

  func describeStatus(_ m: [UInt8]?) -> String {
    guard let m = m else { return "no reply" }
    if isStatus(m) { return statusNames[m[6]] ?? String(format: "status %02X", m[6]) }
    return String(format: "reply %02X", m[6])
  }

  struct Slot {
    let index: Int
    let developer: UInt32
    let unit: UInt32
    let version: String
    let name: String
    let api: String
    let params: [String]
  }

  // Firmware 1.04 answers a USER SLOT STATUS REQUEST with an extra byte after
  // the slot ID, then the unit's whole header (genericfx_unit_header_t, 376
  // bytes) in place of the MIDI implementation's TABLE 4.
  func slot(_ index: Int) -> Slot? {
    guard let r = request([0x19, genericfx, UInt8(index)], reply: 0x49), isReply(r, 0x49), r.count > 11 else { return nil }
    let d = unpack7(r[10..<(r.count - 1)])
    guard d.count >= 312, le32(d, 0) >= 312 else { return nil }
    let v = le32(d, 20), api = le32(d, 8)
    var params: [String] = []
    for p in 0..<Int(min(le32(d, 52), 8)) { params.append(ascii(d[(56 + p * 32 + 10)..<(56 + p * 32 + 32)])) }
    return Slot(index: index, developer: le32(d, 12), unit: le32(d, 16),
                version: "\(v >> 16).\((v >> 8) & 0xFF).\(v & 0xFF)",
                name: ascii(d[24..<44]),
                api: "\(api >> 16).\((api >> 8) & 0xFF).\(api & 0xFF)", params: params)
  }

  func probe() {
    if let r = request([0x17], reply: 0x47), isReply(r, 0x47) {
      print("user API: platform \(r[7]), version \(r[8]).\(r[9]).\(r[10])")
    }
    // Firmware 1.01 sends the module ID after 48h, which the MIDI
    // implementation's table leaves out.
    if let r = request([0x18, genericfx], reply: 0x48), isReply(r, 0x48), r.count > 9 {
      let d = unpack7(r[8..<(r.count - 1)])
      print("genericfx module info: " + d.map { String(format: "%02X", $0) }.joined(separator: " ") +
            (d.count >= 4 ? " (storage \(le32(d, 0)) bytes)" : ""))
    }
    var empty: [Int] = []
    for i in 0..<50 {
      if let s = slot(i) {
        print(String(format: "  slot %2d: %-16@ dev %08X unit %X v%@ api %@: %@", i, s.name as NSString, s.developer, s.unit, s.version as NSString, s.api as NSString, s.params.joined(separator: ", ") as NSString))
      } else {
        empty.append(i)
      }
    }
    print("empty slots: \(empty.isEmpty ? "none" : empty.map(String.init).joined(separator: " "))")
  }

  static let ourDeveloper: UInt32 = 0x30784D43
  // Data bytes per USER SLOT DATA message: 510 full 7-bit groups plus 3, which
  // makes 4096-byte messages, as the device sends them.
  static let bytesPerMessage = 3573

  func writable(_ index: Int, force: Bool) -> Bool {
    guard let current = slot(index) else { return true }
    if current.developer == NTS3.ourDeveloper || force { return true }
    print("slot \(index) holds \"\(current.name)\" (dev \(String(format: "%08X", current.developer))); use --force to replace it")
    return false
  }

  func load(_ path: String, slot index: Int, force: Bool) -> Bool {
    guard let file = FileManager.default.contents(atPath: path) else {
      print("cannot read \(path)"); return false
    }
    guard writable(index, force: force) else { return false }
    let payload = [UInt8](file)
    var data: [UInt8] = []
    for k in 0..<4 { data.append(UInt8((UInt32(payload.count) >> (8 * UInt32(k))) & 0xFF)) }
    data += [0, 0, 0, 0]  // CRC field: 0, as in the device's own copy
    data += payload
    let count = (data.count + NTS3.bytesPerMessage - 1) / NTS3.bytesPerMessage
    for m in 0..<count {
      let start = m * NTS3.bytesPerMessage
      let chunk = data[start..<min(start + NTS3.bytesPerMessage, data.count)]
      device.send(header() + [0x4A, genericfx, UInt8(index), UInt8(m), UInt8(count - 1)] + pack7(chunk) + [0xF7], to: destination)
      // A status before the last message means the device stopped on an error.
      let timeout = m == count - 1 ? 15.0 : 0.3
      if let status = device.wait(timeout: timeout, { self.isStatus($0) }) {
        if m < count - 1 || status[6] != 0x23 {
          print("load \((path as NSString).lastPathComponent) -> slot \(index): \(describeStatus(status)) after message \(m + 1)/\(count)")
          if status[6] != 0x23 { return false }
        } else {
          print("load \((path as NSString).lastPathComponent) -> slot \(index): \(describeStatus(status)) (\(payload.count) bytes, \(count) messages)")
          return true
        }
      } else if m == count - 1 {
        print("load \((path as NSString).lastPathComponent) -> slot \(index): no reply after the last message")
        return false
      }
    }
    return false
  }

  func clear(slot index: Int, force: Bool) -> Bool {
    guard writable(index, force: force) else { return false }
    let r = request([0x1B, genericfx, UInt8(index)], reply: 0x23)
    print("clear slot \(index): \(describeStatus(r))")
    return r.map { $0[6] == 0x23 } ?? false
  }

  func program() {
    guard let r = request([0x10], reply: 0x40), isReply(r, 0x40) else { print("no program dump"); return }
    let d = unpack7(r[7..<(r.count - 1)])
    guard d.count >= 500, ascii(d[0..<4]) == "PROG" else { print("unexpected program data (\(d.count) bytes)"); return }
    print("program \"\(ascii(d[12..<28]))\", routing \(d[29])")
    for fx in 0..<4 {
      let o = 32 + fx * 116
      let dev = le32(d, o), unit = le32(d, o + 4)
      let on = d[o + 32] & 1
      var params: [Int] = []
      for p in 0..<8 { let q = o + 52 + p * 8; params.append(Int(d[q]) << 8 | Int(d[q + 1])) }
      print(String(format: "  FX %d: %-20@ dev %08X unit %X %@ params %@", fx + 1, ascii(d[(o + 12)..<(o + 32)]) as NSString, dev, unit, (on == 1 ? "on" : "off") as NSString, params.map(String.init).joined(separator: " ") as NSString))
    }
  }

  func cc(_ pairs: [(UInt8, UInt8)]) {
    for (number, value) in pairs {
      device.send([0xB0 | channel, number, value], to: destination)
      usleep(2000)
    }
  }
}

// MARK: - main

let args = Array(CommandLine.arguments.dropFirst())
let force = args.contains("--force")
let positional = args.filter { $0 != "-v" && $0 != "--force" }
guard let command = positional.first else {
  print("usage: nts3ctl probe | slot <n> | load <file> <slot> [--force] | clear <slot> [--force] | fetch <slot> <prefix> | program | cc <cc> <value> ...")
  exit(2)
}
let nts3 = NTS3()
nts3.verbose = args.contains("-v")
guard nts3.connect() else { exit(1) }
switch command {
case "probe":
  nts3.probe()
case "load" where positional.count == 3:
  exit(nts3.load(positional[1], slot: Int(positional[2]) ?? -1, force: force) ? 0 : 1)
case "clear" where positional.count == 2:
  exit(nts3.clear(slot: Int(positional[1]) ?? -1, force: force) ? 0 : 1)
case "slot" where positional.count == 2:
  _ = nts3.slot(Int(positional[1]) ?? 0)
case "program":
  nts3.program()
case "fetch" where positional.count == 3:
  // Saves every USER SLOT DATA message the device sends for a slot.
  nts3.device.send(nts3.header() + [0x1A, genericfx, UInt8(positional[1]) ?? 0, 0xF7], to: nts3.destination)
  var n = 0
  while let r = nts3.device.wait(timeout: 3.0, { nts3.isReply($0, 0x4A) || nts3.isStatus($0) }) {
    print("message \(n): \(r.count) bytes, prefix " + r.prefix(14).map { String(format: "%02X", $0) }.joined(separator: " ") + " ... " + r.suffix(3).map { String(format: "%02X", $0) }.joined(separator: " "))
    FileManager.default.createFile(atPath: positional[2] + ".\(n).syx", contents: Data(r))
    n += 1
  }
  print("\(n) messages")
case "cc" where positional.count >= 3 && positional.count % 2 == 1:
  var pairs: [(UInt8, UInt8)] = []
  var i = 1
  while i + 1 < positional.count { pairs.append((UInt8(positional[i])!, UInt8(positional[i + 1])!)); i += 2 }
  nts3.cc(pairs)
default:
  print("bad arguments")
  exit(2)
}
