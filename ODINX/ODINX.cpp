//
//  ODINX.cpp
//  ODINX
//
//  Created by Andrew Querol on 11/9/15.
//  Copyright © 2015 Andrew Querol. All rights reserved.
//

#include "ODINX.hpp"

OSDefineMetaClassAndStructors(ODINXHostDevice, IOUSBHostDevice);

bool ODINXHostDevice::init(OSDictionary *properties) {
    LOG(V_NOTE, "SAMSUNG ODIN Mode Driver for Mac OS X");
    
    if (IOUSBHostDevice::init(properties) == false) {
        LOG(V_ERROR, "super class initialization failed!");
        return false;
    }
    inbuf.mdp = IOBufferMemoryDescriptor::withCapacity(PAGE_SIZE, kIODirectionIn);
    inbuf.buf = inbuf.mdp->getBytesNoCopy();
    inbuf.comp.owner = this;
    inbuf.comp.action = OSMemberFunctionCast(IOUSBHostCompletionAction, this, &ODINXHostDevice::ioComplete);
    inbuf.comp.parameter = &inbuf;
    
    outbuf.mdp = IOBufferMemoryDescriptor::withCapacity(PAGE_SIZE, kIODirectionOut);
    outbuf.buf = outbuf.mdp->getBytesNoCopy();
    outbuf.comp.owner = this;
    outbuf.comp.action = OSMemberFunctionCast(IOUSBHostCompletionAction, this, &ODINXHostDevice::ioComplete);
    outbuf.comp.parameter = &outbuf;

    return true;
}

void ODINXHostDevice::free() {
    OSSafeReleaseNULL(inbuf.mdp);
    OSSafeReleaseNULL(outbuf.mdp);
    IOUSBHostDevice::free();
}

bool ODINXHostDevice::attach(IOService *provider) {
    // We can do custom logic here if needed to expilicity not allow it to attach and fail matching, for now this is a stub
    LOG(V_NOTE, "ODINXHostDevice::attach called");
    return true;
}

/**
 * Provider = device that passed passive matching
 * Score = The current score that this driver has
 */
IOService *ODINXHostDevice::probe(IOService *provider, SInt32 *score) {
    // Is this a usb device provider? If yes this does nothing, but if not it'll return null causing the probe to fail.
    LOG(V_NOTE, "Probe Score:%d", *score);
    return OSDynamicCast(IOUSBHostDevice, provider) ? this : nullptr;
}

void ODINXHostDevice::detach(IOService *provider) {
    LOG(V_NOTE, "ODINXHostDevice::detach called");
    // We don't allocate anything in attach, we don't need to do anything
}

