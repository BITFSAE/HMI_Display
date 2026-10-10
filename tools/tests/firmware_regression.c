/* USER CODE BEGIN Firmware_Regression */
#include "mock_platform.h"
#include "firmware_diag.h"
#include <stdio.h>

uint32_t test_tick=1000, test_primask, test_dma_ndtr=512;
unsigned test_mutex_depth;
SD_HandleTypeDef hsd;
uint8_t g_sd_ready=1;
FATFS SDFatFS;
char SDPath[]="0:";
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
static CAN_TxHeaderTypeDef test_tx_header;
static uint8_t test_tx_data[8];
int HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h,CAN_FilterTypeDef *f) {(void)h;(void)f;return HAL_OK;}
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h,uint32_t fifo) {(void)h;(void)fifo;return test_rx_pending;}
int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h,uint32_t fifo,CAN_RxHeaderTypeDef *rx,uint8_t *data) {(void)h;(void)fifo;assert(test_rx_pending);test_rx_pending--;*rx=test_rx_header;memcpy(data,test_rx_data,8);return HAL_OK;}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) {(void)h;return 1;}
int HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h,CAN_TxHeaderTypeDef *tx,uint8_t *data,uint32_t *mail) {(void)h;*mail=0;test_tx_count++;test_tx_header=*tx;memcpy(test_tx_data,data,8);if(tx->StdId==0x301)test_can_speed_tenths=data[0]|((unsigned)data[1]<<8);return HAL_OK;}

static struct {uint8_t bytes[128];unsigned size;} files[3];
static int test_io_error,test_short_write,test_mounted;
static int test_open_error_file=-1,test_read_error_file=-1,test_close_error_file=-1;
static int test_sync_error;
FRESULT f_mount(FATFS *fs,const char *path,int opt) {(void)path;(void)opt;assert(test_mutex_depth);test_mounted=fs!=NULL;return fs&&test_io_error?FR_DISK_ERR:FR_OK;}
FRESULT f_open(FIL *fil,const char *path,unsigned mode) {assert(test_mounted&&test_mutex_depth);fil->file=strstr(path,"ODO_A")?0:strstr(path,"ODO_B")?1:2;if((int)fil->file==test_open_error_file)return FR_DISK_ERR;if(mode&FA_CREATE_ALWAYS)files[fil->file].size=0;return files[fil->file].size||(mode&FA_CREATE_ALWAYS)?FR_OK:FR_NO_FILE;}
FRESULT f_read(FIL *fil,void *data,unsigned size,UINT *bytes) {assert(test_mutex_depth);if((int)fil->file==test_read_error_file)return FR_DISK_ERR;*bytes=files[fil->file].size<size?files[fil->file].size:size;memcpy(data,files[fil->file].bytes,*bytes);return FR_OK;}
FRESULT f_write(FIL *fil,const void *data,unsigned size,UINT *bytes) {assert(test_mutex_depth&&size<=128);*bytes=test_short_write?size-1:size;memcpy(files[fil->file].bytes,data,*bytes);files[fil->file].size=*bytes;return FR_OK;}
FRESULT f_close(FIL *fil) {assert(test_mutex_depth);return (int)fil->file==test_close_error_file?FR_DISK_ERR:FR_OK;}
FRESULT f_sync(FIL *fil) {(void)fil;assert(test_mutex_depth);return test_sync_error?FR_DISK_ERR:FR_OK;}

/* Compile actual application code; only the Cube-generated CAN init/MSP
 * sections are omitted by run_firmware_tests.py. Hardware/RTOS are mocks. */
