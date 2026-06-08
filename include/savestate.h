#ifndef SAVESTATE_H
#define SAVESTATE_H

#include <stdbool.h>

bool savestate_save(const char *filepath);
bool savestate_load(const char *filepath);

#endif // SAVESTATE_H