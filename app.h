#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

#include "com.h"
#include "log.h"
#include "progress.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DEV_INFO_LEN (16)

typedef struct {
  UPDI_COM_port * port;
  UPDI_logger * logger;
  UPDI_progress * progress;
  bool NVM_Progmode;
  int8_t DEVICE_Id;
  uint8_t DEVICE_sigrow[DEV_INFO_LEN];
} UPDI_APP;

UPDI_APP * APP_Init(UPDI_logger * logger, UPDI_progress * progress);
bool APP_EnterProgmode(UPDI_APP *);
void APP_LeaveProgmode(UPDI_APP *);
bool APP_WaitFlashReady(UPDI_APP *);
bool APP_Unlock(UPDI_APP *);
bool APP_ChipErase(UPDI_APP *);
bool APP_ReadDataWords(UPDI_APP *,uint16_t address, uint8_t *data, uint16_t words);
bool APP_WriteData(UPDI_APP *,uint16_t address, uint8_t *data, uint16_t len);
bool APP_WriteNvm(UPDI_APP *,uint16_t address, uint8_t *data, uint16_t len, bool use_word_access);
UPDI_logger * APP_GetLog(UPDI_APP *);
void APP_Done(UPDI_APP *);

#ifdef __cplusplus
}
#endif

#endif
