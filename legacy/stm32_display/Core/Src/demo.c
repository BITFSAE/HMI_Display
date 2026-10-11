/**
 ****************************************************************************************************
 * @file        demo.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-01-28
 * @brief       ATK-MW1278D模块测试实验
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 STM32F103开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "demo.h"
#include "atk_mw1278d.h"
#include "usart.h"
/* ATK-MW1278D模块配置参数定义 */
#define DEMO_ADDR       9                               /* 设备地址 */
#define DEMO_WLRATE     ATK_MW1278D_WLRATE_19K2         /* 空中速率 */
#define DEMO_CHANNEL    8                               /* 信道 */
#define DEMO_TPOWER     ATK_MW1278D_TPOWER_20DBM        /* 发射功率 */
#define DEMO_WORKMODE   ATK_MW1278D_WORKMODE_NORMAL     /* 工作模式 */
#define DEMO_TMODE      ATK_MW1278D_TMODE_TT            /* 发射模式 */
#define DEMO_WLTIME     ATK_MW1278D_WLTIME_1S           /* 休眠时间 */
#define DEMO_UARTRATE   ATK_MW1278D_UARTRATE_115200BPS  /* UART通讯波特率 */
#define DEMO_UARTPARI   ATK_MW1278D_UARTPARI_NONE       /* UART通讯校验位 */

/**
 * @brief       例程演示入口函数
 * @param       无
 * @retval      无
 */
void demo_run(void)
{
    uint8_t ret;
//    uint8_t key;
//    uint8_t *buf;
    
    /* 初始化ATK-MW1278D模块 */
    ret = atk_mw1278d_init(115200);
//    if (ret != 0)
//    {
////        printf("ATK-MW1278D init failed!\r\n");
////        while (1)
////        {
//            //LED0_TOGGLE();
//        	HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
//            delay_ms(200);
////        }
//    }
    
    /* 配置ATK-MW1278D模块 */
    atk_mw1278d_enter_config();
    ret  = atk_mw1278d_addr_config(DEMO_ADDR);
    ret += atk_mw1278d_wlrate_channel_config(DEMO_WLRATE, DEMO_CHANNEL);
    ret += atk_mw1278d_tpower_config(DEMO_TPOWER);
    ret += atk_mw1278d_workmode_config(DEMO_WORKMODE);
    ret += atk_mw1278d_tmode_config(DEMO_TMODE);
    ret += atk_mw1278d_wltime_config(DEMO_WLTIME);
    ret += atk_mw1278d_uart_config(DEMO_UARTRATE, DEMO_UARTPARI);
    atk_mw1278d_exit_config();
//    if (ret != 0)
//    {
//        printf("ATK-MW1278D config failed!\r\n");
//        while (1)
//        {
//            //LED0_TOGGLE();
//
//        	HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
//            delay_ms(200);
//        }
//    }
//    atk_mw1278d_uart_printf("1234\r\n");
    //printf("ATK-MW1278D config succedded!\r\n");
//    atk_mw1278d_uart_rx_restart();//注释掉
    
//    while (1)
//    {
//
////        buf = atk_mw1278d_uart_rx_get_frame();
////        if (atk_mw1278d_free() != ATK_MW1278D_EBUSY)
////                    {
////                        atk_mw1278d_uart_printf("This is from ATK-MW1278D.\r\n");
////                    }
////        if (buf != NULL)
////        {
////            printf("%s", buf);
////            atk_mw1278d_uart_rx_restart();
////        }
////    }
////    	/* 在main.c的while(1)循环中 */
////    	uint8_t rx_byte;
////    	HAL_StatusTypeDef status;
////
////    	// 尝试接收一个字节，等待时间为1毫秒（非阻塞方式）
////    	status = HAL_UART_Receive(&huart1, &rx_byte, 1, 1);
////
////    	if (status == HAL_OK) {
////    	    // 如果收到一个字节，立刻通过串口把它发送回去（Echo）
////    	    HAL_UART_Transmit(&huart1, &rx_byte, 1, 100);
////    	    // 同时可以点亮一个LED灯指示收到数据
////    	    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); // 假设PA5接了一个LED
////    	}
////    	else if (status == HAL_TIMEOUT) {
////    	    // 超时是正常的，说明没有收到数据，继续循环
////    	}
////    	else {
////    	    // HAL_ERROR, 说明可能有问题
////    	}
//    	/* 在main.c的while(1)循环中 */
//    	uint8_t rx_byte;
//    	HAL_StatusTypeDef status;
//
//    	// 尝试非阻塞接收一个字节，超时时间设为1ms
//    	status = HAL_UART_Receive(&huart1, &rx_byte, 1, 1);
//
//    	switch (status) {
//    	    case HAL_OK:
//    	        // 成功收到数据！
//    	        // 1. 回显数据（测试TX通路是否畅通）
//    	        HAL_UART_Transmit(&huart1, &rx_byte, 1, 100);
//
//    	        // 2. 翻转LED，直观显示收到数据了
//    	        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); // 请根据你的工程修改LED引脚定义
//
//    	        // 3. 【可选】通过串口打印调试信息（如果串口助手支持ASCII和HEX显示）
//    	        // 这会发送一段文字，帮助你确认收到的是什么
//    	         printf("Received: %c (0x%02X)\r\n", rx_byte, rx_byte); // 如果开启了printf重定向
//
//    	        break;
//
//    	    case HAL_TIMEOUT:
//    	        // 这是正常情况，表示在1ms内没有收到数据
//    	        // 不需要做任何处理，继续循环即可
//    	        break;
//
//    	    case HAL_ERROR:
//    	        // 如果出现错误，可能是串口硬件或底层驱动问题
//    	        // 让另一个LED保持常亮作为错误指示（如果有第二个LED的话）
//    	    	HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_4); // 请根据你的工程修改LED引脚定义
//    	        break;
//    	}
//
//    	// 可以加一个短暂延时，避免循环太快，但不要影响接收
//    	 HAL_Delay(10);
//
//    }
}
