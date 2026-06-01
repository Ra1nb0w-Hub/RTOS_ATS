#include "sdk/ats_audio.h"
#include "sdk/ats_error.h"
#include "sdk/ats_fs.h"
#include "sdk/ats_lcd.h"
#include "sdk/ats_net.h"
#include "sdk/ats_printer.h"
#include "sdk/ats_sys.h"
#include "sdk/ats_reader.h"

#include "emv_lib/include/emv_api.h"

void ats_main(void)
{
    // {
    //     volatile uint32_t *p = (volatile uint32_t *)0xDEADDEAD;

    //     ats_log_printf("[ARM] about to trigger HardFault...");
    //     ats_thread_sleep(1000);
    //     *p = 0;
    // }
    
    // {
    //     int iRet = 0;
    //     EMVReaderInterface reader_if = {0};

    //     iRet = ats_reader_init();
    //     ats_log_printf("[ARM] ats_reader_init complete(%d)", iRet);

    //     reader_if.open = ats_reader_open;
    //     reader_if.close = ats_reader_close;
    //     reader_if.poll_card = ats_reader_poll;
    //     reader_if.cancel_io = ats_reader_cancel;
    //     reader_if.icc_power_on = ats_reader_icc_power_on;
    //     reader_if.icc_power_off = ats_reader_icc_power_off;
    //     reader_if.icc_transceive_apdu = ats_reader_icc_transceive_apdu;
    //     reader_if.picc_activate = ats_reader_picc_activate;
    //     reader_if.picc_deactivate = ats_reader_picc_deactivate;
    //     reader_if.picc_transceive_apdu = ats_reader_picc_transceive_apdu;
    //     reader_if.get_last_hw_error = ats_reader_get_last_hw_error;

    //     iRet = emv_terminal_set_reader(&reader_if);
    //     ats_log_printf("[ARM] emv_terminal_set_reader complete(%d)", iRet);
    // }

    while(1)
    {
        ats_log_printf("[ARM] ats_main is running, current tick: %u", ats_tick_get());
        ats_thread_sleep(1000);
    }
}