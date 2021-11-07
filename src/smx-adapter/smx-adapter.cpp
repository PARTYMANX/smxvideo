#include <smx-adapter/smx-adapter.h>
#include <cstdio>

#include <SMX.h>

void stateChangeCallback (int pad, SMXUpdateCallbackReason reason, void *pUser) {
	printf("Device %i state changed: %04x\n", pad, SMX_GetInputState(pad));
}

void startSMX() {
	SMX_Start(stateChangeCallback, NULL);
}

void stopSMX() {
	SMX_Stop();
}

/*void getSMXInfo(int pad, SMXInfo *info) {
	SMX_GetInfo
}*/

void setSMXLights(const char *lightData, int lightDataSize) {
	SMX_SetLights2(lightData, lightDataSize);
}
