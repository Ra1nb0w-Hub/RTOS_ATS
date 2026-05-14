#include "sdk/ats_audio.h"
#include "sdk/ats_error.h"
#include "sdk/ats_fs.h"
#include "sdk/ats_lcd.h"
#include "sdk/ats_net.h"
#include "sdk/ats_printer.h"
#include "sdk/ats_sys.h"

void ats_main(void)
{
    ats_log_printf(ATS_LOG_LEVEL_INFO, "[Demo] ats_main is running...");

    while(1)
        ats_thread_sleep(1000);
}