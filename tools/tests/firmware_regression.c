/* USER CODE BEGIN Firmware_Regression */
#include "mock_platform.h"
#include "firmware_diag.h"
#include <stdio.h>

uint32_t test_tick=1000, test_primask, test_dma_ndtr=512;
unsigned test_mutex_depth;
UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;
CAN_HandleTypeDef hcan1={CAN1},hcan2={CAN2};
volatile firmware_diag_t g_firmware_diag;
void Firmware_Diag_RegisterTask(firmware_task_id_t id,void *h,const char *n,uint32_t size) {assert(h&&n&&size);(void)id;}
void Firmware_Diag_LvglAssert(void) {fprintf(stderr,"LVGL allocation/assertion failed\n");abort();}
void LED_Diag_SetBootStage(uint8_t stage) {(void)stage;}
void Error_Handler(void) {abort();}
int HAL_UART_Transmit(UART_HandleTypeDef *h,uint8_t *b,uint16_t n,uint32_t timeout) {(void)h;(void)b;(void)n;(void)timeout;return HAL_OK;}

static CAN_RxHeaderTypeDef test_rx_header;
static uint8_t test_rx_data[8];
static unsigned test_rx_pending,test_tx_count,test_can_speed_tenths;
static unsigned test_tx_id_count[0x800];
static uint8_t test_tx_id_data[0x800][8];
static CAN_TxHeaderTypeDef test_tx_header;
static uint8_t test_tx_data[8];
int HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h,CAN_FilterTypeDef *f) {(void)h;(void)f;return HAL_OK;}
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h,uint32_t fifo) {(void)h;(void)fifo;return test_rx_pending;}
int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h,uint32_t fifo,CAN_RxHeaderTypeDef *rx,uint8_t *data) {(void)h;(void)fifo;assert(test_rx_pending);test_rx_pending--;*rx=test_rx_header;memcpy(data,test_rx_data,8);return HAL_OK;}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) {(void)h;return 1;}
int HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h,CAN_TxHeaderTypeDef *tx,uint8_t *data,uint32_t *mail) {(void)h;*mail=0;test_tx_count++;test_tx_header=*tx;memcpy(test_tx_data,data,8);if(tx->StdId<0x800){test_tx_id_count[tx->StdId]++;memcpy(test_tx_id_data[tx->StdId],data,8);}if(tx->StdId==0x301)test_can_speed_tenths=data[0]|((unsigned)data[1]<<8);return HAL_OK;}

/* Internal flash sector 7 with NOR semantics: programming only clears bits and
 * erase sets every bit. Faults are injected per call. */
#define ODO_TEST_BASE 0x08060000UL
#define ODO_TEST_WORDS (0x20000U/4U)
static uint32_t flash_words[ODO_TEST_WORDS];
static unsigned flash_unlocked,flash_erase_calls,flash_program_calls;
static int flash_tear_call=-1,flash_fail_programs,flash_ignore_programs,flash_fail_erase;
static uint32_t flash_read(uint32_t address)
{
    assert(address>=ODO_TEST_BASE&&address<ODO_TEST_BASE+ODO_TEST_WORDS*4U&&(address&3U)==0U);
    return flash_words[(address-ODO_TEST_BASE)/4U];
}
#define ODO_FLASH_READ32(address) flash_read(address)
#define ODO_FLUSH_DATA_CACHE() ((void)0)
HAL_StatusTypeDef HAL_FLASH_Unlock(void) {assert(!flash_unlocked);flash_unlocked=1;return HAL_OK;}
HAL_StatusTypeDef HAL_FLASH_Lock(void) {assert(flash_unlocked);flash_unlocked=0;return HAL_OK;}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,uint32_t address,uint64_t data)
{
    assert(flash_unlocked&&type==FLASH_TYPEPROGRAM_WORD&&data<=UINT32_MAX);
    unsigned call=flash_program_calls++;
    (void)flash_read(address);                                  /* range and alignment check */
    if(flash_fail_programs)return HAL_ERROR;
    if(flash_ignore_programs)return HAL_OK;
    uint32_t *word=&flash_words[(address-ODO_TEST_BASE)/4U];
    if((int)call==flash_tear_call) {*word&=(uint32_t)data|0xFFFF0000U;return HAL_ERROR;} /* power lost mid-word */
    *word&=(uint32_t)data;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *init,uint32_t *error)
{
    assert(flash_unlocked&&init->TypeErase==FLASH_TYPEERASE_SECTORS&&init->Sector==FLASH_SECTOR_7);
    assert(init->NbSectors==1&&init->VoltageRange==FLASH_VOLTAGE_RANGE_3);
    flash_erase_calls++;
    if(flash_fail_erase) {*error=7;return HAL_ERROR;}
    memset(flash_words,0xFF,sizeof(flash_words));*error=0xFFFFFFFFU;return HAL_OK;
}

