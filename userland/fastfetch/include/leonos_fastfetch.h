#pragma once

#include "fastfetch.h"

void ffLeonOSInit(void);
void ffLeonOSDestroy(void);
void ffLeonOSPrintStatic(const char* key, const char* value);
const char* ffLeonOSCPUName(void);
void ffLeonOSPrintCPU(void);
bool ffLeonOSLogoPrint(void);
void ffLeonOSLogoList(void);
void ffLeonOSLogoPrintAll(void);
void ffLeonOSLogoDestroy(void);