// We are the best match, setup driver
bool ODINXHostDevice::start(IOService *provider) {
    LOG(V_DEBUG, "ODINXHostDevice::start called");

    this->usbDevice = OSDynamicCast(IOUSBHostDevice, provider);
    if (!this->usbDevice) {
        
        LOG(V_ERROR, "Provider is not an IOUSBHostDevice, this is impossible!");
        // Tell the kernel to try the next highest score driver
        return false;
    }
    
    const StandardUSB::ConfigurationDescriptor *activeConfiguration = nullptr;
    IOReturn returnCode = NULL;
    
    if (!this->usbDevice->isOpen()) {
        returnCode = this->usbDevice->open(this);
        if (returnCode != kIOReturnSuccess && returnCode != kIOReturnExclusiveAccess) {
            /*
             * Only error if return code is neither kIOReturnSuccess or kIOReturnExclusiveAccess.
             * If we got kIOReturnExclusiveAccess that means we do (not?) have exclusivity and that open has already been called.
             */
            LOG(V_ERROR, "Error opening the USB device! Code: %04X", returnCode);
            return false;
        }
        LOG(V_DEBUG, "Device is not open and we failed to open it!");
    }
    
    // DEBUG
    {
        LOG(V_NOTE, "      Manufacturer: \"%s\"\n", ODINXHostDevice::stringDescriptorToCString(this->usbDevice->getStringDescriptor(this->usbDevice->getDeviceDescriptor()->iManufacturer)));
        LOG(V_NOTE, "           Product: \"%s\"\n", ODINXHostDevice::stringDescriptorToCString(this->usbDevice->getStringDescriptor(this->usbDevice->getDeviceDescriptor()->iProduct)));
        LOG(V_NOTE, "         Serial No: \"%s\"\n", ODINXHostDevice::stringDescriptorToCString(this->usbDevice->getStringDescriptor(this->usbDevice->getDeviceDescriptor()->iSerialNumber)));
        
        LOG(V_NOTE, "            length: %d\n", this->usbDevice->getDeviceDescriptor()->bLength);
        LOG(V_NOTE, "      device class: %d\n", this->usbDevice->getDeviceDescriptor()->bDeviceClass);
        LOG(V_NOTE, "               S/N: %d\n", this->usbDevice->getDeviceDescriptor()->iSerialNumber);
        LOG(V_NOTE, "           VID:PID: %04X:%04X\n", this->usbDevice->getDeviceDescriptor()->idVendor, this->usbDevice->getDeviceDescriptor()->idProduct);
        LOG(V_NOTE, "         bcdDevice: %04X\n", this->usbDevice->getDeviceDescriptor()->bcdDevice);
        LOG(V_NOTE, "   iMan:iProd:iSer: %d:%d:%d\n", this->usbDevice->getDeviceDescriptor()->iManufacturer, this->usbDevice->getDeviceDescriptor()->iProduct, this->usbDevice->getDeviceDescriptor()->iSerialNumber);
        LOG(V_NOTE, "          nb confs: %d\n", this->usbDevice->getDeviceDescriptor()->bNumConfigurations);
    }
    
    if (this->usbDevice->getDeviceDescriptor()->bNumConfigurations > 0 && returnCode != kIOReturnExclusiveAccess) {
        for (int configIndex = 0; configIndex < this->usbDevice->getDeviceDescriptor()->bNumConfigurations; configIndex++) {
            // Set the configuration active, the second param disables IOKit from automaticly trying to find drivers for the new interfaces since we do it ourselves.
            activeConfiguration = this->usbDevice->getConfigurationDescriptor(configIndex);
            returnCode = this->usbDevice->setConfiguration(activeConfiguration->bConfigurationValue, false);
            if (returnCode != kIOReturnSuccess) {
                LOG(V_ERROR, "Error setting the device to the configuration at index zero!");
            }
            
            // Find the interfaces
            OSIterator *deviceInterfaceIterator = this->usbDevice->getChildIterator(gIOServicePlane);
            if (!deviceInterfaceIterator) {
                LOG(V_ERROR, "USB Device has no interfaces?!?!");
                return false;
            }
            
            OSObject *entry = nullptr;
            
            while ((entry = deviceInterfaceIterator->getNextObject()) != nullptr) {
                IOUSBHostInterface *usbInterface = OSDynamicCast(IOUSBHostInterface, entry);
                if (!usbInterface) {
                    LOG(V_DEBUG, "Object in interface iterator that isn't a IOUSBHostInterface!");
                    continue;
                }
                
                uint8_t interfaceClass = usbInterface->getInterfaceDescriptor()->bInterfaceClass;
                if (interfaceClass != USB_INTERFACE_CLASS_CDC_DATA) {
                    LOG(V_DEBUG, "Interface isn't of the CDC Data class. Interface class: 0x%02X!", interfaceClass);
                    continue;
                }

                {
                    LOG(V_NOTE, "config[%d].interface[%d]: num endpoints = %d", configIndex, usbInterface->getInterfaceDescriptor()->bInterfaceNumber, usbInterface->getInterfaceDescriptor()->bNumEndpoints);
                    LOG(V_NOTE, "   Class.SubClass.Protocol: %02X.%02X.%02X", usbInterface->getInterfaceDescriptor()->bInterfaceClass, usbInterface->getInterfaceDescriptor()->bInterfaceSubClass, usbInterface->getInterfaceDescriptor()->bInterfaceProtocol);
                }
                
                const StandardUSB::EndpointDescriptor *endpoint = nullptr;
                while ((endpoint = StandardUSB::getNextEndpointDescriptor(activeConfiguration, usbInterface->getInterfaceDescriptor(), endpoint)) != nullptr) {
                    {
                        LOG(V_NOTE, "       endpoint address: %02X\n", endpoint->bEndpointAddress);
                        LOG(V_NOTE, "           max packet size: %04X\n", endpoint->wMaxPacketSize);
                        LOG(V_NOTE, "          polling interval: %02X\n", endpoint->bInterval);
                    }
                    
                    if (!inPipe || !outPipe) {
                        if (StandardUSB::getEndpointDirection(endpoint)) {
                            if (inPipe) {
                                OSSafeReleaseNULL(inPipe);
                            }
                            this->inPipe = usbInterface->copyPipe(endpoint->bEndpointAddress);
                            LOG(V_DEBUG, "Found an input endpoint: 0x%02X!", endpoint->bEndpointAddress);
                        } else {
                            if (outPipe) {
                                OSSafeReleaseNULL(outPipe);
                            }
                            this->outPipe = usbInterface->copyPipe(endpoint->bEndpointAddress);
                            LOG(V_DEBUG, "Found an output endpoint: 0x%02X!", endpoint->bEndpointAddress);
                        }
                    }
                }
            }
            OSSafeReleaseNULL(deviceInterfaceIterator);
        }
        
        if (!inPipe || !outPipe) {
            LOG(V_ERROR, "Error opening I/O pipes!");
            OSSafeReleaseNULL(inPipe);
            OSSafeReleaseNULL(outPipe);
            return false;
        }
    } else if (returnCode != kIOReturnExclusiveAccess) {
        LOG(V_ERROR, "The USB device somehow has no valid configurations?!?!");
        return false;
    }
    
    // Retain the device
    usbDevice->retain();
    
    // Kick off input
    inPipe->io(inbuf.mdp, PAGE_SIZE, &inbuf.comp);
    this->handshake();
    
    LOG(V_NOTE, "ODIN Mode device successfully started!");
    return true;
}

