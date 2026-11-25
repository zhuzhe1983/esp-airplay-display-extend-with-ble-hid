#!/usr/bin/env swift
// Move the existing Mac pointer once; no click, input capture or background service.
import CoreGraphics
import Foundation

let args=Array(CommandLine.arguments.dropFirst())
let targetSerial:UInt32
if args.isEmpty {targetSerial=2794200208} // observed serial for this AirPlay display UUID
else if args.count==2 && args[0]=="--serial",let value=UInt32(args[1]) {targetSerial=value}
else {fputs("Usage: swift tools/focus_display.swift [--serial NUMBER]\n",stderr);exit(2)}
var ids=[CGDirectDisplayID](repeating:0,count:32)
var count:UInt32=0
guard CGGetOnlineDisplayList(UInt32(ids.count),&ids,&count) == .success else {exit(1)}
let matches=ids.prefix(Int(count)).filter {CGDisplaySerialNumber($0)==targetSerial&&CGDisplayIsBuiltin($0)==0}
guard matches.count==1,let id=matches.first else {
    fputs("Connect the Claw4 AirPlay display first; the target must be unique.\n",stderr);exit(1)
}
let bounds=CGDisplayBounds(id)
guard bounds.width>0&&bounds.height>0 else {exit(1)}
let point=CGPoint(x:bounds.midX,y:bounds.midY)
let result=CGWarpMouseCursorPosition(point)
guard result == .success else {fputs("Cursor move failed: \(result.rawValue)\n",stderr);exit(1)}
print("Moved pointer to display \(id) center (\(point.x), \(point.y)).")
