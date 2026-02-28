#pragma once
#include "ark/types.h"

void e1000_init(void);
void e1000_send(void *data, u16 len);
int e1000_recv(void *out);
