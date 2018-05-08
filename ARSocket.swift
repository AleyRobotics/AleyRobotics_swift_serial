//
//  ARSocket.swift
//  hexapod
//  www.AleyRobotics.com
//  Created by Aleynikov Yuri on 09.03.2018.
//

import Foundation
import CoreFoundation

#if os(Linux)
    import Glibc
#else
    import Darwin
#endif


protocol ARSocketDelegate: class {
    func processPacket(arrayData: inout [UInt8])
}

class ARSocket: NSObject, StreamDelegate {
    // read and write streams
    var inputStream: InputStream!
    var outputStream: OutputStream!
    var isOpen = false
    let maxReadLength = 4096
    var inputBytes = [UInt8]()
    
    //режим по одному старт-стоп байту (var isStartByte = true)
    var isStartByte = false
    var startByte: UInt8 = 0
    var stopByte: UInt8 = 0
    
    //режим по стоповым байтам  (var isStartByte = false)
    var stopBytes = [UInt8]()
    var packetLength = 1500
    
    weak var delegate: ARSocketDelegate?
    
    var host = ""
    var port:UInt32 = 0
    
    func setupNetworkCommunication(host: String, port: UInt32) {
        print("opening: \(host):\(port)")
        self.host = host
        self.port = port
        // 1
        var readStream: Unmanaged<CFReadStream>?
        var writeStream: Unmanaged<CFWriteStream>?
        
        // 2
        
        CFStreamCreatePairWithSocketToHost(kCFAllocatorDefault,
                                           host as! CFString,
                                           port,
                                           &readStream,
                                           &writeStream)
        
        let inputStream_ = readStream!.takeRetainedValue()
        let outputStream_ = writeStream!.takeRetainedValue()
        
        inputStream = inputStream_ as! InputStream
        outputStream = outputStream_ as! OutputStream
        
        inputStream.delegate = self
        outputStream.delegate = self
        
        inputStream.schedule(in: .main, forMode: .commonModes)
        outputStream.schedule(in: .main, forMode: .commonModes)
        
        inputStream.open()
        outputStream.open()
        
        isOpen = true
    }
    
    public func writeByte(byte: UInt8) -> Int {
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: 1)
        defer {
            buffer.deallocate(capacity: 1)
        }
        buffer[0] = byte
        let bytesWritten = outputStream.write(buffer, maxLength: 1)
        return bytesWritten
    }
    
    public func writeByteArray(into bytes: [UInt8]) -> Int {
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: bytes.count)
        defer {
            buffer.deallocate(capacity: bytes.count)
        }
        for i in 0...bytes.count - 1 {
            buffer[i] = bytes[i]
        }
        
        let bytesWritten = outputStream.write(buffer, maxLength: bytes.count)
        return bytesWritten
    }
    
    public func sendMessage(message: String)
    {
        let data = "msg:\(message)".data(using: .ascii)!
        
        _ = data.withUnsafeBytes { outputStream.write($0, maxLength: data.count) }
    }
    
    //MARK: - StreamDelegate

#if os(Linux)
    func stream(_ aStream: Stream, handleEvent eventCode: Stream.Event)
    {
        print("read")
        switch eventCode {
            case Stream.Event.hasBytesAvailable:
//                print("new message received: \(host):\(port)")
                readAvailableBytes(stream: aStream as! InputStream)
            case Stream.Event.endEncountered:
                print("Connection closed: \(host):\(port)")
            case Stream.Event.errorOccurred:
                print("error occurred: \(host):\(port)")
            case Stream.Event.hasSpaceAvailable:
                print("has space available: \(host):\(port)")
    
            default:
                print("some other event...")
            break
        }
    }
#elseif os(OSX)
    func stream(_ aStream: Stream, handle eventCode: Stream.Event)
    {
        switch eventCode {
        case Stream.Event.hasBytesAvailable:
//            print("Stream: new message received")
            readAvailableBytes(stream: aStream as! InputStream)
        case Stream.Event.endEncountered:
            print("Stream: Connection closed: \(host):\(port)")
        case Stream.Event.errorOccurred:
            print("Stream: error occurred: \(host):\(port) \(String(describing: aStream.streamError))")
//        case Stream.Event.hasSpaceAvailable:
//            print("Stream: has space available: \(host):\(port)")
        case Stream.Event.openCompleted:
            print("Stream: openCompleted: \(host):\(port)")
        default:
//            print("Stream: some other event...: \(host):\(port) \(String(describing: aStream.streamError))")
            break
        }
    }