void ODINXHostDevice::handshake() {
    LOG(V_DEBUG, "ODINXHostDevice::handshake called");
    memcpy(outbuf.buf, (const void*)"ODIN", 4);
    outPipe->io(outbuf.mdp, 4, &outbuf.comp);
}

void ODINXHostDevice::ioComplete(void *obj, void *param, IOReturn rc, UInt32 remaining) {
    LOG(V_NOTE, "ODINXHostDevice::ioComplete called");

    pipebuf_t buffer = *(pipebuf_t *)param;
    size_t bytesRead = PAGE_SIZE - remaining;
    
    if (buffer.direction == PacketType::READ) {
        if (!this->handshakeComplete) {
            if (bytesRead >= 4 && memcmp(buffer.buf, "LOKE", 4)) {
                this->handshakeComplete = true;
                LOG(V_NOTE, "Successfully completed handshaking with the device!");
            }
            return;
        }
    }
}

void ODINXHostDevice::stop(IOService *provider) {
    LOG(V_DEBUG, "ODINXHostDevice::stop called");

    OSSafeReleaseNULL(this->inPipe);
    OSSafeReleaseNULL(this->outPipe);
    OSSafeReleaseNULL( this->usbDevice);
    
    // Something is telling us that we need to stop, forward to the Ethernet driver
    IOUSBHostDevice::stop(provider);
}

char *ODINXHostDevice::stringDescriptorToCString(const StandardUSB::StringDescriptor *stringDescriptor) {
    static char stringBuffer[256];
    size_t length = sizeof(stringBuffer) - 1; // Null termination
    stringDescriptorToUTF8(stringDescriptor, &stringBuffer[0], length);
    stringBuffer[length + 1] = '\0'; // Null termination
    return stringBuffer;
}