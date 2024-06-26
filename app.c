#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "devices.h"
#include "link.h"
#include "log.h"
#include "sleep.h"
#include "updi.h"
#include "msgs.h"

#ifdef __cplusplus
extern "C"
{
#endif

UPDI_APP * APP_Init(UPDI_logger * logger, UPDI_progress * progress) {
    UPDI_APP * res = (UPDI_APP *)malloc(sizeof(UPDI_APP));
    if (res) {
        memset(res, 0, sizeof(UPDI_APP));

        if (logger)
          res->logger = logger;
        else
          res->logger = global_LOG();

        res->progress = progress;
        res->DEVICE_Id = DEVICE_UNKNOWN_ID;

        return res;
    }
    return NULL;
}

void APP_Done(UPDI_APP * app) {
    if (app) {
        if (app->port) COM_Close(&(app->port));
        free(app);
    }
}

void APP_Reset(UPDI_APP * app, bool apply_reset)
{
  //Applies or releases an UPDI reset condition
  if (apply_reset == true)
  {
    LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_APPLY_RESET);
    LINK_stcs(app, UPDI_ASI_RESET_REQ, UPDI_RESET_REQ_VALUE);
  } else
  {
    LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_RELEASE_RESET);
    LINK_stcs(app, UPDI_ASI_RESET_REQ, 0x00);
  }
}

bool APP_InProgMode(UPDI_APP * app)
{
  //Checks whether the NVM PROG flag is up
  if (LINK_ldcs(app, UPDI_ASI_SYS_STATUS) & (1 << UPDI_ASI_SYS_STATUS_NVMPROG))
    return true;
  return false;
}

bool APP_WaitUnlocked(UPDI_APP * app, uint16_t timeout_ms)
{

  //Waits for the device to be unlocked.
  //All devices boot up as locked until proven otherwise

  while (timeout_ms-- > 0)
  {
    msleep(1);
    if (!(LINK_ldcs(app, UPDI_ASI_SYS_STATUS) & (1 << UPDI_ASI_SYS_STATUS_LOCKSTATUS)))
      return true;
  }

  LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_ON_UNLOCK);
  return false;
}

bool APP_EnterProgmode(UPDI_APP * app)
{
  //Enters into NVM programming mode
  uint8_t key_status;

  //First check if NVM is already enabled
  if (APP_InProgMode(app) == true)
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_ALREADY_PROG_MODE);
    return true;
  }

  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_ENTERING_PROG_MODE);

  // Put in the key
  LINK_SendKey(app, UPDI_KEY_NVM, UPDI_KEY_64);

  // Check key status
  key_status = LINK_ldcs(app, UPDI_ASI_KEY_STATUS);
  LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_KEY_STATUS, key_status);

  if (!(key_status & (1 << UPDI_ASI_KEY_STATUS_NVMPROG)))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_KEY_NOT_ACCEPTED);
    return false;
  }

  // Toggle reset
  APP_Reset(app,true);
  APP_Reset(app,false);

  // And wait for unlock
  if (!APP_WaitUnlocked(app, 100))
  {
    LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_FAIL_PROG_MODE_LOCKED);
    return false;
  }

  // Check for NVMPROG flag
  if (APP_InProgMode(app) == false)
  {
    LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_FAIL_PROG_MODE);
    return false;
  }

  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_IN_PROG_MODE);
  return true;
}

void APP_LeaveProgmode(UPDI_APP * app)
{
  //Disables UPDI which releases any keys enabled
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_LEAVING_PROG_MODE);
  APP_Reset(app, true);
  APP_Reset(app, false);
  LINK_stcs(app, UPDI_CS_CTRLB, (1 << UPDI_CTRLB_UPDIDIS_BIT) | (1 << UPDI_CTRLB_CCDETDIS_BIT));
}

bool APP_Unlock(UPDI_APP * app)
{
  //Unlock and erase
  uint8_t key_status;

  // Put in the key
  LINK_SendKey(app, UPDI_KEY_CHIPERASE, UPDI_KEY_64);

  // Check key status
  key_status = LINK_ldcs(app, UPDI_ASI_KEY_STATUS);
  LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_KEY_STATUS, key_status);

  if (!(key_status & (1 << UPDI_ASI_KEY_STATUS_CHIPERASE)))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_KEY_NOT_ACCEPTED);
    return false;
  }

  // Toggle reset
  APP_Reset(app,true);
  APP_Reset(app,false);

  // And wait for unlock
  if (!APP_WaitUnlocked(app, 100))
  {
    LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_FAILED_TO_ERASE);
    return false;
  }

  return true;
}

bool APP_WaitFlashReady(UPDI_APP * app)
{
  //Waits for the NVM controller to be ready
  uint8_t status;
  uint16_t timeout = 10000; // 10 sec timeout, just to be sure

  LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_WAIT_FLASH_READY);
  while (timeout-- > 0)
  {
    msleep(1);
    status = LINK_ld(app, DEVICES_GetNvmctrlAddress(app->DEVICE_Id) + UPDI_NVMCTRL_STATUS);
    if (status & (1 << UPDI_NVM_STATUS_WRITE_ERROR))
    {
      LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_NVM_ERROR);
      return false;
    }

    if (!(status & ((1 << UPDI_NVM_STATUS_EEPROM_BUSY) | (1 << UPDI_NVM_STATUS_FLASH_BUSY))))
      return true;
  }

  LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_ON_FLASH);

  return false;
}

