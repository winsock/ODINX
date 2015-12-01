//
//  ODINX.hpp
//  ODINX
//
//  Created by Andrew Querol on 11/9/15.
//  Copyright © 2015 Andrew Querol. All rights reserved.
//

#include <IOKit/usb/StandardUSB.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

#define MYNAME "ODINX"
#define V_PTR 0
#define V_DEBUG 1
#define V_NOTE 2
#define V_ERROR 3

#define DEBUGLEVEL V_DEBUG
#define LOG(verbosity, s, ...) do { if (verbosity >= DEBUGLEVEL) IOLog(MYNAME ": %s: " s "\n", __func__, ##__VA_ARGS__); } while(0)

#define USB_INTERFACE_CLASS_CDC_DATA 0x0A

enum class PacketType {
    READ,
    WRITE
};

typedef struct {
    bool inuse;
    IOBufferMemoryDescriptor *mdp;
    void *buf;
    IOUSBHostCompletion comp;
    PacketType direction;
} pipebuf_t;

class ODINXHostDevice : public IOUSBHostDevice {
    OSDeclareDefaultStructors(ODINXHostDevice);
private:
    IOUSBHostDevice *usbDevice = nullptr;
    
    IOUSBHostPipe *inPipe = nullptr;
    IOUSBHostPipe *outPipe = nullptr;
    
    pipebuf_t inbuf;
    pipebuf_t outbuf;
    
    volatile bool handshakeComplete = false;
public:
    virtual bool init(OSDictionary *properties) override;
    virtual bool start(IOService *provider) override;
    virtual bool attach(IOService * provider) override;
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    virtual void detach(IOService * provider) override;
    virtual void stop(IOService *provider) override;
    virtual void free() override;
    
    void ioComplete(void *obj, void *param, IOReturn rc, UInt32 remaining);
private:
    void handshake();
private:
    /// NOT THREAD SAFE, stores string in a static local to make string handling easier.
    static char *stringDescriptorToCString(const StandardUSB::StringDescriptor *stringDescriptor);
};