/* Compile actual application code; only the Cube-generated CAN init/MSP
 * sections are omitted by run_firmware_tests.py. Hardware/RTOS are mocks. */
#include "can_app.inc"
#include "../../stm32_port/Core/Src/odometer_flash.c"
#include "../../stm32_port/Core/Src/gps_lap.c"
#include "../../stm32_port/Core/Src/gps.c"
#include "../../stm32_port/Core/lvgl/lvgl.h"
#ifndef DASHBOARD_UI_SOURCE
#define DASHBOARD_UI_SOURCE "../../stm32_port/Core/Src/dashboard_ui.c"
#endif
#include DASHBOARD_UI_SOURCE

static uint16_t frame_pixels[800*480],draw_pixels[800*10];
static unsigned flushes;
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data)
{
    unsigned width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;y++) {
        memcpy(frame_pixels+y*800+area->x1,data,width*2);data+=width*2;
    }
    flushes++;
    lv_display_flush_ready(display);
}
static void init_ui(void)
{
    GPS_Lap_InitLock();
    lv_init();lv_tick_set_cb(HAL_GetTick);
    lv_display_t *display=lv_display_create(800,480);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_pixels,NULL,sizeof(draw_pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    Dashboard_UI_Init();lv_refr_now(display);
}
static void service_can(void)
{
    User_CAN_RxItem_t frame;
    while(xQueueReceive(g_can_rx_queue,&frame,0)==pdPASS) {
        if(frame.fifo==0)User_CAN_ProcessFifo0(&frame);else User_CAN_ProcessFifo1(&frame);
    }
}
static void receive(uint16_t id,unsigned dlc,unsigned count)
{
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=id,.DLC=dlc};
    test_rx_pending=count;HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
}
static void test_can(void)
{
    /* Hardware FIFO work is bounded, and software queue overflow is counted. */
    receive(0x305,6,4);assert(test_rx_pending==1);assert(g_can_rx_queue->count==3);service_can();
    for(unsigned i=0;i<65;i++)receive(0x305,6,1);
    assert(g_can_rx_queue->count==64&&g_can_rx_drop_count==1&&g_can_rx_queue_peak==64);service_can();
    unsigned invalid=g_can_rx_invalid_count;
    receive(0x4B0,2,1);service_can();assert(g_can_rx_invalid_count==invalid+1);
    memset(test_rx_data,0,8);test_rx_data[2]=0xF4;test_rx_data[3]=1;
    receive(0x305,6,1);service_can();assert(g_can_dashboard_data.aps_open_pct==50);
    test_rx_data[0]=0x10;receive(0x509,5,1);service_can();assert(g_can_dashboard_data.mode_index==1);
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=0x700,.DLC=1};test_rx_data[0]=7;test_rx_pending=1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);service_can();
    assert(test_tx_header.StdId==0x703&&test_tx_header.DLC==2&&test_tx_data[0]==7);
    test_rx_header.RTR=1;test_rx_pending=1;invalid=g_can_rx_invalid_count;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);assert(g_can_rx_invalid_count==invalid+1);
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=0x50,.DLC=8};
    memset(test_rx_data,0,8);test_rx_data[0]=0x55;test_rx_data[1]=0x51;test_rx_data[2]=0x34;test_rx_data[3]=0x12;
    test_rx_pending=1;HAL_CAN_RxFifo1MsgPendingCallback(&hcan1);service_can();
    assert(test_tx_header.StdId==0x61&&g_can_dashboard_data.imu_accel[0]==0x1234);
    test_rx_header.DLC=2;test_rx_pending=1;invalid=g_can_rx_invalid_count;
    HAL_CAN_RxFifo1MsgPendingCallback(&hcan1);service_can();assert(g_can_rx_invalid_count==invalid+1);
    puts("CAN: bounded FIFO, queue saturation, short/remote frames, valid DLCs and steering relay passed");
}
static void flash_blank(void) {memset(flash_words,0xFF,sizeof(flash_words));}
static uint32_t odo_boot(void)
{
    uint32_t total=0xDEADBEEFU;
    Odometer_Flash_Init();assert(Odometer_Flash_GetInitial(&total)&&!flash_unlocked);
    return total;
}
/* Slot index of the newest valid record; ODO_SLOT_COUNT when there is none. */
static uint32_t odo_last_valid_slot(void)
{
    uint32_t last=ODO_SLOT_COUNT,total;
    for(uint32_t slot=0;slot<ODO_SLOT_COUNT;slot++)if(odo_read_slot(slot,&total)==ODO_SLOT_VALID)last=slot;
    return last;
}
static void test_odometer(void)
{
    uint32_t total=777;
    /* Nothing is read or written before Odometer_Flash_Init(). */
    flash_blank();
    assert(!Odometer_Flash_GetInitial(&total)&&total==777&&!Odometer_Flash_Save(5));
    assert(g_odometer_flash_save_fail_count==1&&flash_program_calls==0);

    /* Blank sector: starts at 0 without erasing; records survive a reboot. */
    assert(odo_boot()==0&&flash_erase_calls==0);
    for(uint32_t m=100;m<=500;m+=100)assert(Odometer_Flash_Save(m));
    assert(odo_boot()==500&&odo_last_valid_slot()==4&&flash_erase_calls==0);
    assert(Odometer_Flash_Save(600)&&odo_boot()==600&&odo_last_valid_slot()==5);
    assert(!flash_unlocked&&g_odometer_flash_save_fail_count==1);

    /* A failed write consumes the slot and the retry lands in the next one. */
    flash_program_calls=0;
    assert(Odometer_Flash_Save(700)&&odo_boot()==700);
    /* Power lost halfway through the check word leaves a damaged slot. */
    unsigned fails=g_odometer_flash_save_fail_count;
    flash_tear_call=flash_program_calls+1;
    uint32_t torn=odo_last_valid_slot()+1;
    assert(Odometer_Flash_Save(800)&&odo_read_slot(torn,&total)==ODO_SLOT_DAMAGED);
    flash_tear_call=-1;
    assert(odo_last_valid_slot()==torn+1&&odo_boot()==800&&g_odometer_flash_save_fail_count==fails);
    assert(Odometer_Flash_Save(900)&&odo_last_valid_slot()==torn+2&&odo_boot()==900);
    /* Both attempts fail: report failure, keep the old record, never erase. */
    flash_fail_programs=1;fails=g_odometer_flash_save_fail_count;
    assert(!Odometer_Flash_Save(1000)&&g_odometer_flash_save_fail_count==fails+1);
    flash_fail_programs=0;
    assert(odo_boot()==900&&!flash_unlocked&&flash_erase_calls==0);
    /* HAL reporting success without the data reaching flash is also a failure. */
    flash_ignore_programs=1;
    assert(!Odometer_Flash_Save(1100)&&g_odometer_flash_save_fail_count==fails+2);
    flash_ignore_programs=0;
    assert(odo_boot()==900&&Odometer_Flash_Save(1200)&&odo_boot()==1200);

    /* Leftovers without a valid record (older firmware image, interrupted erase)
     * are erased once; a damaged-only sector counts as empty. */
    for(unsigned i=0;i<ODO_TEST_WORDS;i++)flash_words[i]=0x9E3779B1U*(i+1U);
    unsigned erases=flash_erase_calls;
    assert(odo_boot()==0&&flash_erase_calls==erases+1);
    assert(Odometer_Flash_Save(50)&&odo_boot()==50&&flash_erase_calls==erases+1);
    flash_blank();flash_words[0]=0x12345678U;
    assert(odo_boot()==0&&flash_erase_calls==erases+2&&odo_last_valid_slot()==ODO_SLOT_COUNT);

    /* Full sector: saving stops (no erase while driving), the next boot erases
     * and carries the newest total into slot 0. */
    flash_blank();assert(odo_boot()==0);
    erases=flash_erase_calls;
    for(uint32_t slot=0;slot<ODO_SLOT_COUNT;slot++)assert(Odometer_Flash_Save(1000+slot));
    fails=g_odometer_flash_save_fail_count;
    assert(!Odometer_Flash_Save(999999)&&g_odometer_flash_save_fail_count==fails+1&&flash_erase_calls==erases);
    assert(odo_last_valid_slot()==ODO_SLOT_COUNT-1);
    flash_fail_erase=1;
    assert(odo_boot()==1000+ODO_SLOT_COUNT-1&&flash_erase_calls==erases+1&&odo_last_valid_slot()==ODO_SLOT_COUNT-1);
    assert(!Odometer_Flash_Save(5)&&!flash_unlocked);          /* erase failed: read-only, old data intact */
    flash_fail_erase=0;
    assert(odo_boot()==1000+ODO_SLOT_COUNT-1&&flash_erase_calls==erases+2);
    assert(odo_last_valid_slot()==0&&g_odometer_flash_erase_count>=1);
    assert(Odometer_Flash_Save(20000)&&odo_last_valid_slot()==1&&odo_boot()==20000);
    /* The carry-over write fails after the erase: the RAM value still reaches
     * the UI, and the next successful save restores the record. */
    flash_blank();assert(odo_boot()==0);
    for(uint32_t slot=0;slot<ODO_SLOT_COUNT;slot++)assert(Odometer_Flash_Save(3000+slot));
    flash_fail_programs=1;
    assert(odo_boot()==3000+ODO_SLOT_COUNT-1&&odo_last_valid_slot()==ODO_SLOT_COUNT);
    flash_fail_programs=0;
    assert(Odometer_Flash_Save(40000)&&odo_boot()==40000);

    /* GPS integration: the stored total starts the UI counter, every 100 m of
     * travel is appended, and a read-only sector never stops the counter. */
    flash_blank();assert(odo_boot()==0&&Odometer_Flash_Save(1000));
    Odometer_Flash_Init();
    g_odometer_initialized=0;gps_odometer_init();assert(g_odometer_initialized&&g_odometer_total_m==1000);
    g_odometer_total_m=1540;gps_odometer_init();assert(g_odometer_total_m==1540);
    for(unsigned step=0;step<130;step++) {
        float latitude=(float)(30.0+step*8.993e-6);
        gps_odometer_process(latitude,120.0f,7200,10000U+step*50U);
    }
    assert(g_odometer_total_m>=1640&&g_odometer_total_m-g_odometer_checkpoint_m<100U);
    assert(g_odometer_checkpoint_m>=1640&&odo_boot()==g_odometer_checkpoint_m);
    uint32_t shown=g_odometer_total_m;
    flash_fail_programs=1;fails=g_odometer_flash_save_fail_count;
    for(unsigned step=130;step<260;step++)
        gps_odometer_process((float)(30.0+step*8.993e-6),120.0f,7200,10000U+step*50U);
    flash_fail_programs=0;
    assert(g_odometer_total_m>=shown+100U&&g_odometer_flash_save_fail_count>fails&&!flash_unlocked);
    puts("Odometer: blank/reboot, torn and failed writes, junk sector, full sector, erase failure and GPS saves passed");
}
static void nmea(const char *payload)
{
    char line[160];unsigned sum=0;
    for(const char *p=payload;*p;p++)sum^=(unsigned char)*p;
    snprintf(line,sizeof(line),"$%s*%02X\r\n",payload,sum);
    for(const char *p=line;*p;p++)GPS_ProcessByte(*p);
}
static void test_gps(void)
{
    nmea("GPHPR,120000,123.4,0,0,4");assert(g_gps_data.heading_valid&&g_gps_data.heading_angle>123);
    nmea("GPHPR,120000,9999999999999999999999999999999999999999999999999999999999999,0,0,4");
    assert(!g_gps_data.heading_valid);
    assert(nmea_field_atoi("$GPGGA,999999999999999",1)==0);
    assert(nmea_field_atoi_milli("$GPGGA,999999999999999999999999",1)==0);
    assert(nmea_parse_speed_kmh_centi("$GPRMC,999999999999999999999999",1)==0);
    assert(nmea_parse_track_int("$GPRMC,999999999999999999999999",1)==0);
    assert(nmea_parse_coord_deg("$GPRMC,999999999999999999999999,N",1,2)==0);
    assert(gps_angle_to_cdeg(INFINITY)==0&&gps_angle_to_cdeg(-1)==0&&gps_angle_to_cdeg(360)==0);
    test_tick=UINT32_MAX-20;g_gps_poll_tick=test_tick;test_tick+=50;GPS_SentenceProcess();
    assert(g_gps_dma_late_poll_count==1&&g_gps_poll_gap_max_ms==50);
    test_tick=1000;g_gps_poll_tick=0;GPS_WaitAndDrain(500);
    assert(g_gps_poll_gap_max_ms==50&&g_gps_dma_late_poll_count==1);
    test_tick=10000;g_gps_rmc_last_tick=test_tick;g_gps_rmc_valid=1;
    g_gps_speed_kmh_centi=7200;g_gps_speed_kmh_int=72;g_gps_speed_ui_alive_tick=test_tick;
    /* Speed, position and motion go out every 50 ms; odometer/status every
     * 500 ms and lap timing every 200 ms (20-tick cycle = 20/20/2/5 frames). */
    unsigned sent=test_tx_count;
    memset(test_tx_id_count,0,sizeof(test_tx_id_count));
    g_gps_can_last_tick=0;g_gps_can_slow_phase=0;g_gps_can_lap_count=0;g_odometer_total_m=12345; /* 123 x 0.1 km */
    gps_can_speed_tick(test_tick);assert(test_can_speed_tenths==720);
    assert(test_tx_count==sent+5&&test_tx_id_count[0x069]==1&&test_tx_id_count[0x06A]==1);
    assert(test_tx_id_data[0x069][0]==123&&test_tx_id_data[0x069][1]==0&&test_tx_id_data[0x069][2]==0);
    sent=test_tx_count;gps_can_speed_tick(test_tick+49);assert(test_tx_count==sent);
    for(unsigned tick=1;tick<20;tick++) {
        g_gps_rmc_last_tick=test_tick+tick*50U;g_gps_rmc_valid=1;
        gps_can_speed_tick(test_tick+tick*50U);
    }
    assert(test_tx_id_count[0x301]==20&&test_tx_id_count[0x067]==20&&test_tx_id_count[0x068]==20);
    assert(test_tx_id_count[0x069]==2&&test_tx_id_count[0x06A]==5);
    /* A lap-count change sends status and lap timing at once, off the slow slots. */
    unsigned status=test_tx_id_count[0x069],lap=test_tx_id_count[0x06A];
    assert(g_gps_can_slow_phase==0);gps_can_speed_tick(test_tick+20U*50U);   /* slot 0: both due */
    assert(test_tx_id_count[0x069]==status+1&&test_tx_id_count[0x06A]==lap+1);
    g_gps_can_lap_count=7;gps_can_speed_tick(test_tick+21U*50U);            /* slot 1: nothing due, lap changed */
    assert(test_tx_id_count[0x069]==status+2&&test_tx_id_count[0x06A]==lap+2&&g_gps_can_lap_count==0);
    gps_can_speed_tick(test_tick+22U*50U);                                  /* slot 2: only the fast frames */
    assert(test_tx_id_count[0x069]==status+2&&test_tx_id_count[0x06A]==lap+2);
    g_gps_rmc_last_tick=test_tick;g_gps_rmc_valid=1;g_gps_speed_kmh_centi=7200;g_gps_speed_kmh_int=72;
    g_gps_can_last_tick=test_tick+50;
    gps_can_speed_tick(test_tick+501);assert(test_can_speed_tenths==0&&g_gps_speed_ui_alive_tick==test_tick);
    gps_can_speed_tick(test_tick+3001);assert(g_gps_speed_ui_alive_tick==0&&g_pending_speed==0);
    puts("GPS: fragmented NMEA, pathological heading, tick wrap, DMA polling and per-frame CAN periods passed");
}
static void test_ui(void)
{
    Dashboard_UI_RequestAlertClear();test_tick+=200;Dashboard_UI_Process();lv_refr_now(NULL);
    /* Re-submitting unchanged values must not invalidate anything. */
    const dashboard_data_t *d=Dashboard_UI_GetCurrentData();
    flushes=0;Dash_View_SetBattery(d->soc,d->sum_voltage,d->sum_current);
    for(unsigned wheel=0;wheel<4;wheel++)Dash_View_SetTire(wheel,g_tire_temp[wheel]);
    lv_refr_now(NULL);assert(flushes==0);
    for(unsigned i=0;i<400;i++) {
        dashboard_data_t data=*Dashboard_UI_GetCurrentData();
        data.soc=i%101;data.sum_voltage=400;data.sum_current=i%100;
        data.aps_open_pct=i%101;data.brake_pct=(i+10)%101;
        data.diag_num[i%4]=(i%5==0)?0x231:0;data.inverter_temp[i%4]=(int32_t)(i%120);
        data.lv_bus_current_cA=-(int32_t)i*10;data.fan_rpm[i%3]=(int32_t)i*10;
        Dashboard_UI_SubmitData(&data);Dashboard_UI_SubmitSpeed(i%300);
        Dashboard_UI_SubmitDriveMode(i%4);Dashboard_UI_SubmitSlipLevel(i%8);
        Dashboard_UI_PushAlert("TEST ALERT");if(i%3==0)Dashboard_UI_RequestAlertClear();
        test_tick+=100;Dashboard_UI_Process();lv_refr_now(NULL);
        assert(test_primask==0&&test_mutex_depth==0);
    }
    /* Inverter temperature, diagnostics, LV bus and fans reach the display copy. */
    assert(Dashboard_UI_GetCurrentData()->lv_bus_current_cA==-3990);
    lv_mem_monitor_t m;lv_mem_monitor(&m);assert(m.free_size>1024);
    printf("UI: 400 telemetry/mode/alert cycles, identical-value skip and 120 KiB host pool passed (peak %zu, free %zu)\n",m.max_used,m.free_size);
}
/* Representative telemetry for screenshots: one frame per drive mode plus an
 * alert frame (mode-4) and the S-mode READY banner (mode-5). */
