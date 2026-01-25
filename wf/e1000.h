#pragma once

void e1000_init(void);
void e1000_send(void *data, uint16_t len);
int e1000_recv(void *out);
