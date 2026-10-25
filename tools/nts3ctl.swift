// nts3ctl: talk to a KORG NTS-3 kaoss pad kit over USB-MIDI (CoreMIDI), using
// the system exclusive messages in KORG's NTS-3 MIDI implementation
// (NTS-3_kaosspad_kit_MIDIimp.txt).
//
//   nts3ctl probe                  device, user API version, slot list
//   nts3ctl slot <n>               one user slot (with -v: raw messages)
//   nts3ctl program                current program: the unit in each FX slot
//   nts3ctl cc <cc> <value> ...    send control changes (global channel)
//
// Read-only apart from cc. Units are loaded with KORG KONTROL Editor: the
// USER SLOT DATA upload in the MIDI implementation (2024.09.03) is rejected by
// firmware 1.01 with DATA FORMAT ERROR for real units.
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
  }

  func slot(_ index: Int) -> Slot? {
    guard let r = request([0x19, genericfx, UInt8(index)], reply: 0x49), isReply(r, 0x49) else { return nil }
    let d = unpack7(r[9..<(r.count - 1)])
    guard d.count >= 32 else { return nil }
    return Slot(index: index, developer: le32(d, 6), unit: le32(d, 10),
                version: "\(d[14]).\(d[16]).\(d[17])", name: ascii(d[18..<32]),
                api: "\(d[2]).\(d[4]).\(d[5])")
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
        print(String(format: "  slot %2d: %-16@ dev %08X unit %X v%@ (api %@)", i, s.name as NSString, s.developer, s.unit, s.version as NSString, s.api as NSString))
      } else {
        empty.append(i)
      }
    }
    print("empty slots: \(empty.isEmpty ? "none" : empty.map(String.init).joined(separator: " "))")
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
let positional = args.filter { $0 != "-v" }
guard let command = positional.first else {
  print("usage: nts3ctl probe | slot <n> | program | cc <cc> <value> ...")
  exit(2)
}
let nts3 = NTS3()
nts3.verbose = args.contains("-v")
guard nts3.connect() else { exit(1) }
switch command {
case "probe":
  nts3.probe()
case "slot" where positional.count == 2:
  _ = nts3.slot(Int(positional[1]) ?? 0)
case "program":
  nts3.program()
case "cc" where positional.count >= 3 && positional.count % 2 == 1:
  var pairs: [(UInt8, UInt8)] = []
  var i = 1
  while i + 1 < positional.count { pairs.append((UInt8(positional[i])!, UInt8(positional[i + 1])!)); i += 2 }
  nts3.cc(pairs)
default:
  print("bad arguments")
  exit(2)
}
