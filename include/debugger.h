#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdbool.h>
#include <stdint.h>

void debugger_init(bool start_paused); 
void debugger_update(void);
bool debugger_is_active(void);
void debugger_pause(void);

#endif // DEBUGGER_H