#endif
    
    
    private func readAvailableBytes(stream: InputStream) {
        let buffer = UnsafeMutablePointer<UInt8>.allocate(capacity: maxReadLength)
        while stream.hasBytesAvailable {
            let numberOfBytesRead = inputStream.read(buffer, maxLength: maxReadLength)
            if numberOfBytesRead < 0 {
                if let _ = inputStream.streamError {
                    break
                }
            }
//            print("numberOfBytesRead: \(numberOfBytesRead)")
            if numberOfBytesRead > 0 {
                
                for i in 0...(numberOfBytesRead-1) {
                    inputBytes.append(buffer[i])
                }
                
                if isStartByte == true  {
                    var bytes = readBytes(startByte: startByte, stopByte: stopByte, packetLength: packetLength, buffer: inputBytes, maxBytes: maxReadLength)
                    if delegate != nil && bytes != nil{
                        delegate?.processPacket(arrayData: &bytes!)
                    }
                    inputBytes.removeAll()
                } else {
                    var bytes = readUntilBytes(stopBytes: stopBytes, maxBytes: maxReadLength, buffer: inputBytes)
                    if delegate != nil && bytes != nil {
                        delegate?.processPacket(arrayData: &bytes!)
                    }
                    inputBytes.removeAll()
    //                print("bytes?.count: \(bytes?.count) data:\(data.count)")
                }
                
                if inputBytes.count > maxReadLength {
                    print("soket input buffer overflow!")
                    inputBytes.removeAll()
                }
    //            if let message = processedMessageString(buffer: buffer, length: numberOfBytesRead) {
    //                delegate?.receivedMessage(message: message)
    //            }
    //
            }
        }
        buffer.deallocate(capacity: maxReadLength)
    }
    
    fileprivate var data: Array<UInt8> = [UInt8]()
    public func readBytes(startByte: UInt8, stopByte: UInt8, packetLength: Int, buffer: [UInt8], maxBytes: Int)-> [UInt8]? {
        if buffer.count > 0 {
            for i in 0 ... (buffer.count - 1) {
                data.append(buffer[i])
                if buffer[i] == stopByte {
                    if data.count >= packetLength {
                        if data[data.count - packetLength] == startByte {
                            let data_result: Array<UInt8> = Array(data[(data.count - packetLength) ... data.count - 1])
                            data.removeAll()
                            return data_result
                        }
                    }
                }
            }
        }
        if data.count >= maxBytes {
            let result = data
            data.removeAll()
            return result
        }
        return nil
    }
    
    public func readUntilBytes(stopBytes: [UInt8], maxBytes: Int, buffer: [UInt8]) -> [UInt8]? {
        var isStopFound = 0
        if buffer.count > 0 {
            for i in 0 ... (buffer.count - 1) {
                data.append(buffer[i])
                isStopFound = 0
                if data.count >= stopBytes.count {
                    if buffer[i] == stopBytes[stopBytes.count - 1] {
                        for index in (0..<stopBytes.count).reversed() {
                            
                            if stopBytes[stopBytes.count - index - 1 ] == data[data.count - index - 1] {
                                isStopFound = isStopFound + 1
                            }
                            
                            if isStopFound == stopBytes.count {
                                let result = data
//                                print("result: \(result.count), buffer:\(buffer.count)")
                                data.removeAll()
                                if i > 0 {
                                    if buffer.count > i + 1 {
                                        for ii in i ... (buffer.count - 2) {
                                            data.append(buffer[ii + 1])
                                        }
                                    }
                                }
//                                print("\(data.count), \(buffer.count) diff:\((buffer.count - data.count))")
                                return result
                            }
                        }
                    }
                }
                
                if data.count >= maxBytes {
                    let result = data
                    data.removeAll()
                    return result
                }
            }
        }
        return nil
    }
    
 
    
    
}
