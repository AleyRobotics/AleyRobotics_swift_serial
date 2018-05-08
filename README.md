#AleyRobotics swift 3.0 serial for Raspberry Pi and external UART devices.

Simple code useful for working with UART ports at Raspberry Pi with  Apple swift language.
Easy debuging UART protocols with remote socket connection at Mac throught Sockets and ip2Ser linux App.


For remote connect use little modified ip2Ser inside repository. Added more avalible UART speeds constants.
```
./ip2ser/ip2ser -d /dev/usb_uart_port_0 -b 230400 -p 20301 -R
```


Swift 3.0 init code for linux real UART or remote throught sokets at OSx.
```
#if os(Linux)
let uart = SomeUartDevice.init(portName: "/dev/usb_uart0")
#elseif os(OSX)
let uart = SomeUartDevice.init(host: "192.168.10.10", port: 20301)
#endif
if uart.start() == false {
    print("Error opening port \(uart.serialPort.path)")
}
```

Code for working with UART 
```
import Foundation
import Dispatch

class SomeUartDevice: ARSocketDelegate {
    var serialPort: ARSerialPort!
    .......
    func start() -> Bool //Start reading thread
    func stop() // stop thread
    
    func processPacket(arrayData: inout [UInt8]) {
        //do what u want with recieved data
        print("recieved arrayData.count: \(arrayData.count)")
        print("recieved arrayData: \(arrayData)")
        
    }
    
    func createPacket() {
        packetToSend.removeAll()
        //add bytes for sending here
        packetToSend.append(0x55)
        packetToSend.append(0xAA)
    }
    
    func sendPacket() {
        createPacket()
        do {
            _ = try serialPort.writeByteArray(into: self.packetToSend)
        } catch {
            print("Error: \(error)")
        }
    }

