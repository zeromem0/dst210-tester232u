// See UsbFix.cpp: the core's USB serial, made to work on the CH32F103.
// Call usbFix() before SerialUSB.begin().

#ifndef USBFIX_H
#define USBFIX_H

void usbFix();

// Whether the PC has the port open (DTR). The core's `(bool)SerialUSB` says the
// same after a delay(10) of its own, which a check in every turn or before
// every packet cannot afford: it held the sender to 6 kB/s.
bool usbOpen();

// What the PC sent, in place of SerialUSB.available() and read(), which see
// nothing here: the packets are taken in the interrupt (UsbFix.cpp).
int usbAvailable();
int usbRead();              // -1 if nothing

// Called every turn of the main loop. A control request from the PC that the
// chip leaves unanswered (seen when the port is opened right after the
// instrument appears on USB) would leave the PC's open waiting for ever; if
// one is left for more than half a second, the instrument goes off USB and
// comes back, so that the PC gives up that open and the next one works.
void usbWatch();
uint16_t usbRestarts();     // how many times that has happened since power on

#endif
