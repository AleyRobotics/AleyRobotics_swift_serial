//
//  sampleCode.swift
//  www.AleyRobotics.com
//
//  Created by Yuri Aleynikov on 08.05.2018.
//

import Foundation
import Dispatch

class SomeUartDevice: ARSocketDelegate {
    var serialPort: ARSerialPort!
    var portName = ""
    var isRuningLoop = false
    var maxBufferSize = 1024 //1K input UART buffer for parcing incomming data

    var serialQueue: DispatchQueue!
    
    var host = ""
    var port:UInt32 = 0
    var isSocketMode = false
    
    init(portName:String = "/dev/usb_uart_0") {
        self.portName = portName
        serialPort = ARSerialPort(path: portName)
    }
    
    init(host:String, port: UInt32) {
        isSocketMode = true
        self.portName = "\(host):\(port)"
        serialPort = ARSerialPort(path: self.portName)
        self.host = host
        self.port = port
        serialQueue = DispatchQueue(label: self.portName)
    }
    
    func start() -> Bool {
        if isRuningLoop == true {
            return true
        }
        
        var result = false
        isRuningLoop = true
        do {
            print("Attempting to open port: \(portName)")
            if isSocketMode == false {
                try serialPort.openPort()
                print("Serial port \(portName) opened successfully.")
                
                serialPort.setSettings(receiveRate: 230400, //SET PORT SPEED HERE
                                       transmitRate: 230400,
                                       minimumBytesToRead: 1)
            } else {
                //soket mode
                serialPort.openSocket(ipAddress: self.host, portNum: self.port)
                serialPort.socket?.isStartByte = false
                serialPort.socket?.stopBytes = [13,10,58,58,58] //array of stop bytes (end of uart message)
                serialPort.socket?.delegate = self
                return true
            }
            
            serialQueue = DispatchQueue(label: self.portName)
            serialQueue.async {
                self.backgroundRead()
            }
            result = true
        } catch PortError.failedToOpen {
            print("Serial port \(portName) failed to open. You might need root permissions.")
            isRuningLoop = false
            result = false
        } catch {
            print("Error: \(error)")
            result = false
        }
        
        
        return result
    }
    
    func backgroundRead() {
        if isSocketMode == true {
            return
        }
        var arrayData:[UInt8] = [UInt8]()
        
        do{
            let readData = try serialPort.readUntilBytes(stopBytes: [13,10,58,58,58], maxBytes: 64)
            print("\(readData)")
            arrayData.append(contentsOf: Array(readData))
            
            if arrayData.count > maxBufferSize {
                arrayData.removeAll()
            }
            processPacket(arrayData: &arrayData)
        } catch {
            print("Error: \(error)")
        }
        
        serialQueue.async {
            self.backgroundRead()
        }
    }
    
    func processPacket(arrayData: inout [UInt8]) {
        print("recieved arrayData.count: \(arrayData.count)")
        print("recieved arrayData: \(arrayData)")
    }
    
    func stop() {
        isRuningLoop = false
        print("Stoping thread \(self.portName)...")
    }
}

