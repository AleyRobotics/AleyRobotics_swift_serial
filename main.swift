import Foundation
import Dispatch

#if os(Linux)
let uart = SomeUartDevice.init(portName: "/dev/usb_uart0")
#elseif os(OSX)
let uart = SomeUartDevice.init(host: "192.168.10.10", port: 20301)
#endif
if uart.start() == false {
    print("Error opening port \(uart.serialPort.path)")
}

while true {
     sleep(1)
}
