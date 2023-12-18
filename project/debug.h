#pragma once
#include <stdio.h>
#ifdef DEBUG
#define debug(msg, param...) {printf(msg "\n", param);};
#else
#define debug(msg, param...)
#endif