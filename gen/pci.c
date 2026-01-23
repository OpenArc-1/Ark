/*Written by Yahya Mokhlis 1/23/2026*/
# include "ark/printk.h"
# include "pci.h"
# include "../io/built-in.h" // needed for otll to read and write through ports


// SEND TO ADDRESS PORT(OXCF8) AND READ FROM 0XCFC //
//  NOTE :  0XCF8 --->  ADDRESS PORT  && 0XCFC --> DATA PORT

u32 pciread(u8 bus , u8 slot , u8 func , u8 offset){

    u32 address =
        (1 << 31)    |
        (bus << 16)  |
        (slot << 11) |
        (func << 8)  |
        (offset & 0xFC);

    outl (0xCF8,address); // send adress to the 0xCF8;

    return inl(0xCFC); // READ IT THROUGH data port(0xCFC);
}

// loop through pci and finding the port
void scanAll(void){
    for (u8 bus = 0 ; bus < 30 ; bus++){
        for (u8 slot = 0 ;slot < 30 ; slot ++){
            for (u8 func = 0; func < 8 ; func++){
                u32 value =  pciread(bus,slot ,func,0x00);
                u16 vendor_id =  value & 0xFFFF;
                if (vendor_id ==0XFFFF){continue;}

                u16 device_id = (value >> 16) & 0xFFFF;

                printk("PCI: bus: %u  slot: %u  func:  %u  Device: %04x \n", bus ,slot , func ,device_id);


            }
        }
    }

}