static void save_frame(const char *directory,unsigned index)
{
    char path[1024];snprintf(path,sizeof(path),"%s/mode-%u.rgb565",directory,index);
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(frame_pixels,2,800*480,file)==800*480);fclose(file);
}
static void render_modes(const char *directory)
{
    static const int32_t tires[4][4]={{7200,7600,8100,8400},{6900,7300,7700,8000},{7400,7900,8600,9300},{6600,7000,7300,7500}};
    dashboard_data_t data=*Dashboard_UI_GetCurrentData();
    data.soc=76;data.sum_voltage=386;data.sum_current=118;data.max_temperature=74;
    data.aps_open_pct=62;data.brake_pct=0;
    data.lv_bus_voltage_mV=13820;data.lv_bus_current_cA=-1240;data.lv_bus_power_dW=1712;
    data.fan_rpm[0]=2376;data.fan_rpm[1]=2316;data.fan_rpm[2]=2196;data.fan_pwm_duty[0]=49;data.fan_pwm_duty[1]=49;
    for(unsigned w=0;w<4;w++) {
        data.torque[w]=18+(int32_t)w;data.rpm[w]=11800-(int32_t)w*120;data.motor_enable[w]=w!=3;
        data.motor_temp[w]=58+(int32_t)w*3;data.inverter_temp[w]=44+(int32_t)w*4;
        Dashboard_UI_SubmitTireTemperaturesCenti(w,tires[w]);
    }
    data.motor_temp[2]=88;data.diag_num[1]=0x0231;
    Dashboard_UI_SubmitData(&data);Dashboard_UI_SubmitSpeed(87);Dashboard_UI_SubmitSlipLevel(3);
    Dashboard_UI_SubmitSignalLevel(4);Dashboard_UI_SubmitOdometer(412);
    for(unsigned mode=0;mode<4;mode++) {
        Dashboard_UI_SubmitDriveMode(mode);test_tick+=300;Dashboard_UI_Process();
        Dashboard_UI_SubmitLapTimes(6233,8410,8345,3);Dashboard_UI_SubmitLapDelta(-32);
        test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);save_frame(directory,mode);
    }
    Dashboard_UI_PushAlert("inverter overtemp");Dashboard_UI_PushAlert("gps not fixed");
    test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);save_frame(directory,4);
    Dashboard_UI_RequestAlertClear();Dashboard_UI_SubmitDriveMode(0);Dashboard_UI_SetSprintReady(1);
    Dashboard_UI_SubmitSpeed(0);test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);save_frame(directory,5);
}
int main(int argc,char **argv)
{
    CAN_StartServiceTask();
    if(argc==1) {assert(!Dashboard_UI_IsStartupComplete());receive(0x305,6,3);assert(g_can_rx_queue->count==0);}
    init_ui();assert(Dashboard_UI_IsStartupComplete());
    if(argc==2) {render_modes(argv[1]);return 0;}
    test_can();test_odometer();test_gps();test_ui();return 0;
}
/* USER CODE END Firmware_Regression */
