#include "ats_audio.h"
#include "ats_error.h"
#include "ats_fs.h"
#include "ats_lcd.h"
#include "ats_net.h"
#include "ats_printer.h"
#include "ats_sys.h"

void ats_main(void)
{
    ats_log_printf(ATS_LOG_LEVEL_INFO, "ats_main is running...");

    while(1)
        ats_thread_sleep(1000);
}