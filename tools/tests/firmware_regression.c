/* USER CODE BEGIN Firmware_Regression */
#include "mock_platform.h"
#include "firmware_diag.h"
#include <stdio.h>

uint32_t test_tick=1000, test_primask, test_dma_ndtr=512;
unsigned test_mutex_depth;
UART_HandleTypeDef huart2,huart3;
DMA_HandleTypeDef hdma_usart3_rx;
uint32_t SystemCoreClock=168000000U;
CAN_HandleTypeDef hcan1={CAN1},hcan2={CAN2};
volatile firmware_diag_t g_firmware_diag;
void Firmware_Diag_RegisterTask(firmware_task_id_t id,void *h,const char *n,uint32_t size) {assert(h&&n&&size);(void)id;}
void Firmware_Diag_LvglAssert(void) {fprintf(stderr,"LVGL allocation/assertion failed\n");abort();}
void LED_Diag_SetBootStage(uint8_t stage) {(void)stage;}
void Error_Handler(void) {abort();}
/* USART3 发往接收机的命令（去掉 CRLF）；hook 用来模拟接收机的回执。 */
static char test_cmd_log[64][64];
static unsigned test_cmd_count;
static void (*test_tx_hook)(const char *command);
int HAL_UART_Transmit(UART_HandleTypeDef *h,uint8_t *b,uint16_t n,uint32_t timeout)
{
    char command[64];unsigned len=0;
    (void)h;(void)timeout;
    while(len<n&&len<sizeof(command)-1&&b[len]!='\r'&&b[len]!='\n'){command[len]=(char)b[len];len++;}
    command[len]=0;
    if(test_cmd_count<64){strcpy(test_cmd_log[test_cmd_count],command);}
    test_cmd_count++;
    if(test_tx_hook)test_tx_hook(command);
    return HAL_OK;
}
static unsigned test_rx_dma_starts,test_rx_it_arms,test_abort_calls;
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *h,uint8_t *p,uint16_t n)
{
    (void)p;(void)n;
    if(h->RxState!=HAL_UART_STATE_READY)return 2;
    h->RxState=HAL_UART_STATE_BUSY_RX;test_rx_dma_starts++;test_dma_ndtr=512;return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *h,uint8_t *p,uint16_t n) {(void)h;(void)p;(void)n;test_rx_it_arms++;return HAL_OK;}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *h) {h->RxState=HAL_UART_STATE_READY;test_abort_calls++;return HAL_OK;}
/* USART2（命令行）：一次只允许一段发送在途，和 HAL 一致；测试用 cli_tx_complete 模拟发送完成中断。 */
static char test_cli_out[65536];
static unsigned test_cli_len,test_cli_tx_pending;
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *h,uint8_t *p,uint16_t n)
{
    (void)h;
    if(test_cli_tx_pending)return 2;
    assert(test_cli_len+n<sizeof(test_cli_out));
    memcpy(test_cli_out+test_cli_len,p,n);test_cli_len+=n;test_cli_out[test_cli_len]=0;
    test_cli_tx_pending=n;return HAL_OK;
}

static CAN_RxHeaderTypeDef test_rx_header;
static uint8_t test_rx_data[8];
static unsigned test_rx_pending,test_tx_count,test_can_speed_tenths;
static unsigned test_tx_id_count[0x800];
static uint8_t test_tx_id_data[0x800][8];
static unsigned test_can_error;
static unsigned test_can_free_mailboxes=1;
static CAN_TxHeaderTypeDef test_tx_header;
static uint8_t test_tx_data[8];
int HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h,CAN_FilterTypeDef *f) {(void)h;(void)f;return HAL_OK;}
uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h) {(void)h;return test_can_error;}
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h,uint32_t fifo) {(void)h;(void)fifo;return test_rx_pending;}
int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h,uint32_t fifo,CAN_RxHeaderTypeDef *rx,uint8_t *data) {(void)h;(void)fifo;assert(test_rx_pending);test_rx_pending--;*rx=test_rx_header;memcpy(data,test_rx_data,8);return HAL_OK;}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) {(void)h;return test_can_free_mailboxes;}
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
#include "../../stm32_port/Core/Src/sop.c"
#include "../../stm32_port/Core/Src/brake_pedal.c"
#include "../../stm32_port/Core/Src/odometer_flash.c"
#include "../../stm32_port/Core/Src/gps_lap.c"
#include "../../stm32_port/Core/Src/gps_speed.c"
#include "../../stm32_port/Core/Src/speed_trace.c"
#include "../../stm32_port/Core/Src/gps.c"
#include "../../stm32_port/Core/Src/diag_cli.c"
#include "../../stm32_port/Core/lvgl/lvgl.h"
#ifndef DASHBOARD_UI_SOURCE
#define DASHBOARD_UI_SOURCE "../../stm32_port/Core/Src/dashboard_ui.c"
#endif
#include DASHBOARD_UI_SOURCE

static uint16_t frame_pixels[800*480],draw_pixels[800*10];
static unsigned flushes;
static unsigned long flushed_px;   /* 写屏像素数：开屏每一步实际重绘的面积 */
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data)
{
    unsigned width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;y++) {
        memcpy(frame_pixels+y*800+area->x1,data,width*2);data+=width*2;
    }
    flushes++;flushed_px+=(unsigned long)width*(unsigned)(area->y2-area->y1+1);
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
        User_CAN_ProcessFifo0(&frame);
    }
}
static void receive(uint16_t id,unsigned dlc,unsigned count)
{
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=id,.DLC=dlc};
    test_rx_pending=count;HAL_CAN_RxFifo0MsgPendingCallback(&hcan1);
}
static void test_can(void)
{
    assert(CAN1_RX_MSG_ID_BANK4[0]==0x5A2&&CAN1_RX_MSG_ID_BANK4[1]==0x001&&
           CAN1_RX_MSG_ID_BANK4[2]==0x5A2&&CAN1_RX_MSG_ID_BANK4[3]==0x001);
    /* Hardware FIFO work is bounded, and software queue overflow is counted. */
    receive(0x305,6,4);assert(test_rx_pending==1);assert(g_can_rx_queue->count==3);service_can();
    for(unsigned i=0;i<65;i++)receive(0x305,6,1);
    assert(g_can_rx_queue->count==64&&g_can_rx_drop_count==1&&g_can_rx_queue_peak==64);service_can();
    unsigned invalid=g_can_rx_invalid_count;
    receive(0x4B0,2,1);service_can();assert(g_can_rx_invalid_count==invalid+1);
    memset(test_rx_data,0,8);test_rx_data[2]=0xF4;test_rx_data[3]=1;
    receive(0x305,6,1);service_can();assert(g_can_dashboard_data.aps_open_pct==50);
    test_rx_data[0]=0x10;receive(0x509,5,1);service_can();assert(g_can_dashboard_data.mode_index==1);
    assert(CAN2_RX_MSG_ID_BANK0[0]==0x310&&CAN2_RX_MSG_ID_BANK0[1]==0x703&&
           CAN2_RX_MSG_ID_BANK0[2]==0x310&&CAN2_RX_MSG_ID_BANK0[3]==0x703);
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=0x703,.DLC=2};test_rx_data[0]=7;test_rx_data[1]=0;test_rx_pending=1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);service_can();
    assert(test_tx_header.StdId==0x703&&test_tx_header.DLC==2&&test_tx_data[0]==7&&test_tx_data[1]==0);
    test_rx_header.RTR=1;test_rx_pending=1;invalid=g_can_rx_invalid_count;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);assert(g_can_rx_invalid_count==invalid+1);
    /* 0x050 的 IMU 设备已停用，车手屏不再转发 0x060..0x066；即使该 ID 到达也不得产生发送。 */
    memset(test_rx_data,0,8);test_rx_data[0]=0x55;test_rx_data[1]=0x51;
    unsigned tx_before=test_tx_count;
    receive(0x50,8,1);service_can();assert(test_tx_count==tx_before);
    puts("CAN: bounded FIFO, queue saturation, short/remote frames, valid DLCs and steering relay passed");
}
/* Every DASH_VALID_* bit, for tests that feed hand-built telemetry to the UI. */
#define TEST_ALL_VALID ((DASH_VALID_REGEN<<1)-1U)
static void inject(uint16_t id,unsigned dlc,const uint8_t *bytes)
{
    memset(test_rx_data,0,8);memcpy(test_rx_data,bytes,dlc);
    receive(id,dlc,1);service_can();
}
static void inject_wheel(uint16_t id,unsigned dlc,uint8_t first)
{
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=id,.DLC=dlc};
    memset(test_rx_data,0,8);test_rx_data[0]=first;test_rx_pending=1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);service_can();
}
/* 方向盘整帧 0x703：byte0 滑级，byte1 bit0 退使、bit1 清错、bit2 圈速计时。 */
static void inject_cmd(uint8_t slip,uint8_t bits)
{
    test_rx_header=(CAN_RxHeaderTypeDef){.StdId=0x703,.DLC=2};
    memset(test_rx_data,0,8);test_rx_data[0]=slip;test_rx_data[1]=bits;test_rx_pending=1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan2);service_can();
}
static int alert_shown(const char *text)
{
    for(unsigned i=0;i<g_alert_count;i++)if(!strcmp(g_alert_queue[i],text))return 1;
    return 0;
}

static void feed_encoder(brake_pedal_t *p,uint16_t count,uint16_t oil,uint32_t *now,unsigned samples)
{
    for(unsigned i=0;i<samples;i++) {
        BrakePedal_OnOil(p,oil,*now);
        BrakePedal_OnEncoder(p,count,*now);
        *now+=10U;
    }
}

static void test_brake_pedal_module(void)
{
    brake_pedal_t p;
    uint32_t now=1000U;
    uint16_t count=0;
    int32_t rest=0;
    const uint8_t position[7]={7,1,1,0xF2,0x0F,0,0};             /* 4082 = 358.8 deg */
    uint8_t bad[7]={7,1,1,0,0x10,0,0};                          /* count 4096 is out of range */

    BrakePedal_Init(&p);
    assert(BrakePedal_DecodePosition(position,7,&count)&&count==4082);
    assert(!BrakePedal_DecodePosition(position,4,&count));       /* command ACK */
    bad[2]=2;assert(!BrakePedal_DecodePosition(bad,7,&count));   /* other FUNC */
    bad[2]=1;bad[1]=2;assert(!BrakePedal_DecodePosition(bad,7,&count));
    assert(!BrakePedal_DecodePosition(position,6,&count));

    /* No rest zero means --, not 0 %; 1.5 s of stable counts at 2.135 kPa creates it. */
    BrakePedal_OnOil(&p,2135U,now);
    assert(BrakePedal_GetRegenPct(&p,now)==-1);
    feed_encoder(&p,4082U,2135U,&now,151U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-14);
    assert(BrakePedal_GetRegenPct(&p,now-10U)==0);

    /* 0.3 deg dead band, 7 deg full scale, saturated above 7 deg. */
    BrakePedal_OnEncoder(&p,4086U,now);                          /* +4 counts = 0.35 deg */
    assert(BrakePedal_GetRegenPct(&p,now)==0);
    BrakePedal_OnEncoder(&p,4090U,now);                          /* +8 counts = 0.70 deg */
    assert(BrakePedal_GetRegenPct(&p,now)==6);
    BrakePedal_OnEncoder(&p,4162U,now);                          /* +80 counts = 7.03 deg */
    assert(BrakePedal_GetRegenPct(&p,now)==100);

    /* A lower resting position is adopted after the 1.5 s window. */
    feed_encoder(&p,4060U,2135U,&now,152U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-36);
    assert(BrakePedal_GetRegenPct(&p,now-10U)==0);

    /* A higher position must hold for 5 s: 4.9 s keeps the old zero, 5.1 s replaces it. */
    feed_encoder(&p,4065U,2135U,&now,151U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-36);
    feed_encoder(&p,4065U,2135U,&now,340U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-36);
    feed_encoder(&p,4065U,2135U,&now,20U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-31);

    /* Oil outside the rest band prevents calibration, but does not erase an existing zero. */
    feed_encoder(&p,4060U,2150U,&now,151U);
    assert(BrakePedal_GetRestCount(&p,&rest)&&rest==-31);

    /* Mechanical brake keeps the oil calibration and its own 1 s freshness. */
    {
        struct {uint16_t oil;int pct;} const cases[]={{0,0},{2099,0},{2100,0},{3500,45},
                                                       {4900,90},{5200,99},{5300,100},{65535,100}};
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
            BrakePedal_OnOil(&p,cases[i].oil,now);
            assert(BrakePedal_GetMechanicalPct(&p,now)==cases[i].pct);
            now+=100U;
        }
        assert(BrakePedal_GetMechanicalPct(&p,now)==100);
        assert(BrakePedal_GetMechanicalPct(&p,now+901U)==-1);
    }

    /* Encoder silence invalidates the regen bar at 150 ms, not at the oil timeout. */
    BrakePedal_OnOil(&p,2135U,now);
    BrakePedal_OnEncoder(&p,4082U,now);
    assert(BrakePedal_GetRegenPct(&p,now+150U)>=0);
    assert(BrakePedal_GetRegenPct(&p,now+151U)==-1);
    puts("Brake pedal: frame validation, rest median, asymmetric zero update, 7 deg scaling and both timeouts passed");
}

static void test_brake_pedal_can(void)
{
    const dashboard_data_t *d=&g_can_dashboard_data;
    const uint8_t oil[6]={0,0,0xF4,0x01,0x57,0x08};              /* APS 50 %, oil 2.135 kPa */
    const uint8_t position[7]={7,1,1,0xF2,0x0F,0,0};
    uint8_t moved[7]={7,1,1,0xFA,0x0F,0,0};                     /* 4090: +0.70 deg */

    inject(0x305,6,oil);
    for(unsigned i=1;i<151U;i++) {
        if((i%10U)==0U) inject(0x305,6,oil);
        inject(0x001,7,position);
        test_tick+=10U;
    }
    inject(0x001,7,position);
    assert((d->valid_mask&DASH_VALID_REGEN)!=0U&&d->regen_pct==0);
    inject(0x001,7,moved);
    assert((d->valid_mask&DASH_VALID_REGEN)!=0U&&d->regen_pct==6);

    /* ACK and non-position FUNC frames are ignored without disturbing the last value. */
    inject(0x001,4,(const uint8_t[]){4,1,0,0});
    assert((d->valid_mask&DASH_VALID_REGEN)!=0U&&d->regen_pct==6);
    test_tick+=151U;User_CAN_ServiceTimeouts(test_tick);
    assert((d->valid_mask&DASH_VALID_REGEN)==0U);
    inject(0x001,7,moved);
    assert((d->valid_mask&DASH_VALID_REGEN)!=0U&&d->regen_pct==6);
}