#include "can_app.inc"
#include "../../stm32_port/Core/Src/sd_log.c"
#include "../../stm32_port/Core/Src/gps_lap.c"
#include "../../stm32_port/Core/Src/gps.c"
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
static void store_record(unsigned file,uint32_t seq,uint32_t distance,int corrupt)
{
    sd_odometer_record_t r={SD_ODOMETER_MAGIC,seq,distance,sd_odometer_checksum(seq,distance)};
    if(corrupt)r.checksum^=1;
    memcpy(files[file].bytes,&r,sizeof(r));files[file].size=sizeof(r);
}
static void test_sd(void)
{
    uint32_t distance;
    SD_Odometer_StartTask();store_record(0,10,1234,0);store_record(1,11,1500,0);
    /* Storage faults must leave initialization pending and preserve both
     * records. GPS polling/configuration and CAN telemetry keep running. */
    for(unsigned failure=0;failure<6;failure++) {
        uint8_t records[2][128];
        memcpy(records[0],files[0].bytes,128);memcpy(records[1],files[1].bytes,128);
        g_sd_initial_odometer_ready=0;g_odometer_initialized=0;
        if(failure==0)g_sd_ready=0;
        if(failure==1)test_io_error=1;
        if(failure==2)test_open_error_file=1;
        if(failure==3)test_read_error_file=1;
        if(failure==4)test_close_error_file=1;
        if(failure==5)test_read_error_file=0;
        unsigned failures=g_sd_odometer_load_fail_count;
        assert(!sd_odometer_initialize()&&g_sd_odometer_load_fail_count==failures+1);
        distance=999;assert(!SD_Odometer_GetInitial(&distance)&&distance==999);
        assert(!sd_odometer_save(1));
        unsigned sent=test_tx_count;g_gps_can_last_tick=0;
        GPS_WaitAndDrain(100);assert(!g_odometer_initialized&&test_tx_count>sent);
        assert(!memcmp(records[0],files[0].bytes,128)&&!memcmp(records[1],files[1].bytes,128));
        assert(test_mutex_depth==0&&!test_mounted);
        if(failure==1) {
            SD_Log_InitAndWrite(NULL);assert(g_sd_diag_stage==SD_DIAG_MOUNT_FAILED&&!test_mounted&&test_mutex_depth==0);
        }
        g_sd_ready=1;test_io_error=0;test_open_error_file=-1;test_read_error_file=-1;test_close_error_file=-1;
        assert(sd_odometer_initialize());gps_odometer_init();assert(g_odometer_total_m==1500);
        g_odometer_total_m=1540;gps_odometer_init();assert(g_odometer_total_m==1540);
    }
    assert(SD_Odometer_Load(&distance)&&distance==1500&&test_mutex_depth==0);
    store_record(1,11,1500,1);assert(SD_Odometer_Load(&distance)&&distance==1234);
    store_record(0,UINT32_MAX,2000,0);store_record(1,0,2100,0);
    assert(SD_Odometer_Load(&distance)&&distance==2100);
    assert(sd_odometer_save(2200));assert(SD_Odometer_Load(&distance)&&distance==2200);
    uint32_t seq=g_sd_odometer_sequence;test_short_write=1;
    assert(!sd_odometer_save(2300)&&g_sd_odometer_sequence==seq&&test_mutex_depth==0&&!test_mounted);test_short_write=0;
    assert(SD_Odometer_Load(&distance)&&distance==2200);
    /* Sequence parity need not match the file containing the latest record. */
    store_record(0,UINT32_MAX,3000,0);files[1].size=0;
    assert(SD_Odometer_Load(&distance)&&distance==3000);
    test_short_write=1;assert(!sd_odometer_save(3100));test_short_write=0;
    assert(SD_Odometer_Load(&distance)&&distance==3000);
    assert(sd_odometer_save(3100)&&SD_Odometer_Load(&distance)&&distance==3100&&g_sd_odometer_sequence==0);
    seq=g_sd_odometer_sequence;unsigned next=g_sd_odometer_next_file;
    uint8_t preserved[128];memcpy(preserved,files[next^1].bytes,128);
    test_sync_error=1;assert(!sd_odometer_save(3200)&&g_sd_odometer_sequence==seq&&g_sd_odometer_next_file==next);test_sync_error=0;
    test_close_error_file=next;assert(!sd_odometer_save(3300)&&g_sd_odometer_sequence==seq&&g_sd_odometer_next_file==next);test_close_error_file=-1;
    assert(!memcmp(preserved,files[next^1].bytes,128)&&test_mutex_depth==0&&!test_mounted);
    test_io_error=1;assert(!SD_Odometer_Load(&distance)&&!sd_odometer_save(3400)&&test_mutex_depth==0&&!test_mounted);test_io_error=0;
    files[0].size=files[1].size=0;g_sd_initial_odometer_ready=0;
    assert(sd_odometer_initialize()&&SD_Odometer_GetInitial(&distance)&&distance==0);
    assert(sd_odometer_save(2200));
    SD_Log_InitAndWrite(NULL);assert(g_sd_diag_stage==SD_DIAG_OK&&test_mutex_depth==0);
    assert(sd_odometer_initialize());g_odometer_initialized=0;gps_odometer_init();assert(g_odometer_total_m==2200);
    puts("SD: startup I/O failures/recovery, empty card, preserved alternate record, sequence wrap and mutex cleanup passed");
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
    g_gps_can_last_tick=0;gps_can_speed_tick(test_tick);assert(test_can_speed_tenths==720);
    unsigned sent=test_tx_count;gps_can_speed_tick(test_tick+49);assert(test_tx_count==sent);
    gps_can_speed_tick(test_tick+50);assert(test_tx_count==sent+5);
    gps_can_speed_tick(test_tick+501);assert(test_can_speed_tenths==0&&g_gps_speed_ui_alive_tick==test_tick);
    gps_can_speed_tick(test_tick+3001);assert(g_gps_speed_ui_alive_tick==0&&g_pending_speed==0);
    puts("GPS: fragmented NMEA, pathological heading, tick wrap and configuration DMA polling passed");
}
static void test_ui(void)
{
    Dashboard_UI_RequestAlertClear();test_tick+=200;Dashboard_UI_Process();lv_refr_now(NULL);
    flushes=0;dashboard_set_label_text(g_dashboard.soc_value,lv_label_get_text(g_dashboard.soc_value));
    lv_refr_now(NULL);assert(flushes==0);
    for(unsigned wheel=0;wheel<4;wheel++)apply_vehicle_motor_ui(wheel);
    lv_refr_now(NULL);assert(flushes==0);
    for(unsigned i=0;i<400;i++) {
        dashboard_data_t data=*Dashboard_UI_GetCurrentData();
        data.soc=i%101;data.sum_voltage=400;data.sum_current=i%100;
        data.aps_open_pct=i%101;data.brake_pct=(i+10)%101;
        Dashboard_UI_SubmitData(&data);Dashboard_UI_SubmitSpeed(i%100);
        Dashboard_UI_SubmitDriveMode(i%4);Dashboard_UI_SubmitSlipLevel(i%8);
        Dashboard_UI_PushAlert("TEST ALERT");if(i%3==0)Dashboard_UI_RequestAlertClear();
        test_tick+=100;Dashboard_UI_Process();lv_refr_now(NULL);
        assert(test_primask==0&&test_mutex_depth==0);
    }
    lv_mem_monitor_t m;lv_mem_monitor(&m);assert(m.free_size>1024);
    printf("UI: 400 telemetry/mode/alert cycles, identical-label skip and 120 KiB host pool passed (peak %zu, free %zu)\n",m.max_used,m.free_size);
}
static void render_modes(const char *directory)
{
    for(unsigned mode=0;mode<4;mode++) {
        Dashboard_UI_SubmitDriveMode(mode);test_tick+=100;Dashboard_UI_Process();lv_refr_now(NULL);
        char path[1024];snprintf(path,sizeof(path),"%s/mode-%u.rgb565",directory,mode);
        FILE *file=fopen(path,"wb");assert(file);assert(fwrite(frame_pixels,2,800*480,file)==800*480);fclose(file);
    }
}
int main(int argc,char **argv)
{
    CAN_StartServiceTask();
    if(argc==1) {assert(!Dashboard_UI_IsStartupComplete());receive(0x305,6,3);assert(g_can_rx_queue->count==0);}
    init_ui();assert(Dashboard_UI_IsStartupComplete());
    if(argc==2) {render_modes(argv[1]);return 0;}
    test_can();test_sd();test_gps();test_ui();return 0;
}
/* USER CODE END Firmware_Regression */
