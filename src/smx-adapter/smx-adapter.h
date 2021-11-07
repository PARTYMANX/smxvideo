#ifndef SMX_ADAPTER_H
#define SMX_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

// adapter from c++ to c for smx sdk

void startSMX();
void stopSMX();
//void getSMXInfo(int pad, SMXInfo *info);
void setSMXLights(const char *lightData, int lightDataSize);

#ifdef __cplusplus
}
#endif

#endif