static void test_can_decode(void)
{
    const dashboard_data_t *d=&g_can_dashboard_data;
    /* Nothing is valid before the first frame. */
    assert((d->valid_mask&(DASH_VALID_SOC|DASH_VALID_LV_BUS|DASH_VALID_FAN|DASH_VALID_TORQUE))==0);

    /* 0x503/0x504 carry AMK1..4 = RL, RR, FL, FR; dashboard order is LF, LR, RF, RR. */
    {
        const uint8_t a[8]={0x31,0x02,0,0, 0x22,0,0,0},b[8]={0x33,0,0,0, 0x44,0,0,0};
        inject(0x503,8,a);inject(0x504,8,b);
        assert(d->diag_num[1]==0x231&&d->diag_num[3]==0x22);   /* LR=RL=#1, RR=RR=#2 */
        assert(d->diag_num[0]==0x33&&d->diag_num[2]==0x44);    /* LF=FL=#3, RF=FR=#4 */
        assert((d->valid_mask&(DASH_VALID_DIAG_RL_RR|DASH_VALID_DIAG_FL_FR))==(DASH_VALID_DIAG_RL_RR|DASH_VALID_DIAG_FL_FR));
    }
    /* Per-wheel 0x506 temperatures and MAX T over fresh sources only. */
    {
        const uint8_t t[8]={0x2C,0x01, 0x90,0x01, 0xF4,0x01, 0x58,0x02};  /* RL 30, RR 40, FL 50, FR 60 degC */
        inject(0x506,8,t);
        assert(d->motor_temp[0]==50&&d->motor_temp[1]==30&&d->motor_temp[2]==60&&d->motor_temp[3]==40);
        assert(d->max_temperature==60);
    }
    /* 0x4B0: 549.1 V, -85.7 A (discharge is negative), SOC 77, all three flags set. */
    {
        const uint8_t pack[7]={0x15,0x73, 0xFC,0xA7, 77, 0x1F, 0x50};
        const uint32_t valid=DASH_VALID_SOC|DASH_VALID_HV_V|DASH_VALID_HV_I;
        unsigned peak_before;
        inject(0x4B0,7,pack);
        assert(d->sum_voltage_dv==5491&&d->sum_voltage==549&&d->sum_current_da==-857&&d->sum_current==-85&&d->soc==77);
        assert((d->valid_mask&valid)==valid);
        /* P NOW is the discharge power: 549.1 V x 85.7 A = 47.06 kW -> 47. */
        test_tick+=300;Dashboard_UI_Process();
        assert(g_power_peak==47&&g_power_peak_seen);peak_before=(unsigned)g_power_peak;
        /* The PK row tracks the discharge current in 0.1 A: -85.7 A -> 857. */
        assert(g_current_peak_seen&&g_current_peak_da==857);
        /* Charging (+2.9 A) is not drive power and must not move the peak. */
        {
            const uint8_t charge[7]={0x15,0x73, 0x00,0x1D, 77, 0x1F, 0x50};
            inject(0x4B0,7,charge);test_tick+=300;Dashboard_UI_Process();
            assert(d->sum_current_da==29&&(unsigned)g_power_peak==peak_before);
            assert(g_current_peak_da==857);                       /* charging never raises the current peak */
        }
        /* SOC flag clear (BMS default 80 % before initialisation): SOC invalid, V and I stay valid. */
        {
            const uint8_t no_soc[7]={0x15,0x73, 0x00,0x1D, 80, 0x1B, 0x50};
            inject(0x4B0,7,no_soc);
            assert((d->valid_mask&DASH_VALID_SOC)==0&&(d->valid_mask&(DASH_VALID_HV_V|DASH_VALID_HV_I))==(DASH_VALID_HV_V|DASH_VALID_HV_I));
            assert(d->soc==77);                                   /* the unqualified 80 is not stored */
        }
        /* No flags: nothing from this frame is usable. SOC above 100 % is rejected even when flagged. */
        {
            const uint8_t none[7]={0x15,0x73, 0x00,0x1D, 77, 0x00, 0x50},bad[7]={0x15,0x73, 0x00,0x1D, 101, 0x1F, 0x50};
            inject(0x4B0,7,none);assert((d->valid_mask&valid)==0);
            inject(0x4B0,7,bad);assert((d->valid_mask&DASH_VALID_SOC)==0&&(d->valid_mask&DASH_VALID_HV_V)!=0);
        }
    }
    /* PDM: only all four placeholders together mean the INA226 is offline. */
    {
        const uint8_t on[8]={0x35,0xFC, 0x04,0xD8, 0x06,0xAB, 0x00,0x10};     /* 13.820 V, 12.40 A */
        const uint8_t sat[8]={0x7F,0xFF, 0x04,0xD8, 0x06,0xAB, 0x00,0x10};    /* saturated voltage only */
        const uint8_t off[8]={0x7F,0xFF, 0x7F,0xFF, 0xFF,0xFF, 0xFF,0xFF};
        inject(0x5A0,8,on);assert((d->valid_mask&DASH_VALID_LV_BUS)!=0&&d->lv_bus_voltage_mV==13820);
        inject(0x5A0,8,sat);assert((d->valid_mask&DASH_VALID_LV_BUS)!=0&&d->lv_bus_voltage_mV==32767);
        inject(0x5A0,8,off);assert((d->valid_mask&DASH_VALID_LV_BUS)==0&&d->lv_bus_voltage_mV==32767);
        inject(0x5A0,8,on);assert((d->valid_mask&DASH_VALID_LV_BUS)!=0&&d->lv_bus_voltage_mV==13820);
    }
    /* 0x305 油压推算刹车条：2.1 kPa(2100)→0%，4.9 kPa(4900)→90%，线性并限幅到 0..100。 */
    {
        struct {uint16_t oil;int pct;} const cases[]={{0,0},{2099,0},{2100,0},{3500,45},{4900,90},{5200,99},{5300,100},{65535,100}};
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
            const uint8_t f[6]={0,0, 0xF4,0x01, (uint8_t)(cases[i].oil&0xFF),(uint8_t)(cases[i].oil>>8)};
            inject(0x305,6,f);
            assert((d->valid_mask&DASH_VALID_BRAKE)!=0&&d->brake_pct==cases[i].pct);
            assert(d->oil_pressure==cases[i].oil&&d->aps_open_pct==50);          /* 油门仍取 byte2..3 */
        }
    }
    /* Silence drops the valid bits: ECU frames after 1 s, BMS after 2 s, never-seen sources stay invalid. */
    {
        const uint8_t fan[8]={0x08,0x00, 0x08,0x00, 0x08,0x00, 40,41};
        const uint8_t pack[7]={0x15,0x73, 0xFC,0xA7, 77, 0x1F, 0x50};
        inject(0x5A2,8,fan);inject(0x4B0,7,pack);
        assert((d->valid_mask&DASH_VALID_FAN)!=0&&(d->valid_mask&DASH_VALID_SOC)!=0&&(d->valid_mask&DASH_VALID_MOTOR_TEMP)!=0);
        test_tick+=1001;User_CAN_ServiceTimeouts(test_tick);
        assert((d->valid_mask&DASH_VALID_MOTOR_TEMP)==0&&d->max_temperature==0);   /* ECU temperatures timed out */
        assert((d->valid_mask&DASH_VALID_FAN)!=0&&(d->valid_mask&DASH_VALID_SOC)!=0); /* fan 1.5 s, BMS 2 s still fresh */
        test_tick+=600;User_CAN_ServiceTimeouts(test_tick);
        assert((d->valid_mask&DASH_VALID_FAN)==0&&(d->valid_mask&DASH_VALID_SOC)!=0);
        test_tick+=500;User_CAN_ServiceTimeouts(test_tick);
        assert((d->valid_mask&(DASH_VALID_SOC|DASH_VALID_HV_V|DASH_VALID_HV_I))==0);
        assert((d->valid_mask&(DASH_VALID_BRAKE|DASH_VALID_APS))==0);               /* 0x305 timed out */
        assert((d->valid_mask&DASH_VALID_REGEN)==0);                                /* encoder also timed out */
        Dashboard_UI_Process();test_tick+=300;Dashboard_UI_Process();
        assert((Dashboard_UI_GetCurrentData()->valid_mask&DASH_VALID_SOC)==0);       /* and the UI copy follows */
        inject(0x4B0,7,pack);assert((d->valid_mask&DASH_VALID_SOC)!=0);               /* data resumes */
    }
    /* BMS SOP V2: 0x4A0 power limits (CRC) and 0x4A3 current limits, separate freshness. */
    {
        /* Sample frames from vehicle-interfaces docs/CANB接口.md 4.4. */
        const uint8_t power[8]={0xE4,0x02,0x28,0x00,0x05,0x02,0x01,0x29};   /* 74.0 kW / 4.0 kW, valid */
        const uint8_t current[8]={0x08,0x07,0x50,0x00,0x00,0x00,0xFF,0x24}; /* 180.0 A / 8.0 A, inputs valid */
        const uint8_t check[9]={'1','2','3','4','5','6','7','8','9'};
        uint8_t frame[8];
        unsigned invalid;
        assert(Sop_Crc8(check,9)==0x4B);
        assert((d->valid_mask&(DASH_VALID_SOP_POWER|DASH_VALID_SOP_CURRENT))==0);   /* nothing before the first frame */
        inject(0x4A0,8,power);inject(0x4A3,8,current);
        assert((d->valid_mask&(DASH_VALID_SOP_POWER|DASH_VALID_SOP_CURRENT))==(DASH_VALID_SOP_POWER|DASH_VALID_SOP_CURRENT));
        assert(d->sop_discharge_power_dkw==740&&d->sop_charge_power_dkw==40);
        assert(d->sop_discharge_current_da==1800&&d->sop_charge_current_da==80);
        test_tick+=300;Dashboard_UI_Process();
        assert(Dashboard_UI_GetCurrentData()->sop_discharge_power_dkw==740&&(Dashboard_UI_GetCurrentData()->valid_mask&DASH_VALID_SOP_POWER)!=0);
        /* Rejected frames change nothing and are counted: bad CRC, version 1, reserved bit, short DLC. */
        invalid=g_can_rx_invalid_count;
        memcpy(frame,power,8);frame[0]=0x10;inject(0x4A0,8,frame);                 /* CRC no longer matches */
        assert(d->sop_discharge_power_dkw==740);
        memcpy(frame,power,8);frame[5]=1;frame[7]=Sop_Crc8((const uint8_t[]){0x04,0xA0,0xE4,0x02,0x28,0x00,0x05,1,0x01},9);
        inject(0x4A0,8,frame);assert(d->sop_discharge_power_dkw==740);               /* valid CRC, wrong version */
        memcpy(frame,power,8);frame[6]=0x03;frame[7]=Sop_Crc8((const uint8_t[]){0x04,0xA0,0xE4,0x02,0x28,0x00,0x05,0x02,0x03},9);
        inject(0x4A0,8,frame);assert(d->sop_discharge_power_dkw==740);               /* reserved bit set */
        inject(0x4A0,7,power);                                                       /* DLC 7 */
        memcpy(frame,current,8);frame[7]=0x14;inject(0x4A3,8,frame);                 /* 0x4A3 version 1 */
        assert(g_can_rx_invalid_count==invalid+5&&d->sop_discharge_current_da==1800);
        /* LimitsValid=0: limits are not usable, power invalid but current stays. */
        memcpy(frame,power,8);frame[0]=frame[1]=frame[2]=frame[3]=0;frame[6]=0;
        frame[7]=Sop_Crc8((const uint8_t[]){0x04,0xA0,0,0,0,0,0x05,0x02,0x00},9);
        inject(0x4A0,8,frame);
        assert((d->valid_mask&DASH_VALID_SOP_POWER)==0&&(d->valid_mask&DASH_VALID_SOP_CURRENT)!=0);
        /* InputHealth bit7 clear: current limits are not trusted. */
        memcpy(frame,current,8);frame[6]=0x7F;inject(0x4A3,8,frame);
        assert((d->valid_mask&DASH_VALID_SOP_CURRENT)==0);
        /* Resume, then silence: power times out after 300 ms, current after 1.5 s. */
        inject(0x4A0,8,power);inject(0x4A3,8,current);
        test_tick+=301;User_CAN_ServiceTimeouts(test_tick);
        assert((d->valid_mask&DASH_VALID_SOP_POWER)==0&&(d->valid_mask&DASH_VALID_SOP_CURRENT)!=0);
        test_tick+=1300;User_CAN_ServiceTimeouts(test_tick);
        assert((d->valid_mask&DASH_VALID_SOP_CURRENT)==0);
        Dashboard_UI_Process();test_tick+=300;Dashboard_UI_Process();
        assert((Dashboard_UI_GetCurrentData()->valid_mask&(DASH_VALID_SOP_POWER|DASH_VALID_SOP_CURRENT))==0);
    }
    /* Bus-off notification is counted. */
    {
        unsigned bus_off=g_can_bus_off_count;
        test_can_error=0x1;HAL_CAN_ErrorCallback(&hcan1);assert(g_can_bus_off_count==bus_off);
        test_can_error=HAL_CAN_ERROR_BOF;HAL_CAN_ErrorCallback(&hcan1);assert(g_can_bus_off_count==bus_off+1);
        test_can_error=0;
    }
}
static void test_steering(void)
{
    unsigned invalid=g_can_rx_invalid_count,relayed;
    Dashboard_UI_SubmitDriveMode(0);test_tick+=300;Dashboard_UI_Process();assert(g_drive_mode==DRIVE_MODE_ACCELERATION);
    /* 0x310：只接受 mode + 48（47..51），包括默认模式请求。 */
    relayed=test_tx_id_count[0x310];
    inject_wheel(0x310,8,0x35);assert(test_tx_id_count[0x310]==relayed&&g_can_rx_invalid_count==invalid+1);
    inject_wheel(0x310,8,0x2E);assert(test_tx_id_count[0x310]==relayed&&g_can_rx_invalid_count==invalid+2);
    inject_wheel(0x310,8,0x31);assert(test_tx_id_count[0x310]==relayed+1&&test_tx_id_data[0x310][0]==0x31);
    assert((g_dashboard_dirty_mask&DASH_DIRTY_MODE)!=0U&&g_pending_dashboard_data.mode_index==1);
    /* 计时锁生效：0x310 不转发，界面也不接受新模式，只请求 MODE LOCK 告警。 */
    test_tick+=300;Dashboard_UI_Process();assert(g_drive_mode==DRIVE_MODE_AUTOCROSS);
    g_lap_analysis_active=1U;
    relayed=test_tx_id_count[0x310];
    inject_wheel(0x310,8,'2');
    assert(test_tx_id_count[0x310]==relayed&&g_drive_mode==DRIVE_MODE_AUTOCROSS&&g_pending_mode_lock_alert==1U);
    g_lap_analysis_active=0U;g_pending_mode_lock_alert=0U;
    /* 0x703 由方向盘整帧组好，每个 DLC>=2 的帧都原样转发（ID、DLC、内容不变）。 */
    unsigned commands=test_tx_id_count[0x703];
    inject_cmd(5,0);
    assert(test_tx_id_count[0x703]==commands+1&&test_tx_header.DLC==2&&test_tx_id_data[0x703][0]==5&&test_tx_id_data[0x703][1]==0);
    assert((g_dashboard_dirty_mask&DASH_DIRTY_SLIP)!=0U&&g_pending_dashboard_data.slip_level==5);
    /* DLC 不足 2 丢弃；滑级越界照常转发但不显示。 */
    inject_wheel(0x703,1,3);assert(test_tx_id_count[0x703]==commands+1&&g_can_rx_invalid_count==invalid+3);
    inject_cmd(9,0);
    assert(test_tx_id_count[0x703]==commands+2&&test_tx_id_data[0x703][0]==9&&g_can_rx_invalid_count==invalid+4);
    assert(g_pending_dashboard_data.slip_level==5);
    /* 0x700..0x702 已停用：即使到达也不处理、不转发。 */
    unsigned tx_before=test_tx_count;
    g_pending_lap_toggle=0;g_pending_alert_clear=0;
    inject_wheel(0x700,1,3);inject_wheel(0x701,1,1);inject_wheel(0x702,1,1);
    assert(test_tx_count==tx_before&&g_pending_lap_toggle==0&&g_pending_alert_clear==0);
    /* LapTimerToggle（bit2）：上升沿触发；300 ms 内重复的 1 不再触发，中间有 0 帧则重新计。 */
    inject_cmd(5,0x04);assert(g_pending_lap_toggle==1&&test_tx_id_data[0x703][1]==0x04);
    g_pending_lap_toggle=0;test_tick+=20;
    inject_cmd(5,0x04);assert(g_pending_lap_toggle==0);
    test_tick+=20;inject_cmd(5,0);assert(g_pending_lap_toggle==0);
    test_tick+=20;inject_cmd(5,0x04);assert(g_pending_lap_toggle==1);
    g_pending_lap_toggle=0;test_tick+=100;
    inject_cmd(5,0x04);assert(g_pending_lap_toggle==0);
    test_tick+=400;inject_cmd(5,0x04);assert(g_pending_lap_toggle==1);
    g_pending_lap_toggle=0;
    /* bit0 不再是圈速计时：只置 bit0 不得触发计时。 */
    test_tick+=400;inject_cmd(5,0x01);assert(g_pending_lap_toggle==0);
    test_tick+=20;inject_cmd(5,0);
    /* ErrorClearActive（bit1）同样按上升沿。 */
    g_pending_alert_clear=0;test_tick+=400;
    inject_cmd(5,0x02);assert(g_pending_alert_clear==1&&g_pending_lap_toggle==0);
    g_pending_alert_clear=0;test_tick+=20;
    inject_cmd(5,0x02);assert(g_pending_alert_clear==0);
    test_tick+=20;inject_cmd(5,0);
    /* DisableRequest（bit0）：方向盘每 20 ms 发一帧 1，只在上升沿弹一次提示；每帧都转发。
     * 计时锁期间也必须转发。 */
    Dashboard_UI_Process();g_alert_count=0;
    g_lap_analysis_active=1U;
    commands=test_tx_id_count[0x703];
    for(unsigned i=0;i<10;i++){test_tick+=20;inject_cmd(5,0x01);}
    assert(test_tx_id_count[0x703]==commands+10&&test_tx_id_data[0x703][1]==0x01);
    assert(g_pending_disable_alert==1&&g_pending_lap_toggle==0&&g_pending_alert_clear==0);
    Dashboard_UI_Process();
    assert(g_pending_disable_alert==0&&alert_shown("DISABLE REQUESTED"));
    for(unsigned i=0;i<10;i++){test_tick+=20;inject_cmd(5,0x01);}
    assert(g_pending_disable_alert==0);
    test_tick+=20;inject_cmd(5,0);assert(test_tx_id_count[0x703]==commands+21&&test_tx_id_data[0x703][1]==0);
    test_tick+=500;inject_cmd(5,0x01);assert(g_pending_disable_alert==1);
    test_tick+=20;inject_cmd(5,0);
    g_lap_analysis_active=0U;g_pending_disable_alert=0;g_alert_count=0;
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
static void utc_str(char *out,uint32_t ms)
{
    ms%=86400000UL;
    snprintf(out,16,"%02u%02u%02u.%02u",(unsigned)(ms/3600000UL),(unsigned)((ms/60000UL)%60),(unsigned)((ms/1000UL)%60),(unsigned)((ms%1000UL)/10));
}
/* 构造一条 RMC：epoch_ms 为 UTC 当日毫秒，knots 为速度字段原文，空串表示字段缺失。 */
static void rmc_full(uint32_t epoch_ms,char status,const char *knots,double lat,double lon)
{
    char utc[16],payload[160],latf[24],lonf[24];
    double lat_deg=floor(lat),lon_deg=floor(lon);
    utc_str(utc,epoch_ms);
    snprintf(latf,sizeof(latf),"%02d%08.5f",(int)lat_deg,(lat-lat_deg)*60.0);
    snprintf(lonf,sizeof(lonf),"%03d%08.5f",(int)lon_deg,(lon-lon_deg)*60.0);
    snprintf(payload,sizeof(payload),"GNRMC,%s,%c,%s,N,%s,E,%s,90.0,181026,,,A,V",utc,status,latf,lonf,knots);
    nmea(payload);
}
static void rmc(uint32_t epoch_ms,char status,const char *knots) {rmc_full(epoch_ms,status,knots,32.0,118.0);}
static void inject_line(const char *text)
{
    for(const char *p=text;*p;p++)GPS_ProcessByte((uint8_t)*p);
    GPS_ProcessByte('\r');GPS_ProcessByte('\n');
}
static void gps_test_reset(void)
{
    memset(&g_stats,0,sizeof(g_stats));memset(&g_cfg,0,sizeof(g_cfg));
    GPS_Speed_Init(&g_gps_speed_st);gps_window_clear(test_tick);
    g_stats.shown_speed_kmh=-1;g_stats.last_epoch_ms=GPS_CAPTURE_NO_EPOCH;g_ui_speed_shown=0;
    memset(&g_gps_data,0,sizeof(g_gps_data));g_gps_track_tenths=0;
    g_gga_tick=0;g_service_tick=0;g_odometer_seen_seq=0;g_odometer_anchor_valid=0;
    g_gps_line_len=0;g_gps_line_active=0;g_gps_dma_restarted=0;g_gps_request=GPS_REQ_NONE;
    g_capture_state=GPS_CAPTURE_IDLE;g_capture_count=0;g_raw_active=0;g_raw_count=0;
    g_gps_can_last_tick=0;g_gps_can_slow_phase=0;g_gps_can_lap_count=0;
    huart3.RxState=HAL_UART_STATE_BUSY_RX;test_dma_ndtr=512;g_gps_dma_last_pos=0;
    g_gps_poll_tick=0;g_gps_poll_gap_max_ms=0;g_gps_dma_late_poll_count=0;
}
static int alert_pending(const char *text)
{
    for(unsigned i=0;i<g_pending_alert_count;i++)if(!strcmp((const char *)g_pending_alert_queue[i],text))return 1;
    return 0;
}

static void test_gps(void)
{
    gps_test_reset();
    nmea("GPHPR,120000,123.4,0,0,4");assert(g_gps_data.heading_valid&&g_gps_data.heading_angle>123);
    nmea("GPHPR,120000,9999999999999999999999999999999999999999999999999999999999999,0,0,4");
    assert(!g_gps_data.heading_valid);
    int32_t centi=-1;
    assert(nmea_field_atoi("$GPGGA,999999999999999",1)==0);
    assert(nmea_field_atoi_milli("$GPGGA,999999999999999999999999",1)==0);
    assert(!nmea_parse_speed_kmh_centi("$GPRMC,999999999999999999999999",1,&centi)&&centi==-1);
    assert(nmea_parse_track_int("$GPRMC,999999999999999999999999",1)==0);
    assert(nmea_parse_coord_deg("$GPRMC,999999999999999999999999,N",1,2)==0);
    assert(gps_angle_to_cdeg(INFINITY)==0&&gps_angle_to_cdeg(-1)==0&&gps_angle_to_cdeg(360)==0);
    /* 速度字段：空、非数字、负数都不是 0 km/h；合法数字按 1 kn = 1.852 km/h 换算并限幅。 */
    assert(!nmea_parse_speed_kmh_centi("$GNRMC,,",1,&centi));
    assert(!nmea_parse_speed_kmh_centi("$GNRMC,abc,",1,&centi));
    assert(!nmea_parse_speed_kmh_centi("$GNRMC,-1.0,",1,&centi));
    assert(!nmea_parse_speed_kmh_centi("$GNRMC,1.2.3,",1,&centi));
    assert(nmea_parse_speed_kmh_centi("$GNRMC,0.000,",1,&centi)&&centi==0);
    assert(nmea_parse_speed_kmh_centi("$GNRMC,38.88,",1,&centi)&&centi==7201);
    assert(nmea_parse_speed_kmh_centi("$GNRMC,1000.0,",1,&centi)&&centi==30000);
    uint32_t epoch=0;uint8_t fine=9;
    assert(nmea_parse_utc_ms("$GNRMC,123456.78,",1,&epoch,&fine)&&epoch==45296780UL&&fine==1);
    assert(nmea_parse_utc_ms("$GNRMC,235959,",1,&epoch,&fine)&&epoch==86399000UL&&fine==0);
    assert(!nmea_parse_utc_ms("$GNRMC,246000.00,",1,&epoch,&fine)&&!nmea_parse_utc_ms("$GNRMC,,",1,&epoch,&fine));
    test_tick=UINT32_MAX-20;g_gps_poll_tick=test_tick;test_tick+=50;GPS_SentenceProcess();
    assert(g_gps_dma_late_poll_count==1&&g_gps_poll_gap_max_ms==50);
    test_tick=1000;g_gps_poll_tick=0;GPS_WaitAndDrain(500);
    assert(g_gps_poll_gap_max_ms==50&&g_gps_dma_late_poll_count==1);

    /* CAN 节拍：用真实 RMC 语句建立速度，再检查 50 ms 帧、500 ms 状态帧、200 ms 圈速帧。 */
    test_tick=10000;rmc(43200000UL,'A',"38.88");
    assert(g_stats.rmc_accepted==1&&g_pending_speed==72);
    unsigned sent=test_tx_count;
    memset(test_tx_id_count,0,sizeof(test_tx_id_count));
    g_gps_can_last_tick=0;g_gps_can_slow_phase=0;g_gps_can_lap_count=0;g_odometer_total_m=12345; /* 123 x 0.1 km */
    gps_can_speed_tick(test_tick);assert(test_can_speed_tenths==720);
    assert(test_tx_count==sent+5&&test_tx_id_count[0x7A0]==1&&test_tx_id_count[0x7A1]==1);
    assert(test_tx_id_data[0x7A0][0]==123&&test_tx_id_data[0x7A0][1]==0&&test_tx_id_data[0x7A0][2]==0);
    assert((test_tx_id_data[0x068][6]&0x08)!=0);                       /* RMC_VALID */
    sent=test_tx_count;gps_can_speed_tick(test_tick+49);assert(test_tx_count==sent);
    for(unsigned tick=1;tick<20;tick++) {
        test_tick=10000+tick*50U;rmc(43200000UL+tick*50U,'A',"38.88");
        gps_can_speed_tick(test_tick);
    }
    assert(test_tx_id_count[0x301]==20&&test_tx_id_count[0x067]==20&&test_tx_id_count[0x068]==20);
    assert(test_tx_id_count[0x7A0]==2&&test_tx_id_count[0x7A1]==5);
    /* A lap-count change sends status and lap timing at once, off the slow slots. */
    unsigned status=test_tx_id_count[0x7A0],lap=test_tx_id_count[0x7A1];
    test_tick=10000;
    assert(g_gps_can_slow_phase==0);gps_can_speed_tick(test_tick+20U*50U);   /* slot 0: both due */
    assert(test_tx_id_count[0x7A0]==status+1&&test_tx_id_count[0x7A1]==lap+1);
    g_gps_can_lap_count=7;gps_can_speed_tick(test_tick+21U*50U);            /* slot 1: nothing due, lap changed */
    assert(test_tx_id_count[0x7A0]==status+2&&test_tx_id_count[0x7A1]==lap+2&&g_gps_can_lap_count==0);
    gps_can_speed_tick(test_tick+22U*50U);                                  /* slot 2: only the fast frames */
    assert(test_tx_id_count[0x7A0]==status+2&&test_tx_id_count[0x7A1]==lap+2);
    /* 断流：CAN 车速 500 ms 后报 0 且 RMC_VALID 清零；本地门限 150 ms 先让界面显示 "--"。 */
    test_tick=20000;rmc(44000000UL,'A',"38.88");
    g_gps_can_last_tick=test_tick-50;gps_can_speed_tick(test_tick);assert(test_can_speed_tenths==720);
    g_gps_can_last_tick=test_tick;gps_can_speed_tick(test_tick+400);assert(test_can_speed_tenths==720);
    gps_can_speed_tick(test_tick+451);                                         /* 周期到，但还没过 500 ms */
    assert(test_can_speed_tenths==720&&(test_tx_id_data[0x068][6]&0x08)!=0);
    g_gps_can_last_tick=test_tick;gps_can_speed_tick(test_tick+501);
    assert(test_can_speed_tenths==0&&(test_tx_id_data[0x068][6]&0x08)==0);
    gps_speed_service(test_tick+100);assert(g_pending_speed==72);          /* 仍在 150 ms 内 */
    gps_speed_service(test_tick+151);assert(g_pending_speed==-1&&g_stats.speed_invalid_events==1);
    puts("GPS: fragmented NMEA, pathological heading, tick wrap, DMA polling, speed field parsing and per-frame CAN periods passed");
}

static gps_speed_view_t view_at(const gps_speed_state_t *st,uint32_t now) {gps_speed_view_t v;GPS_Speed_GetView(st,now,0,&v);return v;}
static gps_rmc_sample_t sample(uint32_t epoch,int32_t centi)
{
    return (gps_rmc_sample_t){.fix_ok=1,.speed_present=1,.speed_centi=centi,.position_present=1,.epoch_present=1,.epoch_fine=1,.epoch_ms=epoch};
}
static void test_speed_module(void)
{
    gps_speed_state_t st;gps_speed_view_t v;uint8_t pos;gps_rmc_sample_t s;
    GPS_Speed_Init(&st);
    v=view_at(&st,1000);assert(!v.valid&&v.reason==GPS_SPEED_REASON_NO_DATA&&v.speed_centi==0);
    assert(!GPS_Speed_PositionFresh(&st,1000)&&GPS_Speed_FreshLimit(&st)==150);
    /* 新鲜度门限：3 个 20 Hz 周期 = 150 ms，门限时刻仍有效，过一毫秒失效；失效时速度为 0 但原因是 STALE。 */
    s=sample(43200000UL,7201);
    assert(GPS_Speed_OnRmc(&st,&s,1000,&pos)==GPS_RMC_ACCEPTED&&pos==1&&st.continuity_seq==1);
    v=view_at(&st,1150);assert(v.valid&&v.speed_centi==7201&&v.age_ms==150&&v.limit_ms==150);
    v=view_at(&st,1151);assert(!v.valid&&v.reason==GPS_SPEED_REASON_STALE&&v.speed_centi==0);
    /* 持续到来的样本不改变连续性序号；断流后重新接受才加一。 */
    s.epoch_ms+=50;assert(GPS_Speed_OnRmc(&st,&s,1050,&pos)==GPS_RMC_ACCEPTED);
    assert(st.continuity_seq==1&&st.last_interval_ms==50&&st.last_epoch_delta_ms==50);
    s.epoch_ms+=450;assert(GPS_Speed_OnRmc(&st,&s,1500,&pos)==GPS_RMC_ACCEPTED);
    assert(st.continuity_seq==2&&st.last_interval_ms==450);
    /* 状态 V：立即失效，原因 NO_FIX，位置同时失效；恢复后的第一条再次增加序号。 */
    s.fix_ok=0;assert(GPS_Speed_OnRmc(&st,&s,1510,&pos)==GPS_RMC_NO_FIX&&pos==0);
    v=view_at(&st,1510);assert(!v.valid&&v.reason==GPS_SPEED_REASON_NO_FIX&&v.speed_centi==0);
    assert(!GPS_Speed_PositionFresh(&st,1510));
    s.fix_ok=1;s.epoch_ms+=50;assert(GPS_Speed_OnRmc(&st,&s,1560,&pos)==GPS_RMC_ACCEPTED&&st.continuity_seq==3);
    v=view_at(&st,1560);assert(v.valid&&GPS_Speed_PositionFresh(&st,1560));
    /* 重复历元：不续期，也不刷新位置。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,1000);
    assert(GPS_Speed_OnRmc(&st,&s,2000,&pos)==GPS_RMC_ACCEPTED);
    assert(GPS_Speed_OnRmc(&st,&s,2050,&pos)==GPS_RMC_DUP_EPOCH&&pos==0&&st.last_reject==GPS_RMC_DUP_EPOCH);
    v=view_at(&st,2100);assert(v.valid&&v.age_ms==100);
    v=view_at(&st,2151);assert(!v.valid&&v.reason==GPS_SPEED_REASON_STALE);
    /* 倒退历元：同样不续期。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,1000);
    assert(GPS_Speed_OnRmc(&st,&s,3000,&pos)==GPS_RMC_ACCEPTED);
    s.epoch_ms-=1000;assert(GPS_Speed_OnRmc(&st,&s,3050,&pos)==GPS_RMC_BACK_EPOCH&&pos==0);
    assert(view_at(&st,3100).age_ms==100);
    /* 速度字段缺失（状态 A）：不当作 0，不续期速度；位置仍可续期。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,1000);
    assert(GPS_Speed_OnRmc(&st,&s,4000,&pos)==GPS_RMC_ACCEPTED);
    s.epoch_ms+=50;s.speed_present=0;s.speed_centi=0;
    assert(GPS_Speed_OnRmc(&st,&s,4050,&pos)==GPS_RMC_BAD_FIELD&&pos==1);
    v=view_at(&st,4100);assert(v.valid&&v.speed_centi==1000&&v.age_ms==100);
    assert(GPS_Speed_PositionFresh(&st,4200)&&!GPS_Speed_PositionFresh(&st,4201));
    /* 跨午夜：23:59:59.95 → 00:00:00.00 是前进 50 ms，不是倒退。 */
    GPS_Speed_Init(&st);s=sample(86399950UL,500);
    assert(GPS_Speed_OnRmc(&st,&s,5000,&pos)==GPS_RMC_ACCEPTED);
    s.epoch_ms=0;assert(GPS_Speed_OnRmc(&st,&s,5050,&pos)==GPS_RMC_ACCEPTED&&st.last_epoch_delta_ms==50);
    s.epoch_ms=50;assert(GPS_Speed_OnRmc(&st,&s,5100,&pos)==GPS_RMC_ACCEPTED&&st.continuity_seq==1);
    /* 只有整秒的时间字段无法区分同一秒内的多条：不做重复检查，也不丢弃数据。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,500);s.epoch_fine=0;
    assert(GPS_Speed_OnRmc(&st,&s,6000,&pos)==GPS_RMC_ACCEPTED);
    assert(GPS_Speed_OnRmc(&st,&s,6050,&pos)==GPS_RMC_ACCEPTED&&st.accepted==2);
    /* 静默超过 1 s 后历元基准作废：之后即使历元更小也接受（接收机重启）。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,500);
    assert(GPS_Speed_OnRmc(&st,&s,7000,&pos)==GPS_RMC_ACCEPTED);
    s.epoch_ms=49000000UL;assert(GPS_Speed_OnRmc(&st,&s,8500,&pos)==GPS_RMC_ACCEPTED);
    /* MCU 计时回绕：龄期按无符号差计算。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,500);
    assert(GPS_Speed_OnRmc(&st,&s,UINT32_MAX-9U,&pos)==GPS_RMC_ACCEPTED);
    v=view_at(&st,70);assert(v.valid&&v.age_ms==80);
    s.epoch_ms+=50;assert(GPS_Speed_OnRmc(&st,&s,40,&pos)==GPS_RMC_ACCEPTED&&st.last_interval_ms==50&&st.continuity_seq==1);
    assert(!view_at(&st,191).valid&&view_at(&st,190).valid);
    /* 门限随实测输出率收敛：10 Hz → 约 300 ms；1 Hz → 上限 1500 ms；一次断流不会把门限拉高。 */
    GPS_Speed_Init(&st);s=sample(10000000UL,500);
    uint32_t t=1000;
    for(unsigned i=0;i<60;i++,t+=100) {s.epoch_ms+=100;assert(GPS_Speed_OnRmc(&st,&s,t,&pos)==GPS_RMC_ACCEPTED);}
    assert(GPS_Speed_FreshLimit(&st)>=285&&GPS_Speed_FreshLimit(&st)<=315);
    GPS_Speed_Init(&st);s=sample(10000000UL,500);t=1000;
    for(unsigned i=0;i<60;i++,t+=50) {s.epoch_ms+=50;assert(GPS_Speed_OnRmc(&st,&s,t,&pos)==GPS_RMC_ACCEPTED);}
    t+=1450;s.epoch_ms+=1500;assert(GPS_Speed_OnRmc(&st,&s,t,&pos)==GPS_RMC_ACCEPTED);
    assert(GPS_Speed_FreshLimit(&st)==150);                                    /* 偶发断流不抬高门限 */
    s.epoch_ms+=50;t+=50;assert(GPS_Speed_OnRmc(&st,&s,t,&pos)==GPS_RMC_ACCEPTED&&GPS_Speed_FreshLimit(&st)==150);
    for(unsigned i=0;i<2;i++){t+=400;s.epoch_ms+=400;(void)GPS_Speed_OnRmc(&st,&s,t,&pos);}
    assert(GPS_Speed_FreshLimit(&st)==150);                                    /* 连续两次慢间隔仍按断流处理 */
    GPS_Speed_Init(&st);s=sample(10000000UL,500);t=1000;
    for(unsigned i=0;i<40;i++,t+=1000) {s.epoch_ms+=1000;(void)GPS_Speed_OnRmc(&st,&s,t,&pos);}
    assert(GPS_Speed_FreshLimit(&st)==GPS_FRESH_MAX_MS);
    /* 有效性切换记录。 */
    GPS_Speed_Init(&st);s=sample(50000000UL,500);
    assert(GPS_Speed_Poll(&st,1000)==0);
    assert(GPS_Speed_OnRmc(&st,&s,1000,&pos)==GPS_RMC_ACCEPTED&&GPS_Speed_Poll(&st,1000)==1);
    assert(GPS_Speed_Poll(&st,1100)==0&&GPS_Speed_Poll(&st,1151)==1&&GPS_Speed_Poll(&st,1200)==0);
    assert(st.event_total==2&&st.events[0].valid==1&&st.events[1].valid==0&&
           st.events[1].reason==GPS_SPEED_REASON_STALE&&st.events[1].age_ms==151);
    puts("Speed validity: 150 ms freshness, V status, duplicate/backward epochs, bad field, midnight, tick wrap and adaptive limit passed");
}

static void test_gps_pipeline(void)
{
    GPS_Data_t d;
    gps_test_reset();
    g_pending_alert_count=0;
    /* 上电后到首条可信 RMC 之前：没有可信速度，界面保持 "--"（-1），不是 0。 */
    test_tick=30000;gps_service_once();
    GPS_GetData(&d);assert(!d.speed_valid&&d.speed_reason==GPS_SPEED_REASON_NO_DATA&&d.speed_kmh==0.0f&&!d.valid);
    rmc(43200000UL,'A',"38.88");
    assert(g_stats.rmc_accepted==1&&g_pending_speed==72&&g_stats.shown_speed_kmh==72&&g_stats.raw_speed_centi==7201);
    GPS_GetData(&d);assert(d.speed_valid&&d.speed_kmh>72.0f&&d.speed_kmh<72.02f&&d.speed_age_ms==0);
    /* 20 Hz 连续 1 s：一直有效，没有失效事件。 */
    for(unsigned i=1;i<=20;i++) {test_tick+=50;rmc(43200000UL+i*50U,'A',"38.88");gps_service_once();}
    assert(g_stats.rmc_accepted==21&&g_stats.speed_invalid_events==0&&g_gps_speed_st.event_total==1);
    assert(g_win.interval_min_ms==50&&g_win.interval_max_ms==50&&g_win.epoch_min_ms==50&&g_win.epoch_max_ms==50);
    /* 重复与倒退的历元被拒绝，不续期。 */
    rmc(43200000UL+20U*50U,'A',"38.88");assert(g_stats.rmc_dup_epoch==1&&g_stats.rmc_accepted==21);
    rmc(43200000UL+10U*50U,'A',"38.88");assert(g_stats.rmc_back_epoch==1&&g_stats.rmc_accepted==21);
    /* 状态 A 但速度为空：不当作 0 km/h，界面不变。 */
    test_tick+=50;rmc(43200000UL+21U*50U,'A','\0'?"":"");
    assert(g_stats.rmc_bad_field==1&&g_stats.rmc_accepted==21&&g_pending_speed==72);
    /* 接收机对 GPRMC 命令的回执含 "RMC"，不能当作 RMC 语句解析成 V 而使车速失效。 */
    unsigned no_fix=g_stats.rmc_no_fix;
    nmea("command,GPRMC 0.05,response: OK");
    assert(g_stats.n_reply==1&&g_stats.n_rmc==g_stats.rmc_accepted+g_stats.rmc_dup_epoch+g_stats.rmc_back_epoch+g_stats.rmc_bad_field+no_fix);
    assert(g_stats.rmc_no_fix==no_fix);
    gps_service_once();GPS_GetData(&d);assert(d.speed_valid);
    /* 断流超过 150 ms：界面立即显示 "--"，速度为 0 但 speed_valid 为 0。 */
    test_tick+=100;gps_service_once();assert(g_pending_speed==72);          /* 第 150 ms 仍有效（最后一条在 test_tick-150 前） */
    test_tick+=60;gps_service_once();
    assert(g_pending_speed==-1&&g_stats.shown_speed_kmh==-1&&g_stats.speed_invalid_events==1);
    GPS_GetData(&d);assert(!d.speed_valid&&d.speed_reason==GPS_SPEED_REASON_STALE&&d.speed_kmh==0.0f&&d.speed_age_ms>150);
    uint32_t seq=d.continuity_seq;
    /* 恢复：第一条新数据立即生效，连续性序号加一。 */
    test_tick+=50;rmc(44000000UL,'A',"38.88");
    GPS_GetData(&d);assert(d.speed_valid&&g_pending_speed==72&&d.continuity_seq==seq+1);
    /* 真实的 0 km/h 与"没有可信速度"不同：显示 0，speed_valid 为 1。 */
    test_tick+=50;rmc(44000050UL,'A',"0.00");
    GPS_GetData(&d);assert(d.speed_valid&&d.speed_kmh==0.0f&&g_pending_speed==0&&g_stats.shown_speed_kmh==0);
    /* 状态 V：立即失效，不等门限。 */
    test_tick+=50;rmc(44000100UL,'V',"0.00");gps_service_once();
    GPS_GetData(&d);assert(!d.speed_valid&&d.speed_reason==GPS_SPEED_REASON_NO_FIX&&g_pending_speed==-1);
    /* 校验和错误、行溢出、截断：只计数，不影响状态。 */
    unsigned bad=g_stats.checksum_fail;
    for(const char *p="$GNRMC,120000.00,A,3200.0,N,11800.0,E,1.0,0.0,181026,,,A,V*00\r\n";*p;p++)GPS_ProcessByte((uint8_t)*p);
    assert(g_stats.checksum_fail==bad+1);
    GPS_ProcessByte('$');for(unsigned i=0;i<200;i++)GPS_ProcessByte('A');assert(g_stats.line_overflow==1);
    GPS_ProcessByte('$');GPS_ProcessByte('G');GPS_ProcessByte('$');assert(g_stats.line_truncated==1);
    GPS_ProcessByte('\n');g_gps_line_active=0;g_gps_line_len=0;
    /* 位置与定位质量各有自己的过期：GGA 3.5 s，位置跟随 RMC 的新鲜度。 */
    test_tick+=50;rmc(44000150UL,'A',"1.00");
    nmea("GNGGA,120000.00,3200.0000,N,11800.0000,E,1,12,0.8,50.0,M,0,M,,");
    GPS_GetData(&d);assert(d.valid&&d.fix_quality==1&&d.satellites==12);
    uint32_t limit=GPS_Speed_FreshLimit(&g_gps_speed_st);                      /* 状态 V 拉长过一次间隔，门限略高于 150 ms */
    assert(limit>=150&&limit<=200);
    test_tick+=limit;gps_service_once();GPS_GetData(&d);assert(d.valid);
    test_tick+=1;gps_service_once();GPS_GetData(&d);assert(!d.valid&&d.fix_quality==1);   /* RMC 位置先过期 */
    test_tick+=3400;gps_service_once();GPS_GetData(&d);assert(d.fix_quality==0&&d.satellites==0&&d.signal_level==GPS_SIG_LEVEL_NONE);
    rmc(44000200UL,'A',"1.00");GPS_GetData(&d);assert(!d.valid);          /* GGA 过期后不会因 RMC 恢复而变回有定位 */
    nmea("GNGGA,120001.00,3200.0000,N,11800.0000,E,4,12,0.8,50.0,M,0,M,,");
    GPS_GetData(&d);assert(d.valid&&d.fix_quality==4);
    /* 白名单之外的语句与 '#' 日志：计数；只有查询期间才保存原始行。 */
    nmea("GNGSA,A,3,01,02,03,,,,,,,,,,1.0,1.0,1.0");assert(g_stats.n_other==1&&g_raw_count==0);
    g_raw_active=1;inject_line("#VERSION,97,GPS,FINE,2326,378237000,0,0,18,155;UM982,R4.10");
    nmea("GNGSA,A,3,04,05,06,,,,,,,,,,1.0,1.0,1.0");
    assert(g_raw_count==2&&!strncmp(g_raw_lines[0],"#VERSION",8)&&!strncmp(g_raw_lines[1],"$GNGSA",6));
    g_raw_active=0;inject_line("#MODE,81,GPS,FINE");assert(g_raw_count==2);
    puts("GPS pipeline: freshness, '--' vs real zero, V status, duplicate/bad-field rejection, reply isolation, GGA expiry and counters passed");
}

static void test_continuity(void)
{
    GPS_Data_t d;
    gps_test_reset();
    /* 里程：20 Hz、36 km/h 向北行驶，断流 1 s（小于旧版的 2 s 间隔门限）期间多走 10 m，不能被累计。 */
    g_odometer_initialized=0;gps_odometer_init();assert(g_odometer_initialized);
    uint32_t base=g_odometer_total_m;
    double lat=32.0,step=0.5/111195.0;
    uint32_t epoch=43200000UL;
    test_tick=100000;
    for(unsigned i=0;i<100;i++) {
        rmc_full(epoch,'A',"19.44",lat,118.0);gps_service_once();
        epoch+=50;test_tick+=50;lat+=step;
    }
    uint32_t before_gap=g_odometer_total_m-base;
    assert(before_gap>=47&&before_gap<=50);                                   /* 99 个间隔 × 0.5 m */
    lat+=20*step;epoch+=1000;test_tick+=1000;                                /* 断流 1 s，车辆继续前进 10 m */
    for(unsigned i=0;i<20;i++) {
        rmc_full(epoch,'A',"19.44",lat,118.0);gps_service_once();
        epoch+=50;test_tick+=50;lat+=step;
    }
    uint32_t total=g_odometer_total_m-base;
    assert(total>=56&&total<=61);                                             /* 约 50 + 9.5 m，不含断流期间的 10 m */
    /* 圈速与冲刺比较连续性序号；GPS_Data_t 由测试直接构造。 */
    #define LAT_M(m) ((float)(32.0+(m)/111195.0))
    GPS_Lap_Reset();GPS_Sprint_Reset();
    GPS_Lap_SetStartLine(32.0f,118.0f,0.0f);GPS_Lap_SetFinishLine(32.0f,118.0f,0.0f);
    memset(&d,0,sizeof(d));d.valid=1;d.latitude=LAT_M(0.0);d.longitude=118.0f;d.fix_quality=4;
    d.speed_valid=0;d.speed_kmh=0.0f;d.continuity_seq=1;
    /* 速度无效时的 0 不是停车：不允许进入冲刺准备。 */
    assert(GPS_Sprint_StartAtCurrent(&d,0.0f)==0&&!GPS_Sprint_IsActive());
    d.speed_valid=1;assert(GPS_Sprint_StartAtCurrent(&d,0.0f)==1&&GPS_Sprint_IsActive());
    GPS_SprintProcess(&d);
    /* 准备阶段中断，恢复后车辆已在运动：起步位置/时刻不可知，这次冲刺作废。 */
    test_tick+=2000;d.continuity_seq=2;d.speed_kmh=8.0f;d.latitude=LAT_M(5.0);
    g_pending_alert_count=0;GPS_SprintProcess(&d);
    assert(!GPS_Sprint_IsActive()&&alert_pending("gps lost, run void"));
    /* 准备阶段中断但车辆仍静止：只重建锚点，随后正常起步和完成；用时不受中断影响。 */
    GPS_Sprint_Reset();d.speed_kmh=0.0f;d.latitude=LAT_M(0.0);d.continuity_seq=2;
    assert(GPS_Sprint_StartAtCurrent(&d,0.0f)==1);
    GPS_SprintProcess(&d);test_tick+=3000;d.continuity_seq=3;GPS_SprintProcess(&d);
    assert(GPS_Sprint_IsActive()&&!g_sprint_started);
    test_tick+=100;d.speed_kmh=3.0f;d.latitude=LAT_M(1.0);GPS_SprintProcess(&d);assert(g_sprint_started);
    d.speed_kmh=40.0f;
    for(int m=10;m<=30;m+=10) {test_tick+=500;d.latitude=LAT_M(m);GPS_SprintProcess(&d);}
    assert(g_sprint_started&&g_sprint_gap_flag==0);
    /* 进行中断流：恢复后的第一个样本重建锚点（位移检查不跨中断），结果标记 gps gap。 */
    test_tick+=2000;d.continuity_seq=4;d.latitude=LAT_M(50.0);g_pending_alert_count=0;GPS_SprintProcess(&d);
    assert(g_sprint_started&&g_sprint_gap_flag==1&&g_sprint_active);
    for(int m=60;m<=70;m+=10) {test_tick+=500;d.latitude=LAT_M(m);GPS_SprintProcess(&d);}
    test_tick+=250;d.latitude=LAT_M(78.0);GPS_SprintProcess(&d);
    assert(!g_sprint_active&&g_lap_count==1&&alert_pending("75m done (gps gap)")&&g_sprint_gap_flag==0);
    /* 终点在断流期间越过：不能用中断前的旧位置插值，作废。 */
    GPS_Sprint_Reset();d.speed_kmh=0.0f;d.latitude=LAT_M(0.0);
    assert(GPS_Sprint_StartAtCurrent(&d,0.0f)==1);GPS_SprintProcess(&d);
    test_tick+=100;d.speed_kmh=3.0f;d.latitude=LAT_M(1.0);GPS_SprintProcess(&d);assert(g_sprint_started);
    d.speed_kmh=40.0f;test_tick+=500;d.latitude=LAT_M(20.0);GPS_SprintProcess(&d);
    test_tick+=2000;d.continuity_seq=5;d.latitude=LAT_M(90.0);g_pending_alert_count=0;GPS_SprintProcess(&d);
    assert(!g_sprint_active&&g_lap_count==0&&alert_pending("gps lost, run void"));

    /* 圈速：线在 lat=32.0，朝北。开始于线南 50 m。 */
    GPS_Lap_Reset();GPS_Lap_SetStartLine(32.0f,118.0f,0.0f);GPS_Lap_SetFinishLine(32.0f,118.0f,0.0f);
    d.speed_valid=1;d.speed_kmh=40.0f;d.continuity_seq=10;d.latitude=LAT_M(-50.0);
    GPS_Lap_SetAnalysisActive(1);assert(GPS_Lap_StartAtCurrent(&d)==1);
    test_tick+=500;d.latitude=LAT_M(-40.0);GPS_LapProcess(&d);
    /* 过线发生在断流期间：第一个恢复样本只建锚点，不判定过线，也不统计圈数。 */
    test_tick+=2000;d.continuity_seq=11;d.latitude=LAT_M(30.0);GPS_LapProcess(&d);
    assert(g_lap_count==0&&g_lap_gap_flag==1&&g_lap_prev_valid==1);
    /* 掉头后再次过线：这一圈用时不可信，只显示为上一圈，不成为最快圈。 */
    test_tick+=500;d.latitude=LAT_M(10.0);GPS_LapProcess(&d);
    test_tick+=500;d.latitude=LAT_M(-30.0);GPS_LapProcess(&d);
    test_tick+=500;g_pending_alert_count=0;d.latitude=LAT_M(20.0);GPS_LapProcess(&d);
    assert(g_lap_count==1&&g_lap_last_ms>0&&g_lap_best_ms==0&&g_lap_gap_flag==0&&alert_pending("lap time uncertain"));
    /* 没有中断的一圈照常成为最快圈。 */
    GPS_Lap_Reset();GPS_Lap_SetStartLine(32.0f,118.0f,0.0f);GPS_Lap_SetFinishLine(32.0f,118.0f,0.0f);
    d.latitude=LAT_M(-50.0);assert(GPS_Lap_StartAtCurrent(&d)==1);
    test_tick+=500;d.latitude=LAT_M(-20.0);GPS_LapProcess(&d);
    test_tick+=500;d.latitude=LAT_M(20.0);GPS_LapProcess(&d);
    assert(g_lap_count==1&&g_lap_best_ms>0&&g_lap_best_ms==g_lap_last_ms);
    /* 计时中定位失效：旧锚点作废，恢复后不跨这段判定过线，这一圈同样标记。 */
    d.latitude=LAT_M(-50.0);test_tick+=500;GPS_LapProcess(&d);
    d.valid=0;test_tick+=500;GPS_LapProcess(&d);assert(g_lap_prev_valid==0&&g_lap_gap_flag==1);
    GPS_Lap_SetAnalysisActive(0);GPS_Lap_Reset();GPS_Sprint_Reset();
    #undef LAT_M
    puts("Continuity: odometer, sprint READY/RUNNING gaps, void run, lap gap flag and position-loss anchors passed");
}

static void test_gps_link(void)
{
    gps_test_reset();
    /* USART3 错误：HAL 已中止 DMA 接收；回调统计并重启，任务侧丢弃旧读位置并从下标 0 解析。 */
    huart3.RxState=HAL_UART_STATE_READY;unsigned starts=test_rx_dma_starts;
    GPS_UartErrorCallback(HAL_UART_ERROR_FE|HAL_UART_ERROR_ORE);
    assert(g_stats.uart_err_fe==1&&g_stats.uart_err_ore==1&&g_stats.rx_restarts==1&&g_stats.rx_restart_fail==0);
    assert(test_rx_dma_starts==starts+1&&huart3.RxState==HAL_UART_STATE_BUSY_RX&&g_gps_dma_restarted);
    char line[160];unsigned sum=0;const char *body="GNRMC,130000.00,A,3200.0000,N,11800.0000,E,38.88,90.0,181026,,,A,V";
    for(const char *p=body;*p;p++)sum^=(unsigned char)*p;
    int n=snprintf(line,sizeof(line),"$%s*%02X\r\n",body,sum);
    memcpy(g_gps_dma_buf,line,(size_t)n);g_gps_dma_last_pos=300;test_dma_ndtr=512-(unsigned)n;
    test_tick=50000;GPS_SentenceProcess();
    assert(g_stats.rmc_accepted==1&&g_gps_dma_last_pos==(unsigned)n&&!g_gps_dma_restarted);
    assert(g_stats.dma_bytes==(unsigned)n&&g_stats.ring_pending_max==(unsigned)n);
    /* 回调丢失或重启失败时，任务在轮询里补救。 */
    huart3.RxState=HAL_UART_STATE_READY;starts=test_rx_dma_starts;test_tick+=10;GPS_SentenceProcess();
    assert(test_rx_dma_starts==starts+1&&g_stats.rx_restarts==2&&huart3.RxState==HAL_UART_STATE_BUSY_RX);
    /* 错误风暴：100 ms 内中断里最多重启 8 次，其余留给任务轮询。 */
    GPS_UartErrorCallback(0);                                                    /* 重新计窗，下面 12 次集中在同一个 100 ms 内 */
    test_tick+=1000;starts=test_rx_dma_starts;unsigned restarts=g_stats.rx_restarts;
    for(unsigned i=0;i<12;i++){huart3.RxState=HAL_UART_STATE_READY;GPS_UartErrorCallback(HAL_UART_ERROR_FE);}
    assert(test_rx_dma_starts==starts+8&&g_stats.rx_restarts==restarts+8&&g_stats.uart_err_fe>=12);
    huart3.RxState=HAL_UART_STATE_READY;test_tick+=10;GPS_SentenceProcess();    /* 任务补救一次 */
    assert(test_rx_dma_starts==starts+9&&huart3.RxState==HAL_UART_STATE_BUSY_RX);
    test_tick+=200;huart3.RxState=HAL_UART_STATE_READY;GPS_UartErrorCallback(HAL_UART_ERROR_FE);   /* 新窗口恢复中断内重启 */
    assert(test_rx_dma_starts==starts+10);
    /* 接收仍在进行时不重复启动。 */
    starts=test_rx_dma_starts;GPS_UartErrorCallback(HAL_UART_ERROR_PE|HAL_UART_ERROR_NE|HAL_UART_ERROR_DMA);
    assert(test_rx_dma_starts==starts&&g_stats.uart_err_pe==1&&g_stats.uart_err_ne==1&&g_stats.uart_err_dma==1);
    /* 一次在途窗口内的最大待读字节数与窗口极值。 */
    GPS_WindowReset();g_gps_poll_tick=test_tick;test_tick+=10;test_dma_ndtr=512-200;g_gps_dma_last_pos=0;GPS_SentenceProcess();
    GPS_Window_t w;GPS_GetWindow(&w);assert(w.ring_pending_max==200&&w.poll_gap_max_ms==10);
    test_dma_ndtr=512;g_gps_dma_last_pos=0;
    puts("GPS link: UART error recovery, DMA restart from ring start, task fallback and ring fill counters passed");
}

/* 模拟接收机：mode 0 按命令回 OK（查询命令另有 '#' 日志），1 不回，2 回错误。 */
static int test_rx_mode;
static void receiver_reply(const char *command)
{
    char body[120];
    if(test_rx_mode==1)return;
    if(!strcmp(command,"VERSION")) {inject_line("#VERSION,97,GPS,FINE,2326,378237000,0,0,18,155;UM982,R4.10Build11826,HRPT00-S10C-P*5A1B2C3D");return;}
    if(!strcmp(command,"MODE")) {inject_line("#MODE,81,GPS,FINE,2230,547967000,0,0,18,518;MODE ROVER AUTOMOTIVE,*1B");return;}
    snprintf(body,sizeof(body),"command,%s,response: %s",command,test_rx_mode==2?"PARSING FAILED NO MATCHING FUNC":"OK");
    nmea(body);
}
static void test_gps_config(void)
{
    gps_test_reset();
    test_tx_hook=receiver_reply;
    /* 接收机正常回执：UNLOG 不要求确认，其余命令一次成功；启动时不再发 SAVECONFIG。 */
    test_rx_mode=0;test_cmd_count=0;test_tick=60000;gps_cfg_run_boot();
    assert(test_cmd_count==8&&!strcmp(test_cmd_log[0],"UNLOG")&&!strcmp(test_cmd_log[4],"GPRMC 0.05"));
    for(unsigned i=0;i<test_cmd_count;i++)assert(strcmp(test_cmd_log[i],"SAVECONFIG")!=0);
    assert(g_cfg.count==8&&g_cfg.ack_seen&&!g_cfg.ack_unsupported&&!g_cfg.running);
    assert(g_cfg.item[0].state==GPS_CFG_UNCONFIRMED);
    for(unsigned i=1;i<8;i++)assert(g_cfg.item[i].state==GPS_CFG_OK&&g_cfg.item[i].attempts==1&&!strcmp(g_cfg.item[i].reply,"OK"));
    /* 接收机拒绝：记 REJECTED，不重发。 */
    test_rx_mode=2;test_cmd_count=0;gps_test_reset();test_tx_hook=receiver_reply;gps_cfg_run_boot();
    assert(g_cfg.item[1].state==GPS_CFG_REJECTED&&g_cfg.item[1].attempts==1&&strstr(g_cfg.item[1].reply,"PARSING FAILED"));
    assert(test_cmd_count==8);
    /* 无回执：每条重发两次（共 3 次）；连续两条无回执后放弃确认，其余命令只发一次、按固定时间等待。 */
    test_rx_mode=1;test_cmd_count=0;gps_test_reset();test_tx_hook=receiver_reply;
    uint32_t start=test_tick;gps_cfg_run_boot();
    assert(g_cfg.item[1].state==GPS_CFG_NOACK&&g_cfg.item[1].attempts==3);
    assert(g_cfg.item[2].state==GPS_CFG_NOACK&&g_cfg.item[2].attempts==3&&g_cfg.ack_unsupported&&!g_cfg.ack_seen);
    for(unsigned i=3;i<8;i++)assert(g_cfg.item[i].state==GPS_CFG_UNCONFIRMED&&g_cfg.item[i].attempts==1);
    assert(test_cmd_count==1+3+3+5);
    assert((test_tick-start)<9000);                                           /* 启动配置最坏耗时有界 */
    /* 迟到的上一条命令的回执不能算到当前命令上。 */
    gps_test_reset();g_cfg_pending_cmd="GPGGA 1";g_cfg_ack=GPS_CFG_ACK_NONE;
    nmea("command,GPRMC 0.05,response: OK");assert(g_cfg_ack==GPS_CFG_ACK_NONE);
    nmea("command,gpgga 1,response: OK");assert(g_cfg_ack==GPS_CFG_ACK_OK);
    g_cfg_pending_cmd=NULL;
    /* CLI 请求：查询收集 '#' 日志；改速率/保存走同一套确认；同时只接受一个请求。 */
    gps_test_reset();test_rx_mode=0;test_tx_hook=receiver_reply;test_cmd_count=0;
    assert(GPS_Request(GPS_REQ_QUERY)==1&&GPS_Request(GPS_REQ_SAVE)==0&&GPS_RequestPending()==GPS_REQ_QUERY);
    gps_request_service();
    assert(GPS_RequestPending()==GPS_REQ_NONE&&test_cmd_count==4&&!strcmp(test_cmd_log[3],"UNILOGLIST"));
    assert(g_raw_count>=2&&!strncmp(g_raw_lines[0],"#VERSION",8)&&!g_raw_active);
    assert(GPS_Request(GPS_REQ_RATE_10HZ)==1);gps_request_service();
    assert(g_cfg.request.state==GPS_CFG_OK&&!strcmp(g_cfg.request.cmd,"GPRMC 0.1"));
    assert(GPS_Request(GPS_REQ_SAVE)==1);gps_request_service();
    assert(g_cfg.request.state==GPS_CFG_OK&&!strcmp(g_cfg.request.cmd,"SAVECONFIG"));
    test_tx_hook=NULL;
    puts("GPS config: acknowledgements, rejection, no-ack fallback, late replies, bounded boot time and CLI requests passed");
}

/* ------------------------------------------------------------ CLI */
static void cli_tx_complete(void) {if(test_cli_tx_pending){test_cli_tx_pending=0;Diag_CLI_UartTxDone();}}
static void cli_out_clear(void) {test_cli_len=0;test_cli_out[0]=0;}
static void cli_type(const char *text) {for(;*text;text++){g_rx_byte=(uint8_t)*text;Diag_CLI_UartRxCplt();}}
static uint32_t cli_epoch;
static void cli_feed_20hz(unsigned t_ms)
{
    if((t_ms%50U)==0U) {rmc(cli_epoch,'A',"38.88");cli_epoch+=50;}
    gps_service_once();
}
static void cli_feed_silence(unsigned t_ms) {(void)t_ms;gps_service_once();}   /* 接收机断流，GPS 任务照常每 10 ms 检查 */
static void cli_pump(unsigned ms,void (*feed)(unsigned))
{
    for(unsigned t=0;t<ms;t+=10) {
        test_tick+=10;
        if(feed)feed(t);
        if(GPS_RequestPending()!=GPS_REQ_NONE)gps_request_service();
        Diag_CLI_Step(test_tick);
        cli_tx_complete();
    }
}
static double find_num(const char *text,const char *key)
{
    const char *p=strstr(text,key);
    return p?atof(p+strlen(key)):-1e9;
}
static unsigned count_char(const char *text,char c) {unsigned n=0;for(;*text;text++)n+=(*text==c);return n;}
/* 模拟 usart.c 的圈速差解析：以 + 或 - 开头的行算合法。 */
static char test_fallback_lines[8][64];
static unsigned test_fallback_count;
static uint8_t test_lap_delta_line(const char *line)
{
    while(*line==' ')line++;
    if(test_fallback_count<8)strncpy(test_fallback_lines[test_fallback_count],line,63);
    test_fallback_count++;
    return (uint8_t)(*line=='+'||*line=='-');
}
static void test_cli(void)
{
    gps_test_reset();test_tx_hook=receiver_reply;test_rx_mode=0;
    Diag_CLI_Init();assert(test_rx_it_arms>0);
    cli_out_clear();
    /* USART2 同时是圈速差输入：首个词不是命令的行交给回退处理；命令词（含 report -5 这类带负号的参数）只走 CLI。 */
    cli_type("+1.23\r");cli_pump(50,NULL);
    assert(test_fallback_count==0&&strstr(test_cli_out,"unknown command"));      /* 未注册回退时按未知命令处理 */
    Diag_CLI_SetLineFallback(test_lap_delta_line);
    cli_out_clear();cli_type("+1.23\r");cli_pump(50,NULL);
    assert(test_fallback_count==1&&!strcmp(test_fallback_lines[0],"+1.23")&&!strstr(test_cli_out,"error")&&g_cli_fallback_lines==1);
    cli_out_clear();cli_type("-0.5\r\n");cli_pump(50,NULL);
    assert(test_fallback_count==2&&!strcmp(test_fallback_lines[1],"-0.5")&&!strstr(test_cli_out,"error"));
    cli_out_clear();cli_type("reprot\r");cli_pump(50,NULL);                       /* 打错的命令：回退拒绝后提示 */
    assert(test_fallback_count==3&&strstr(test_cli_out,"unknown command"));
    cli_out_clear();cli_type("report -5\r");cli_pump(50,NULL);
    assert(test_fallback_count==3&&strstr(test_cli_out,"seconds must be a number"));
    cli_out_clear();cli_type("   \r");cli_pump(50,NULL);assert(test_fallback_count==3&&!strstr(test_cli_out,"error"));
    cli_out_clear();cli_type("helpme\r");cli_pump(50,NULL);assert(test_fallback_count==4&&strstr(test_cli_out,"unknown command"));
    cli_out_clear();
    /* 参数检查与行编辑。 */
    cli_type("report abc\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"error: seconds must be a number"));
    cli_out_clear();cli_type("report 0\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"seconds must be 1..60"));
    cli_out_clear();cli_type("report 61\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"seconds must be 1..60"));
    cli_out_clear();cli_type("gps rate 15\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"rate must be 10 or 20"));
    cli_out_clear();cli_type("gps capture 99\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"seconds must be 1..20"));
    cli_out_clear();cli_type("bogus\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"unknown command"));
    cli_out_clear();cli_type("help a b c d e\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"too many arguments"));
    cli_out_clear();cli_type("gps\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"usage: gps"));
    cli_out_clear();cli_type("hexx\b\blp\r");cli_pump(100,NULL);                /* 退格后是 help，CRLF 只算一次 */
    assert(strstr(test_cli_out,"report [N]")&&strstr(test_cli_out,"gps capture [N]"));
    cli_out_clear();cli_type("version\r\n");cli_pump(50,NULL);
    assert(strstr(test_cli_out,"hmi-display fw=")&&strstr(test_cli_out,"report_fmt=1")&&count_char(test_cli_out,'>')==1);
    /* 过长的行不会越界。 */
    cli_out_clear();for(unsigned i=0;i<10;i++)cli_type("0123456789");cli_type("\r");cli_pump(100,NULL);
    assert(strstr(test_cli_out,"unknown command"));
    /* gps status、sys status：瞬时输出。 */
    cli_epoch=43200000UL;test_tick+=100;rmc(cli_epoch,'A',"38.88");cli_epoch+=50;
    cli_out_clear();cli_type("gps status\r");cli_pump(100,NULL);
    assert(strstr(test_cli_out,"gps.status speed_valid=1 reason=ok")&&strstr(test_cli_out,"gps.status.link"));
    cli_out_clear();cli_type("sys status\r");cli_pump(100,NULL);
    assert(strstr(test_cli_out,"HMI-DIAG SYS")&&strstr(test_cli_out,"sys.task name=")&&strstr(test_cli_out,"sys.io can_rx_drop=")&&strstr(test_cli_out,"sys.cli rx_drop="));
    assert(count_char(test_cli_out,'\n')>=FW_DIAG_TASK_COUNT+5);

    /* report：采样 3 s，期间 20 Hz 正常输出；窗口内统计和报告各节齐全。 */
    cli_out_clear();test_tick+=100;rmc(cli_epoch,'A',"38.88");cli_epoch+=50;gps_service_once();
    cli_type("report 3\r");cli_pump(20,cli_feed_20hz);assert(strstr(test_cli_out,"sampling 3 s"));
    cli_out_clear();cli_type("help\r");cli_pump(20,cli_feed_20hz);assert(strstr(test_cli_out,"busy, type stop"));
    cli_out_clear();cli_pump(3300,cli_feed_20hz);
    assert(strstr(test_cli_out,"HMI-DIAG BEGIN fmt=1")&&strstr(test_cli_out,"HMI-DIAG END"));
    const char *keys[]={"build fw=","window start_ms=","gps.rmc rmc_total=","gps.rmc.rejected no_fix=","gps.nmea bytes=","gps.speed valid_now=",
        "gps.speed.value raw_kmh=","gps.link poll_gap_max_ms=","gps.uart pe=","gps.cfg ack_seen=","gps.event t_ms=","latency.submit_to_apply n=",
        "latency.apply_to_frame_done n=","latency.display loop_max_ms=","sys uptime_ms=","sys.task name=","sys.io can_rx_drop=","sys.cli rx_drop="};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++){if(!strstr(test_cli_out,keys[i])){fprintf(stderr,"missing report line: %s\n%s\n",keys[i],test_cli_out);abort();}}
    double rate=find_num(test_cli_out,"rate_hz=");assert(rate>19.0&&rate<21.0);
    assert(find_num(test_cli_out,"interval_ms_min=")>=20&&find_num(test_cli_out,"interval_ms_max=")<=60);
    assert(find_num(test_cli_out,"utc_step_ms_min=")==50&&find_num(test_cli_out,"valid_pct=")>=95.0);
    assert(find_num(test_cli_out,"dup_epoch=")==0&&find_num(test_cli_out,"back_epoch=")==0&&find_num(test_cli_out,"bad_field=")==0);
    assert(strstr(test_cli_out,"raw_kmh=72.01 shown_kmh=72")&&strstr(test_cli_out,"reason=ok")&&g_job.kind==JOB_IDLE);
    assert(find_num(test_cli_out,"limit_ms=")==150&&g_tx_drop_bytes==0);
    assert(count_char(test_cli_out,'>')==1);                                   /* 结束后只有一个提示符 */

    /* stop 和 Ctrl-C 终止采样；采样被中止后不输出报告。 */
    cli_out_clear();cli_type("report 30\r");cli_pump(100,cli_feed_20hz);cli_type("stop\r");cli_pump(50,cli_feed_20hz);
    assert(strstr(test_cli_out,"stopped")&&!strstr(test_cli_out,"HMI-DIAG BEGIN")&&g_job.kind==JOB_IDLE);
    cli_out_clear();cli_type("report 30\r");cli_pump(100,cli_feed_20hz);cli_type("\x03");cli_pump(50,cli_feed_20hz);
    assert(strstr(test_cli_out,"stopped")&&g_job.kind==JOB_IDLE);
    /* 命令执行中圈速差输入照常处理，不被 busy 挡住。 */
    cli_out_clear();cli_type("report 5\r");cli_pump(50,cli_feed_20hz);
    unsigned fallback_before=test_fallback_count;
    cli_type("+0.42\r");cli_pump(50,cli_feed_20hz);
    assert(test_fallback_count==fallback_before+1&&!strstr(test_cli_out,"busy")&&g_job.kind==JOB_SAMPLE);
    cli_type("stop\r");cli_pump(50,cli_feed_20hz);assert(g_job.kind==JOB_IDLE);

    /* 速度失效、重复历元、断流都会出现在报告里。 */
    cli_out_clear();cli_type("report 3\r");cli_pump(500,cli_feed_20hz);
    rmc(cli_epoch-50,'A',"38.88");                                              /* 重复历元 */
    cli_pump(600,cli_feed_silence);                                             /* 断流 600 ms */
    cli_pump(2300,cli_feed_20hz);
    assert(find_num(test_cli_out,"dup_epoch=")==1&&strstr(test_cli_out,"gps.event t_ms="));
    assert(strstr(test_cli_out,"to=invalid reason=stale")&&strstr(test_cli_out,"to=valid reason=ok"));
    assert(find_num(test_cli_out,"invalid_events=")==1&&find_num(test_cli_out,"age_max_ms=")>=600);
    assert(find_num(test_cli_out,"valid_pct=")<90.0);

    /* gps capture：逐条 CSV，含重复历元和失效事件。 */
    cli_out_clear();cli_type("gps capture 3\r");cli_pump(500,cli_feed_20hz);
    rmc(cli_epoch-50,'A',"38.88");cli_pump(400,cli_feed_silence);cli_pump(2300,cli_feed_20hz);
    assert(strstr(test_cli_out,"HMI-DIAG GPS-CAPTURE BEGIN")&&strstr(test_cli_out,"GPS-CAPTURE END")&&strstr(test_cli_out,"idx,tick_ms,utc_ms,raw_kmh,shown_kmh,verdict,valid"));
    assert(strstr(test_cli_out,",dup_epoch,")&&strstr(test_cli_out,",invalid:stale,0")&&strstr(test_cli_out,",accepted,1"));
    unsigned records=(unsigned)find_num(test_cli_out,"records=");
    assert(records==GPS_CaptureCount()&&records>=50);
    assert(count_char(test_cli_out,'\n')==records+6);                         /* 命令回显、提示行、BEGIN、列名、记录、END、结束空行 */

    /* 发送拥堵：UART 不完成发送时 CLI 任务停在发送环满处，不丢内容、不阻塞其他任务；恢复后完整输出。 */
    cli_out_clear();cli_type("report 1\r");
    for(unsigned i=0;i<400&&!strstr(test_cli_out,"HMI-DIAG BEGIN");i++)cli_pump(10,cli_feed_20hz);
    assert(strstr(test_cli_out,"HMI-DIAG BEGIN"));
    for(unsigned i=0;i<50;i++){test_tick+=10;cli_feed_20hz(0);Diag_CLI_Step(test_tick);}   /* 不调用 cli_tx_complete */
    assert(g_job.kind==JOB_PRINT&&!strstr(test_cli_out,"HMI-DIAG END")&&tx_free()<(CLI_OUT_LINE+4U));
    cli_pump(2000,cli_feed_20hz);
    assert(strstr(test_cli_out,"HMI-DIAG END")&&g_job.kind==JOB_IDLE&&g_tx_drop_bytes==0);
    /* 回显在发送环已满时丢弃并计数（命令输出不丢）。 */
    unsigned dropped=g_tx_drop_bytes;
    cli_out_clear();for(unsigned i=0;i<1100;i++)tx_put("x",1);assert(g_tx_drop_bytes>dropped);
    cli_pump(500,NULL);

    /* gps config / rate / save 经 GPS 任务执行；启动配置的回执结果也在其中。 */
    gps_cfg_run_boot();
    cli_out_clear();cli_type("gps config\r");cli_pump(200,cli_feed_20hz);cli_pump(600,NULL);
    assert(strstr(test_cli_out,"HMI-DIAG GPS-CONFIG BEGIN")&&strstr(test_cli_out,"gps.cfg.cmd i=1 cmd=\"MODE ROVER AUTOMOTIVE\" state=ok"));
    assert(strstr(test_cli_out,"gps.raw i=0 #VERSION")&&strstr(test_cli_out,"HMI-DIAG GPS-CONFIG END"));
    cli_out_clear();cli_type("gps rate 10\r");cli_pump(600,NULL);
    assert(strstr(test_cli_out,"gps.request cmd=\"GPRMC 0.1\" state=ok attempts=1 reply=\"OK\""));
    cli_out_clear();cli_type("gps save\r");cli_pump(900,NULL);
    assert(strstr(test_cli_out,"gps.request cmd=\"SAVECONFIG\" state=ok"));
    cli_out_clear();cli_type("gps rate 20\r");cli_pump(600,NULL);assert(strstr(test_cli_out,"cmd=\"GPRMC 0.05\" state=ok"));
    cli_out_clear();cli_type("gps smooth on\r");cli_pump(600,NULL);assert(strstr(test_cli_out,"cmd=\"CONFIG SMOOTH PSRVEL enable\" state=ok"));
    cli_out_clear();cli_type("gps smooth off\r");cli_pump(600,NULL);assert(strstr(test_cli_out,"cmd=\"CONFIG SMOOTH PSRVEL disable\" state=ok"));
    cli_out_clear();cli_type("gps smooth maybe\r");cli_pump(50,NULL);assert(strstr(test_cli_out,"smooth must be on or off"));

    /* RX 线路噪声：连续错误过多时停止接收，1 s 后恢复，不长期占用 CPU。 */
    unsigned arms=test_rx_it_arms,aborts=test_abort_calls;
    for(unsigned i=0;i<70;i++)Diag_CLI_UartError(HAL_UART_ERROR_FE);
    assert(g_rx_stalled&&test_abort_calls==aborts+1&&test_rx_it_arms==arms+64);
    test_tick+=500;Diag_CLI_Step(test_tick);assert(g_rx_stalled);
    test_tick+=600;Diag_CLI_Step(test_tick);assert(!g_rx_stalled&&test_rx_it_arms==arms+65);
    g_rx_err_streak=0;
    /* 接收环写满只丢字节并计数。 */
    unsigned rx_drop=g_rx_drop;for(unsigned i=0;i<200;i++)cli_type("a");assert(g_rx_drop>rx_drop);
    Diag_CLI_Step(test_tick+10);cli_type("\r");cli_pump(50,NULL);
    test_tx_hook=NULL;
    puts("CLI: argument checks, line editing, report/capture content, stop, busy, TX congestion, GPS requests and RX noise guard passed");
}
static void ui_pass(void) {test_tick+=20;Dashboard_UI_Process();lv_refr_now(NULL);}
static void test_ui_speed(void)
{
    GPS_Data_t d;
    /* 上电：没有可信速度时显示 "--"（不是 0）；真实的 0 km/h 与 "--" 是两种状态。 */
    assert(g_speed==DASH_VIEW_INVALID);
    Dashboard_UI_SubmitSpeed(0);ui_pass();
    assert(g_speed==0&&Dashboard_UI_GetCurrentData()->speed==0);
    Dashboard_UI_SubmitSpeedInvalid();ui_pass();
    assert(g_speed==DASH_VIEW_INVALID&&Dashboard_UI_GetCurrentData()->speed==-1);
    Dashboard_UI_SubmitSpeed(87);ui_pass();assert(g_speed==87);
    /* 失效后重复提交失效不重绘；覆盖未应用的数值只计一次。 */
    SpeedTrace_Reset();
    Dashboard_UI_SubmitSpeed(88);Dashboard_UI_SubmitSpeed(89);ui_pass();
    speed_trace_t trace;SpeedTrace_Get(&trace);
    assert(g_speed==89&&trace.superseded==1&&trace.submit_to_apply.count==1&&trace.submit_to_apply.last_ms<=20);
    SpeedTrace_FrameDone(test_tick+7);SpeedTrace_Get(&trace);
    assert(trace.apply_to_frame.count==1&&trace.apply_to_frame.last_ms==7);
    SpeedTrace_FrameDone(test_tick+9);SpeedTrace_Get(&trace);assert(trace.apply_to_frame.count==1);   /* 只计应用后的第一帧 */
    Dashboard_UI_SubmitSpeed(89);ui_pass();SpeedTrace_Get(&trace);assert(trace.submit_to_apply.count==1);   /* 数值没变不统计 */

    /* 速度过期但位置仍新鲜：不能当成停车来准备冲刺；没有双天线航向时圈速也需要可信速度。 */
    gps_test_reset();g_pending_alert_count=0;
    test_tick=200000;
    nmea("GNGGA,120000.00,3200.0000,N,11800.0000,E,1,12,0.8,50.0,M,0,M,,");
    for(unsigned i=0;i<10;i++) {rmc(43200000UL+i*50U,'A',"0.00");test_tick+=50;}
    for(unsigned i=10;i<14;i++) {rmc(43200000UL+i*50U,'A',"");test_tick+=50;gps_service_once();}   /* 速度字段缺失，位置照常 */
    GPS_GetData(&d);assert(d.valid&&!d.speed_valid&&d.speed_kmh==0.0f&&d.signal_level>GPS_SIG_LEVEL_NONE);
    g_drive_mode=DRIVE_MODE_ACCELERATION;g_pending_alert_count=0;
    dashboard_toggle_lap_analysis();
    assert(alert_pending("gps speed invalid")&&!GPS_Sprint_IsActive());
    g_drive_mode=DRIVE_MODE_AUTOCROSS;g_pending_alert_count=0;
    dashboard_toggle_lap_analysis();
    assert(alert_pending("gps speed invalid")&&!g_lap_analysis_active);
    /* 双天线航向有效时圈速不需要速度，但 AC 模式冲刺仍必须有可信速度。 */
    nmea("GPHPR,120000,123.4,0,0,4");GPS_GetData(&d);assert(d.heading_valid);
    g_drive_mode=DRIVE_MODE_ACCELERATION;g_pending_alert_count=0;
    dashboard_toggle_lap_analysis();
    assert(alert_pending("gps speed invalid")&&!GPS_Sprint_IsActive());
    /* 速度恢复后可以准备冲刺。 */
    rmc(43200000UL+14U*50U,'A',"0.00");
    g_pending_alert_count=0;
    dashboard_toggle_lap_analysis();
    assert(GPS_Sprint_IsActive()&&!alert_pending("gps speed invalid"));
    GPS_Sprint_Cancel();g_lap_analysis_active=0;g_pending_alert_count=0;
    GPS_Lap_Reset();GPS_Sprint_Reset();
    puts("UI speed: '--' vs real 0, invalid state, supersede/latency trace and sprint/lap start gating on speed validity passed");
}
/* 驾驶模式色块：五档轮转、200 ms 长按门限、单次触发和计时锁。 */
static void start_mode_hold(void)
{
    Dashboard_UI_SubmitTouchState(56,106,0);
    test_tick+=100;Dashboard_UI_SubmitTouchState(56,106,1);
    assert(g_pending_mode_cycle==0U);
}
static void hold_mode_tile(void)
{
    start_mode_hold();
    test_tick+=199;Dashboard_UI_SubmitTouchState(56,106,1);
    assert(g_pending_mode_cycle==0U);
    test_tick++;Dashboard_UI_SubmitTouchState(56,106,1);
    assert(g_pending_mode_cycle==1U);
    test_tick+=60;Dashboard_UI_Process();
    Dashboard_UI_SubmitTouchState(56,106,0);
}

static void test_mode_touch(void)
{
    static const int32_t modes[5]={1,2,3,-1,0};
    static const uint8_t expected[5]={'1','2','3','/','0'};
    unsigned mode_tx;

    assert(Dash_View_HitTest(56,106)==DASH_ZONE_MODE);
    assert(Dash_View_HitTest(6,56)==DASH_ZONE_MODE);       /* 外扩 8 px 的左上角 */
    assert(Dash_View_HitTest(5,106)==DASH_ZONE_NONE);      /* 色块外 9 px */
    assert(Dash_View_HitTest(56,55)==DASH_ZONE_NONE);      /* 顶栏与色块之间 */

    Dashboard_UI_RequestAlertClear();
    Dashboard_UI_SubmitDriveMode(0);
    test_tick+=300;Dashboard_UI_Process();
    g_lap_analysis_active=0U;g_mode_lock_alert_last_tick=0U;
    assert(g_drive_mode==DRIVE_MODE_ACCELERATION);

    mode_tx=test_tx_id_count[0x310];
    for(unsigned i=0;i<5;i++) {
        hold_mode_tile();
        assert(g_drive_mode==(drive_mode_t)modes[i]);
        assert(g_mode_index==modes[i]);
        assert(test_tx_id_count[0x310]==++mode_tx);
        assert(test_tx_id_data[0x310][0]==expected[i]);
        assert(test_tx_id_data[0x310][1]==0U&&test_tx_header.DLC==8U);
    }

    /* 新的 0x310 尚未应用时，ECU 回报的旧模式不能撤销这次请求。 */
    Dashboard_UI_SubmitDriveMode(3);
    Dashboard_UI_SubmitDriveMode(0);
    test_tick+=300;Dashboard_UI_Process();
    assert(g_drive_mode==DRIVE_MODE_ENDURANCE&&g_mode_index==3);
    Dashboard_UI_SubmitDriveMode(0);test_tick+=300;Dashboard_UI_Process();
    assert(g_drive_mode==DRIVE_MODE_ACCELERATION&&g_mode_index==0);

    /* 方向盘连续转回旧档位时，最后一次用户请求仍要覆盖待应用的 E。 */
    Dashboard_UI_RequestDriveMode(3);
    Dashboard_UI_RequestDriveMode(0);
    test_tick+=300;Dashboard_UI_Process();
    assert(g_drive_mode==DRIVE_MODE_ACCELERATION&&g_mode_index==0);

    /* 方向盘默认请求尚未应用时点击：按待应用目标轮转，不能丢掉 AC 请求。 */
    Dashboard_UI_SubmitDriveMode(-1);
    mode_tx=test_tx_id_count[0x310];
    hold_mode_tile();
    assert(test_tx_id_count[0x310]==mode_tx+1U&&test_tx_id_data[0x310][0]=='0');
    assert(g_drive_mode==DRIVE_MODE_ACCELERATION&&g_mode_index==0);

    /* 按下后 199 ms 松手，不切模式、不发送；松手后也不能补发。 */
    mode_tx=test_tx_id_count[0x310];
    start_mode_hold();
    test_tick+=199;Dashboard_UI_SubmitTouchState(56,106,1);
    assert(g_pending_mode_cycle==0U);
    Dashboard_UI_SubmitTouchState(56,106,0);
    test_tick+=100;Dashboard_UI_Process();
    assert(test_tx_id_count[0x310]==mode_tx&&g_drive_mode==DRIVE_MODE_ACCELERATION);
    hold_mode_tile();
    assert(test_tx_id_count[0x310]==mode_tx+1U&&g_drive_mode==DRIVE_MODE_AUTOCROSS);

    /* 非 AC 模式圈速分析进行中：拒绝轮转、不改界面、不发送并弹模式锁告警。 */
    Dashboard_UI_RequestAlertClear();test_tick+=300;Dashboard_UI_Process();
    Dashboard_UI_SubmitDriveMode(1);test_tick+=300;Dashboard_UI_Process();
    g_lap_analysis_active=1U;g_mode_lock_alert_last_tick=0U;
    mode_tx=test_tx_id_count[0x310];
    hold_mode_tile();
    assert(test_tx_id_count[0x310]==mode_tx&&g_drive_mode==DRIVE_MODE_AUTOCROSS);
    assert(g_alert_count>0U&&!strcmp(g_alert_queue[g_alert_count-1U],"MODE LOCK: TIMER ACTIVE"));
    g_lap_analysis_active=0U;

    /* 越界模式不能进入发送队列。 */
    mode_tx=test_tx_id_count[0x310];
    assert(CAN_SendDriveModeRequest(4U)==0U&&test_tx_id_count[0x310]==mode_tx);
    assert(CAN_SendDriveModeRequest(-2)==0U&&test_tx_id_count[0x310]==mode_tx);
    puts("UI mode tile: hit zone, DF/AC/AX/8/EN wrap, 0x310 payload, 199/200 ms hold threshold, pending-request ordering and timing lock passed");
}

static void test_mode_touch_release(void)
{
    Dashboard_UI_SubmitDriveMode(3);
    test_tick+=300;Dashboard_UI_Process();
    unsigned mode_tx=test_tx_id_count[0x310];

    for(int32_t mode=-1;mode<4;mode++) {
        hold_mode_tile();
        assert(g_mode_index==mode&&test_tx_id_count[0x310]==++mode_tx);
        assert(test_tx_id_data[0x310][0]==(uint8_t)(48+mode));

        /* 上一个 helper 已上报松手；不足 80 ms 的丢点不能让长按重新布防。 */
        test_tick+=10;Dashboard_UI_SubmitTouchState(56,106,1);
        for(unsigned sample=0;sample<20;sample++) {
            test_tick+=50;Dashboard_UI_SubmitTouchState(56,106,1);
            Dashboard_UI_Process();
            assert(g_mode_index==mode&&test_tx_id_count[0x310]==mode_tx);
        }
        for(unsigned bounce=0;bounce<3;bounce++) {
            Dashboard_UI_SubmitTouchState(56,106,0);
            test_tick+=10;Dashboard_UI_SubmitTouchState(56,106,1);
            test_tick+=250;Dashboard_UI_SubmitTouchState(56,106,1);Dashboard_UI_Process();
            assert(g_mode_index==mode&&test_tx_id_count[0x310]==mode_tx);
        }

        /* 连续松手 79 ms 不重新布防，重复松手采样不重置确认起点。 */
        Dashboard_UI_SubmitTouchState(56,106,0);
        test_tick+=40;Dashboard_UI_SubmitTouchState(56,106,0);
        test_tick+=39;Dashboard_UI_SubmitTouchState(56,106,1);
        test_tick+=250;Dashboard_UI_SubmitTouchState(56,106,1);Dashboard_UI_Process();
        assert(g_mode_index==mode&&test_tx_id_count[0x310]==mode_tx);
    }

    /* 连续松手恰好 80 ms 后仍须重新按满 200 ms，才允许 EN→DF。 */
    Dashboard_UI_SubmitTouchState(56,106,0);
    test_tick+=80;Dashboard_UI_SubmitTouchState(56,106,1);
    test_tick+=199;Dashboard_UI_SubmitTouchState(56,106,1);assert(g_pending_mode_cycle==0U);
    test_tick++;Dashboard_UI_SubmitTouchState(56,106,1);
    test_tick+=60;Dashboard_UI_Process();
    assert(g_mode_index==-1&&test_tx_id_count[0x310]==++mode_tx);

    /* 达门限前丢点取消本次长按，不能累计松手前后的时间。 */
    start_mode_hold();
    test_tick+=100;Dashboard_UI_SubmitTouchState(56,106,0);
    test_tick+=10;Dashboard_UI_SubmitTouchState(56,106,1);
    test_tick+=250;Dashboard_UI_SubmitTouchState(56,106,1);Dashboard_UI_Process();
    assert(g_mode_index==-1&&test_tx_id_count[0x310]==mode_tx);

    /* 移出色块或从别处滑入均不能切模式，必须松手后重新长按。 */
    start_mode_hold();
    test_tick+=100;Dashboard_UI_SubmitTouchState(180,200,1);
    test_tick+=100;Dashboard_UI_SubmitTouchState(56,106,1);Dashboard_UI_Process();
    assert(test_tx_id_count[0x310]==mode_tx);
    Dashboard_UI_SubmitTouchState(56,106,0);
    test_tick+=100;Dashboard_UI_SubmitTouchState(180,200,1);
    test_tick+=250;Dashboard_UI_SubmitTouchState(56,106,1);Dashboard_UI_Process();
    assert(test_tx_id_count[0x310]==mode_tx);
    hold_mode_tile();assert(g_mode_index==0&&test_tx_id_count[0x310]==++mode_tx);

    /* 200 ms 判定跨 HAL 毫秒计数回绕仍只触发一次。 */
    uint32_t saved_tick=test_tick;
    test_tick=UINT32_MAX-100U;Dashboard_UI_SubmitTouchState(56,106,1);
    test_tick+=199;Dashboard_UI_SubmitTouchState(56,106,1);assert(g_pending_mode_cycle==0U);
    test_tick++;Dashboard_UI_SubmitTouchState(56,106,1);assert(g_pending_mode_cycle==1U);
    test_tick=saved_tick+400U;Dashboard_UI_Process();
    assert(g_mode_index==1&&test_tx_id_count[0x310]==mode_tx+1U);
    Dashboard_UI_SubmitTouchState(56,106,0);
    puts("UI mode hold: five modes, hold/release boundaries, no repeat, interrupted hold, slide cancellation and tick wrap passed");
}

static void test_default_mode(void)
{
    unsigned mode_tx=test_tx_id_count[0x310];
    const uint8_t default_frame[5]={0xF1,0,0xF0,0,0};
    inject(0x509,5,default_frame);test_tick+=300;Dashboard_UI_Process();
    assert(g_can_dashboard_data.mode_index==-1&&g_mode_index==-1);
    assert(g_drive_mode==DRIVE_MODE_DEFAULT&&Dashboard_UI_GetCurrentData()->mode_index==-1);
    assert(g_laps_left==0&&test_tx_id_count[0x310]==mode_tx);

    /* 非模式遥测不能覆盖默认模式；未定义回报值也不能冒充 AC。 */
    dashboard_data_t data=*Dashboard_UI_GetCurrentData();
    data.mode_index=0;Dashboard_UI_SubmitData(&data);
    Dashboard_UI_SubmitDriveMode(-2);Dashboard_UI_SubmitDriveMode(4);
    test_tick+=300;Dashboard_UI_Process();assert(g_mode_index==-1);

    /* 默认模式首次触摸请求 AC，重复的旧模式回报不撤销尚未应用的请求。 */
    hold_mode_tile();
    assert(g_mode_index==0&&test_tx_id_count[0x310]==mode_tx+1U);
    assert(test_tx_id_data[0x310][0]=='0');
    Dashboard_UI_RequestDriveMode(1);Dashboard_UI_SubmitDriveMode(0);
    test_tick+=300;Dashboard_UI_Process();assert(g_mode_index==1);

    /* 圈速计时锁仍拦截默认回报，避免中途切换计时类型。 */
    g_lap_analysis_active=1U;Dashboard_UI_SubmitDriveMode(-1);
    test_tick+=300;Dashboard_UI_Process();assert(g_mode_index==1);
    g_lap_analysis_active=0U;
    Dashboard_UI_SubmitDriveMode(-1);test_tick+=300;Dashboard_UI_Process();
    assert(g_mode_index==-1&&g_drive_mode==DRIVE_MODE_DEFAULT);
    /* 方向盘/网关发来的默认模式请求也按原 ID 和 DLC 转发。 */
    mode_tx=test_tx_id_count[0x310];
    Dashboard_UI_RequestDriveMode(3);test_tick+=300;Dashboard_UI_Process();
    inject_wheel(0x310,8,47);test_tick+=300;Dashboard_UI_Process();
    assert(g_mode_index==-1&&test_tx_id_count[0x310]==mode_tx+1U);
    assert(test_tx_id_data[0x310][0]==47&&test_tx_header.DLC==8U);

    /* 邮箱阻塞并填满队列时，EN→DF 请求失败不得先改显示；恢复后再次点击可切换。 */
    Dashboard_UI_RequestDriveMode(3);test_tick+=300;Dashboard_UI_Process();
    test_can_free_mailboxes=0;
    for(unsigned i=0;i<USER_CAN_TX_QUEUE_CAPACITY;i++) {
        assert(User_CAN_TxQueuePush(0x703,(const uint8_t[]){0,0},2));
    }
    unsigned drops=g_user_can_tx_drop_count;
    hold_mode_tile();
    assert(g_mode_index==3&&g_user_can_tx_drop_count==drops+1U);
    assert(test_tx_id_count[0x310]==mode_tx+1U);
    test_can_free_mailboxes=1;User_CAN_TxQueueDrain();
    hold_mode_tile();
    assert(g_mode_index==-1&&test_tx_id_count[0x310]==mode_tx+2U);
    lv_refr_now(NULL);
    puts("UI default mode: signed 0x509 feedback, invalid modes, telemetry isolation, request relay, timing lock and TX queue recovery passed");
}
/* Per-motor PK is the highest power since power-on: lifting off, zero torque or regen never clears it. */
static void test_motor_peak(void)
{
    static const int32_t torques[]={20,5,0,-3,30,10};
    dashboard_data_t data=*Dashboard_UI_GetCurrentData();
    int32_t expected=g_motor_power_peak[0];
    data.valid_mask=TEST_ALL_VALID;data.rpm[0]=10000;
    for(unsigned i=0;i<sizeof(torques)/sizeof(torques[0]);i++) {
        const int32_t power=torques[i]*10000/955;          /* 0.1 kW, as in the UI */
        data.torque[0]=torques[i];
        Dashboard_UI_SubmitData(&data);test_tick+=300;Dashboard_UI_Process();
        if(power>expected)expected=power;
        assert(g_motor_power_peak[0]==expected);
    }
    assert(g_motor_power_peak[0]>=314);                    /* 30 Nm x 10000 rpm = 31.4 kW */
}
static void test_ui(void)
{
    const uint8_t pack[7]={0x15,0x73, 0xFC,0xA7, 77, 0x1F, 0x50};
    inject(0x4B0,7,pack);
    Dashboard_UI_RequestAlertClear();test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);
    /* Re-submitting unchanged values must not invalidate anything. */
    const dashboard_data_t *d=Dashboard_UI_GetCurrentData();
    flushes=0;Dash_View_SetBattery(d->soc,d->sum_voltage,d->sum_current);
    for(unsigned wheel=0;wheel<4;wheel++)Dash_View_SetTire(wheel,g_tire_temp[wheel]);
    lv_refr_now(NULL);assert(flushes==0);
    /* 慢组每 200 ms 才应用一次：固定节拍相位，使最后一轮（i=399）恰落在应用时刻，断言不依赖之前用例推进了多少毫秒。 */
    g_dashboard_slow_ui_last_tick=test_tick;g_dashboard_fast_ui_last_tick=test_tick;
    for(unsigned i=0;i<400;i++) {
        dashboard_data_t data=*Dashboard_UI_GetCurrentData();
        data.valid_mask=TEST_ALL_VALID;data.soc=i%101;data.sum_voltage=400;data.sum_current=i%100;
        data.sum_voltage_dv=4000;data.sum_current_da=-(int32_t)(i%100)*10;
        data.aps_open_pct=i%101;data.brake_pct=(i+10)%101;data.regen_pct=(i+20)%101;
        data.diag_num[i%4]=(i%5==0)?0x231:0;data.inverter_temp[i%4]=(int32_t)(i%120);
        data.lv_bus_current_cA=-(int32_t)i*10;data.fan_rpm[i%3]=(int32_t)i*10;
        data.sop_discharge_power_dkw=(int32_t)(i%800);data.sop_charge_power_dkw=(int32_t)(i%80);
        data.sop_discharge_current_da=(int32_t)i*5;data.sop_charge_current_da=(int32_t)(i%80);
        Dashboard_UI_SubmitData(&data);Dashboard_UI_SubmitSpeed(i%300);
        Dashboard_UI_SubmitDriveMode(i%4);Dashboard_UI_SubmitSlipLevel(i%8);
        Dashboard_UI_PushAlert("TEST ALERT");if(i%3==0)Dashboard_UI_RequestAlertClear();
        test_tick+=100;Dashboard_UI_Process();lv_refr_now(NULL);
        assert(test_primask==0&&test_mutex_depth==0);
    }
    /* Inverter temperature, diagnostics, LV bus and fans reach the display copy. */
    assert(Dashboard_UI_GetCurrentData()->lv_bus_current_cA==-3990);
    /* Speed is not throttled: each GNSS value shows on the very next pass. */
    for(int32_t kmh=40;kmh<46;kmh++) {
        Dashboard_UI_SubmitSpeed(kmh);test_tick+=10;Dashboard_UI_Process();
        assert(g_speed==kmh&&Dashboard_UI_GetCurrentData()->speed==kmh);
    }
    lv_mem_monitor_t m;lv_mem_monitor(&m);assert(m.free_size>1024);
    printf("UI: 400 telemetry/mode/alert cycles, identical-value skip and 120 KiB host pool passed (peak %zu, free %zu)\n",m.max_used,m.free_size);
}
/* Representative telemetry for screenshots: one frame per drive mode plus an
 * alert frame (mode-4), the S-mode READY banner (mode-5) and the no-data frame (mode-6). */
static void save_frame(const char *directory,unsigned index)
{
    char path[1024];snprintf(path,sizeof(path),"%s/mode-%u.rgb565",directory,index);
    FILE *file=fopen(path,"wb");assert(file);assert(fwrite(frame_pixels,2,800*480,file)==800*480);fclose(file);
}
static void render_modes(const char *directory)
{
    static const int32_t tires[4][4]={{7200,7600,8100,8400},{6900,7300,7700,8000},{7400,7900,8600,9300},{6600,7000,7300,7500}};
    /* mode-6: the power-on frame, every CAN field still "--" (nothing received yet). */
    save_frame(directory,6);
    dashboard_data_t data=*Dashboard_UI_GetCurrentData();
    data.valid_mask=TEST_ALL_VALID;data.soc=76;data.sum_voltage=386;data.sum_current=-118;
    data.sum_voltage_dv=3860;data.sum_current_da=-1180;data.max_temperature=74;
    data.aps_open_pct=62;data.brake_pct=0;data.regen_pct=38;
    data.lv_bus_voltage_mV=13820;data.lv_bus_current_cA=-1240;data.lv_bus_power_dW=1712;
    data.fan_rpm[0]=2376;data.fan_rpm[1]=2316;data.fan_rpm[2]=2196;data.fan_pwm_duty[0]=49;data.fan_pwm_duty[1]=49;
    data.sop_discharge_power_dkw=740;data.sop_charge_power_dkw=40;data.sop_discharge_current_da=1800;data.sop_charge_current_da=80;
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
    Dashboard_UI_SubmitDriveMode(-1);test_tick+=300;Dashboard_UI_Process();
    lv_refr_now(NULL);save_frame(directory,7);
    Dashboard_UI_SubmitDriveMode(3);test_tick+=300;Dashboard_UI_Process();
    Dashboard_UI_PushAlert("inverter overtemp");Dashboard_UI_PushAlert("gps not fixed");
    test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);save_frame(directory,4);
    Dashboard_UI_RequestAlertClear();Dashboard_UI_SubmitDriveMode(0);Dashboard_UI_SetSprintReady(1);
    Dashboard_UI_SubmitSpeed(0);test_tick+=300;Dashboard_UI_Process();lv_refr_now(NULL);save_frame(directory,5);
}

#ifdef HAS_SPLASH
/* 开屏动画：每一步的增量帧必须与整屏重绘逐像素一致（失效区域不漏不错），结束后与仪表一致且覆盖层被删除。 */
static uint16_t splash_ref[800*480],splash_check[800*480];
static unsigned splash_draws;
static void count_draw(lv_event_t *e) {(void)e;splash_draws++;}
static void splash_tick(void) {test_tick+=33;Dashboard_UI_Process();lv_refr_now(NULL);}
static void splash_full_redraw_matches(const char *where)
{
    memcpy(splash_check,frame_pixels,sizeof(frame_pixels));
    lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
    if(memcmp(splash_check,frame_pixels,sizeof(frame_pixels))) {
        unsigned bad=0,first=0;
        for(unsigned i=0;i<800*480;i++) if(splash_check[i]!=frame_pixels[i]) {if(!bad++)first=i;}
        fprintf(stderr,"splash %s: incremental frame differs from full redraw in %u px (first at x=%u y=%u)\n",where,bad,first%800,first/800);
        abort();
    }
}
static unsigned splash_nonblack(unsigned y1,unsigned y2)
{
    unsigned n=0;
    for(unsigned y=y1;y<y2;y++) for(unsigned x=0;x<800;x++) n+=frame_pixels[y*800+x]!=0;
    return n;
}
/* 从当前状态走到动画结束。shots: 要存 PNG 源帧的步序号（相对本次播放，第一步 = 0）；directory 为空则不存。 */
static void splash_run(const char *label,unsigned hold_step,const unsigned *shots,unsigned nshots,const char *directory,unsigned *shot_index)
{
    unsigned steps=0,over25=0,count_dashboard_draw_in_hold=0;
    unsigned long peak=0,sum=0;
    lv_obj_t *probe=lv_obj_get_child(lv_screen_active(),0);   /* 仪表顶栏 Logo 对象：保持阶段不应被绘制 */
    lv_obj_add_event_cb(probe,count_draw,LV_EVENT_DRAW_MAIN,NULL);
    while(Splash_View_IsActive()) {
        flushed_px=0;splash_tick();
        if(steps>0) {  /* 第 0 步含上电首帧的整屏黑，不计入每步面积 */
            if(flushed_px>peak)peak=flushed_px;sum+=flushed_px;over25+=flushed_px>800UL*480UL/4;
        }
        for(unsigned i=0;i<nshots;i++) if(shots[i]==steps&&directory) {
            char path[1024];snprintf(path,sizeof(path),"%s/splash-%u.rgb565",directory,(*shot_index)++);
            FILE *f=fopen(path,"wb");assert(f);assert(fwrite(frame_pixels,2,800*480,f)==800*480);fclose(f);
        }
        if(steps==hold_step&&!directory) {
            /* 全黑保持阶段：整屏失效后 LVGL 只画覆盖层，不碰其下的仪表控件；底栏区域没有开屏内容，必须保持全黑。 */
            splash_draws=0;lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
            count_dashboard_draw_in_hold=splash_draws;
            assert(count_dashboard_draw_in_hold==0);
            assert(splash_nonblack(376,480)==0);
        }
        if(!directory) splash_full_redraw_matches(label);
        steps++;assert(steps<200);
    }
    lv_obj_remove_event_cb(probe,count_draw);
    if(!directory) {
        assert(splash_draws>0);   /* 扫屏阶段确实画了仪表控件 */
        printf("Splash %s: %u steps, per-step redraw peak %lu px (%.1f%%), average %.1f%%, %u steps above 25%%\n",label,steps,peak,
               peak*100.0/(800*480),steps>1?sum*100.0/((steps-1)*800.0*480):0.0,over25);
        assert(peak<=800UL*480UL*2/5);
    }
}
static void splash_touch(unsigned x,unsigned y)
{
    Dashboard_UI_SubmitTouchState(x,y,0);
    test_tick+=400;Dashboard_UI_SubmitTouchState(x,y,1);
}
static void test_splash(void)
{
    unsigned boot_ms=Splash_View_DurationMs(SPLASH_BOOT),replay_ms=Splash_View_DurationMs(SPLASH_REPLAY),children;
    assert(replay_ms==boot_ms+300&&boot_ms>2000&&boot_ms<3000);

    /* 上电：首帧整屏黑，之后逐步播放，结束后覆盖层已删除。 */
    assert(Splash_View_IsActive()&&splash_nonblack(0,480)==0);
    children=lv_obj_get_child_count(lv_screen_active());
    lv_mem_monitor_t with_overlay;lv_mem_monitor(&with_overlay);
    splash_run("boot",62,NULL,0,NULL,NULL);
    assert(!Splash_View_IsActive()&&lv_obj_get_child_count(lv_screen_active())==children-1U);
    lv_mem_monitor_t without_overlay;lv_mem_monitor(&without_overlay);
    size_t overlay_bytes=(with_overlay.total_size-with_overlay.free_size)-(without_overlay.total_size-without_overlay.free_size);
    assert(overlay_bytes<1024);   /* 覆盖层对象 + 事件表 + 局部样式；LVGL 池几乎没有余量，不能多占 */
    memcpy(splash_ref,frame_pixels,sizeof(frame_pixels));       /* 开屏结束后的仪表，后面的重放必须还原成它 */
    assert(splash_nonblack(0,480)>0);

    /* 触摸区：Logo 区只重放开屏，不触发计时；其余顶栏仍是计时区。 */
    assert(Dash_View_HitTest(50,25)==DASH_ZONE_LOGO&&Dash_View_HitTest(163,25)==DASH_ZONE_LOGO);
    assert(Dash_View_HitTest(164,25)==DASH_ZONE_LAP&&Dash_View_HitTest(500,25)==DASH_ZONE_LAP&&Dash_View_HitTest(50,52)==DASH_ZONE_NONE);
    g_pending_lap_toggle=0U;
    splash_touch(200,25);assert(g_pending_lap_toggle==1U);g_pending_lap_toggle=0U;
    splash_touch(50,25);assert(g_pending_lap_toggle==0U&&g_pending_splash_replay==1U);
    test_tick+=20;Dashboard_UI_Process();assert(Splash_View_IsActive()&&g_splash_active==1U);
    Dashboard_UI_SubmitTouchState(50,25,0);

    /* 重放：与上电同样的流程，只是多一段入场扫屏；结束后与原来的仪表逐像素相同。 */
    children=lv_obj_get_child_count(lv_screen_active());
    Splash_View_Start(SPLASH_REPLAY);                          /* 已在播放时再启动无效 */
    assert(lv_obj_get_child_count(lv_screen_active())==children);
    splash_run("replay",72,NULL,0,NULL,NULL);
    assert(!Splash_View_IsActive()&&g_splash_active==0U);
    assert(!memcmp(splash_ref,frame_pixels,sizeof(frame_pixels)));

    /* 开屏中点击：只跳过动画，不触发下面的触摸区（模式色块、Logo 区）。 */
    for(unsigned phase=0;phase<2;phase++) {
        uint32_t mode_before=(uint32_t)g_drive_mode;
        splash_touch(50,25);Dashboard_UI_SubmitTouchState(50,25,0);
        test_tick+=20;Dashboard_UI_Process();lv_refr_now(NULL);
        assert(Splash_View_IsActive());
        for(unsigned i=0;i<(phase?20U:3U);i++)splash_tick();   /* phase 0: 入场扫屏中（t≈100 ms）；phase 1: 字母飞行中（t≈700 ms） */
        Dashboard_UI_SubmitTouchState(56,106,0);Dashboard_UI_SubmitTouchState(56,106,1);   /* 不推进时间，点在模式色块上 */
        assert(g_pending_splash_skip==1U&&g_pending_mode_cycle==0U&&g_pending_splash_replay==0U);
        splash_tick();assert(Splash_View_IsActive());
        unsigned after_skip=0;
        flushed_px=0;
        while(Splash_View_IsActive()) {splash_tick();splash_full_redraw_matches("skip");after_skip++;assert(after_skip<40);}
        assert(after_skip<=(450U+33U)/33U+2U);               /* 跳到退场扫屏，扫屏时长不变 */
        assert(!memcmp(splash_ref,frame_pixels,sizeof(frame_pixels))&&(uint32_t)g_drive_mode==mode_before);
        Dashboard_UI_SubmitTouchState(56,106,0);
    }
    lv_mem_monitor_t m;lv_mem_monitor(&m);
    printf("UI splash: boot/replay/skip frames match a full redraw, dashboard not drawn while covered, touch zones and no-residue passed (overlay %zu B of LVGL pool, pool peak %zu, free %zu)\n",overlay_bytes,m.max_used,m.free_size);
}
/* 截图：上电 6 帧 + 重放入场 1 帧（splash-0..6） */
static void render_splash(const char *directory)
{
    static const unsigned boot_shots[]={6,15,24,33,47,70};
    static const unsigned replay_shots[]={4};
    unsigned index=0;
    splash_run("boot",0,boot_shots,6,directory,&index);
    Splash_View_Start(SPLASH_REPLAY);
    splash_run("replay",0,replay_shots,1,directory,&index);
}
#endif
int main(int argc,char **argv)
{
    CAN_StartServiceTask();
    if(argc==1) {assert(!Dashboard_UI_IsStartupComplete());receive(0x305,6,3);assert(g_can_rx_queue->count==0);}
    init_ui();assert(Dashboard_UI_IsStartupComplete());
    assert(g_drive_mode==DRIVE_MODE_DEFAULT&&g_mode_index==-1&&g_laps_left==0);
#ifdef HAS_SPLASH
    if(argc==2) render_splash(argv[1]);
    else test_splash();
#endif
    if(argc==2) {render_modes(argv[1]);return 0;}
    test_can();test_brake_pedal_module();test_can_decode();test_brake_pedal_can();test_steering();test_odometer();test_gps();test_speed_module();test_gps_pipeline();test_continuity();test_gps_link();test_gps_config();test_cli();test_ui_speed();test_mode_touch();test_mode_touch_release();test_default_mode();test_ui();test_motor_peak();return 0;
}
/* USER CODE END Firmware_Regression */