bool APP_ExecuteNvmCommand(UPDI_APP * app, uint8_t command)
{
  //Executes an NVM COMMAND on the NVM CTRL
  //self.logger.info("NVMCMD {:d} executing".format(command))
  LOG_Print(app->logger, LOG_LEVEL_VERBOSE, MSG_NVMCMD_EXEC, command);
  return LINK_st(app, DEVICES_GetNvmctrlAddress(app->DEVICE_Id) + UPDI_NVMCTRL_CTRLA, command);
}

bool APP_ChipErase(UPDI_APP * app)
{
  // Does a chip erase using the NVM controller
  // Note that on locked devices this it not possible
  // and the ERASE KEY has to be used instead
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_ERASE_NVM_CTRL);

  // Wait until NVM CTRL is ready to erase
  if (!APP_WaitFlashReady(app))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_BEFORE_ERASE);
    return false;
  }

  // Erase
  APP_ExecuteNvmCommand(app, UPDI_NVMCTRL_CTRLA_CHIP_ERASE);

  // And wait for it
  if (!APP_WaitFlashReady(app))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_AFTER_ERASE);
    return false;
  }

  return true;
}

bool APP_WriteDataWords(UPDI_APP * app, uint16_t address, uint8_t *data, uint16_t len)
{
  //Writes a number of words to memory
  uint16_t value;

  // Special-case of 1 word
  if (len == 2)
  {
    value = (uint16_t)data[0] + (data[1] << 8);
    return LINK_st16(app, address, value);
  }

  // Range check
  if (len > (UPDI_MAX_REPEAT_SIZE + 1) << 1)
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_INVALID_LENGTH);
    return false;
  }

  // Store the address
  LINK_st_ptr(app, address);

  // Fire up the repeat
  LINK_Repeat(app, len >> 1);
  return LINK_st_ptr_inc16(app, data, len);
}

bool APP_WriteData(UPDI_APP * app, uint16_t address, uint8_t *data, uint16_t len)
{
  //Writes a number of bytes to memory

  // Special case of 1 byte
  if (len == 1)
    return LINK_st(app, address, data[0]);
  // Special case of 2 byte
  if (len == 2)
  {
    LINK_st(app, address, data[0]);
    return LINK_st(app, address + 1, data[1]);
  }

  // Range check
  if (len > (UPDI_MAX_REPEAT_SIZE + 1))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_INVALID_LENGTH);
    return false;
  }

  // Store the address
  LINK_st_ptr(app, address);

  // Fire up the repeat
  LINK_Repeat(app, len);
  return LINK_st_ptr_inc(app, data, len);
}

bool APP_WriteNvm(UPDI_APP * app, uint16_t address, uint8_t *data, uint16_t len, bool use_word_access)
{
  //Writes a page of data to NVM.APP_ExecuteNvmCommand
  //By default the PAGE_WRITE command is used, which
  //requires that the page is already erased.
  //By default word access is used (flash)

  // Check that NVM controller is ready
  if (!APP_WaitFlashReady(app))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_BEFORE_BUF_CLR);
    return false;
  }

  // Clear the page buffer
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_CLEAR_PAGE);
  APP_ExecuteNvmCommand(app, UPDI_NVMCTRL_CTRLA_PAGE_BUFFER_CLR);

  // Waif for NVM controller to be ready
  if (!APP_WaitFlashReady(app))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_AFTER_BUF_CLR);
    return false;
  }

  // Load the page buffer by writing directly to location
  if (use_word_access == true)
    APP_WriteDataWords(app, address, data, len);
  else
    APP_WriteData(app, address, data, len);

  // Write the page to NVM, maybe erase first
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_COMMIT_PAGE);
  APP_ExecuteNvmCommand(app, UPDI_NVMCTRL_CTRLA_WRITE_PAGE);

  // Wait for NVM controller to be ready again
  if (!APP_WaitFlashReady(app))
  {
    LOG_Print(app->logger, LOG_LEVEL_WARNING, MSG_TIMEOUT_AFTER_WRITE);
    return false;
  }

  return true;
}

bool APP_ReadData(UPDI_APP * app, uint16_t address, uint8_t *data, uint16_t size)
{
  //Reads a number of bytes of data from UPDI
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_READING_BYTES, size, address);

  // Range check
  if (size > UPDI_MAX_REPEAT_SIZE + 1)
  {
    LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_CANT_READ_IN_ONE);
    return false;
  }

  // Store the address
  LINK_st_ptr(app, address);

  // Fire up the repeat
  if (size > 1)
      LINK_Repeat(app, size);

  // Do the read(s)
  return LINK_ld_ptr_inc(app, data, size);
}

bool APP_ReadDataWords(UPDI_APP * app, uint16_t address, uint8_t *data, uint16_t words)
{
  //Reads a number of words of data from UPDI
  LOG_Print(app->logger, LOG_LEVEL_INFO, MSG_READING_WORDS, words, address);

  // Range check
  if (words > (UPDI_MAX_REPEAT_SIZE >> 1) + 1)
  {
    LOG_Print(app->logger, LOG_LEVEL_ERROR, MSG_CANT_READ_IN_ONE);
    return false;
  }

  // Store the address
  LINK_st_ptr(app, address);

  // Fire up the repeat
  if (words > 1)
  {
    LINK_Repeat(app, words);
  }

  // Do the read
  return LINK_ld_ptr_inc16(app, data, words);
}

#ifdef __cplusplus
}
#endif
