#define EHCI_H
#ifndef EHCI_H

void ehci_init(uint32_t bar0); //we need this for now 

extern struct ehci_controller ehci_ctrlr;

#endif
