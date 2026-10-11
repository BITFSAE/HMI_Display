/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
#include "can.h"
#include "stdio.h"
#include "string.h"
#include "atk_mw1278d_uart.h"
#include "atk_mw1278d.h"
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
/* 锟斤拷锟秸伙拷锟斤拷, 锟斤拷锟経SART_REC_LEN锟斤拷锟街斤拷. */
extern uint16_t g_usart_rx_sta = 0;
extern uint8_t g_usart_rx_buf[USART_REC_LEN] = {0};
//extern uint8_t g_rx_buffer[RXBUFFERSIZE] = {0};  /* HAL锟斤拷使锟矫的达拷锟节斤拷锟秸伙拷锟斤拷 */
extern uint8_t g_rx_buffer = 0;
extern uint16_t UART2_RX_STA = 0;
extern uint8_t UART2_RX_BUFFER[UART2_REC_LEN];
extern uint8_t UART2_RX_Buffer;
extern uint8_t RxFlag;
extern int Mode_Index;
extern int speed;   //SPEED
extern int SOC;    //SOC
extern int Sum_Voltage;
extern int RPM[4];
extern int Top_Temperature;
extern int Sum_I;
extern int acc_gl;
extern int acc_gt;
extern int torque_M[4];
extern uint8_t Rxbuf[UART2_REC_LEN];
extern uint8_t mode_sent[8] = {0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00};//驾驶模式CAN发送
//extern int P_FL;
//extern int P_FR;
//extern int P_RL;
//extern int P_RR;
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}
/* USART2 init function */

void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600 ;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = Wireless_TX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(Wireless_TX_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = Wireless_RX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(Wireless_RX_GPIO_Port, &GPIO_InitStruct);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    /* USART2 clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, Wireless_TX_Pin|Wireless_RX_Pin);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);

    /* USART2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
void send(char sign, int number)
{

	char buff[30];
	sprintf(buff,"%c%d\n",sign,number);
	HAL_UART_Transmit(&huart2, (uint8_t*)buff, strlen(buff), 100);
	HAL_Delay(10);
	//wireless communication
	switch(sign){
	case'A':{sprintf(buff,"SP=%d km\\h\n",speed);break;}
	case'B':{sprintf(buff,"SOC=%d %%\n",SOC);break;}
	case'C':{sprintf(buff,"SV=%d V*10\n",Sum_Voltage);break;}
	case'D':{sprintf(buff,"TT=%d `C\n",Top_Temperature-30);break;}
	case'E':{sprintf(buff,"SI=%d A*100\n",Sum_I);break;}
	case'F':{sprintf(buff,"MI=%d \n",Mode_Index);break;}
	case'G':{sprintf(buff,"ac_y=%d m/s^2\n",acc_gl);break;}
	case'H':{sprintf(buff,"ac_x=%d m/s^2\n",acc_gt);break;}
	default:break;
   }

	 HAL_UART_Transmit(&huart1, (uint8_t*)buff, strlen(buff),150);

}
void send_array(char sign,int *INT,int num)
{

	char Buff[64];
	sprintf(Buff,"%c",sign);
	HAL_UART_Transmit(&huart2, (uint8_t*)Buff, strlen(Buff), 100);
	for(int j=0;j<num;j++)
	{
	  sprintf(Buff,"%d,",INT[j]);
	  HAL_UART_Transmit(&huart2, (uint8_t*)Buff, strlen(Buff), 100);
	}
		 HAL_UART_Transmit(&huart2, (uint8_t*)"\n", 1, 100);
	HAL_Delay(10);

	switch(sign){
	case'R':{sprintf(Buff,"RS=%d,%d,%d,%d rpm\n",RPM[0],RPM[1],RPM[2],RPM[3]);break;}
	case'T':{sprintf(Buff,"TM=%d,%d,%d,%d N*M\n",torque_M[0],torque_M[1],torque_M[2],torque_M[3]);break;}
	default:break;
	}

   HAL_UART_Transmit(&huart1, (uint8_t*)Buff, strlen(Buff),150);

}
/**
 * @brief       串口数据接收回调函数
                数据处理在这里进行
 * @param       huart:串口句柄
 * @retval      无
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)//the call-back function is changed for new device ,not for the blue-tooth
{
//	HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
//	if(huart->Instance == USART1)             /* 判断是否是USART1 */
//	{
//	    if((g_usart_rx_sta & 0x8000) == 0)      /* 接收未完成（最高位为0） */
//	    {
//	        if(g_usart_rx_sta & 0x4000)         /* 已经接收到0x0d（次高位为1） */
//	        {
//	            if(g_rx_buffer[0] != 0x0a)      /* 当前字符不是0x0a */
//	            {
//	                g_usart_rx_sta = 0;         /* 接收错误，重新开始接收 */
//	            }
//	            else
//	            {
//
//	                if(g_usart_rx_buf[0]=='M')
//	                {
////	                	HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
//	                	atk_mw1278d_uart_printf("receive!\r\n",speed);
//	                	mode_sent[0]=g_usart_rx_buf[1];
//	                	User_CAN_Send_sq(0x310,mode_sent);
//	                }
//                        g_usart_rx_sta |= 0x8000;   /* 接收完成（设置最高位） */
//	            }
//	        }
//	        else                                /* 还未接收到0X0D */
//	        {
//	            if(g_rx_buffer[0] == 0x0d)      /* 当前字符是0x0d */
//	            {
//	                g_usart_rx_sta |= 0x4000;   /* 标记已收到0x0d（设置次高位） */
//	            }
//	            else
//	            {
//	                /* 存储接收到的数据到缓冲区 */
//	                g_usart_rx_buf[g_usart_rx_sta & 0X3FFF] = g_rx_buffer[0];
//	                g_usart_rx_sta++;           /* 接收数据计数器加1 */
//
//	                if(g_usart_rx_sta > (USART_REC_LEN - 1))
//	                {
//	                    g_usart_rx_sta = 0;     /* 缓冲区溢出，重新开始接收 */
//	                }
//	            }
//	        }
//	    }
//	    HAL_UART_Receive_IT(&huart1, (uint8_t*)g_rx_buffer, RXBUFFERSIZE);
//	}
	if(huart->Instance == USART1)             /* 判断是否是USART1 */
	{
//		HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
		if(g_rx_buffer == 0x0a)            /* 接收到换行符'\n'，帧结束 */
	    {
	        /* 数据处理 */
	        if(g_usart_rx_buf[0] == 'M')      /* 判断第一个字符 */
	        {
	            mode_sent[0] = g_usart_rx_buf[1];
	            HAL_UART_Transmit(&huart1,(uint8_t*)"receiveM!\r\n",11,100);
	        }
	         g_usart_rx_sta = 0; /* 重置接收状态 */
	    }
	    else if(g_usart_rx_sta >= USART_REC_LEN) /* 缓冲区溢出 */
	    {
	        g_usart_rx_sta = 0;               /* 重新开始接收 */
	    }
	    else                                  /* 正常数据接收 */
	    {
	        g_usart_rx_buf[g_usart_rx_sta++] = g_rx_buffer; /* 存储数据 */
	    }
		g_rx_buffer = 0;
	    HAL_UART_Receive_IT(&huart1, (uint8_t*)&g_rx_buffer, RXBUFFERSIZE);
	}
        if(huart->Instance == USART2)
		{
//        	HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_4);
				if(UART2_RX_Buffer==0x0a)// '\n'
				{

					if(UART2_RX_BUFFER[1]==0xef)
					 {
					  HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
					  mode_sent[0]=UART2_RX_BUFFER[0];
					 }

					UART2_RX_STA = 0;

				}
				else if(UART2_RX_STA>=UART2_REC_LEN)
			    {
						UART2_RX_STA = 0;
			      }
				else
				{
					UART2_RX_BUFFER[UART2_RX_STA++] = UART2_RX_Buffer;
				}

			UART2_RX_Buffer = 0;
		    // 重新开启中断
			HAL_UART_Receive_IT(&huart2,&UART2_RX_Buffer,sizeof(UART2_RX_Buffer));
		}
}

/**
 * @brief       串口X中断服务函数
                注意,读取USARTx->SR能避免莫名其妙的错误
 * @param       无
 * @retval      无
 */
void USART_UX_IRQHandler(void)
{
#if SYS_SUPPORT_OS                              /* 使锟斤拷OS */
    OSIntEnter();
#endif

    HAL_UART_IRQHandler(&huart1);       /* 锟斤拷锟斤拷HAL锟斤拷锟叫断达拷锟斤拷锟矫猴拷锟斤拷 */

#if SYS_SUPPORT_OS                              /* 使锟斤拷OS */
    OSIntExit();
#endif

}

/* 锟斤拷锟斤拷锟斤拷锟铰达拷锟斤拷, 支锟斤拷printf锟斤拷锟斤拷, 锟斤拷锟斤拷锟斤拷要选锟斤拷use MicroLIB */

#if 1

#if (__ARMCC_VERSION >= 6010050)            /* 使锟斤拷AC6锟斤拷锟斤拷锟斤拷时 */
__asm(".global __use_no_semihosting\n\t");  /* 锟斤拷锟斤拷锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
__asm(".global __ARM_use_no_argv \n\t");    /* AC6锟斤拷锟斤拷要锟斤拷锟斤拷main锟斤拷锟斤拷为锟睫诧拷锟斤拷锟斤拷式锟斤拷锟斤拷锟津部凤拷锟斤拷锟教匡拷锟杰筹拷锟街帮拷锟斤拷锟斤拷模式 */

#else
/* 使锟斤拷AC5锟斤拷锟斤拷锟斤拷时, 要锟斤拷锟斤拷锟斤定锟斤拷__FILE 锟斤拷 锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* 锟斤拷使锟矫帮拷锟斤拷锟斤拷模式锟斤拷锟斤拷锟斤拷锟斤拷要锟截讹拷锟斤拷_ttywrch\_sys_exit\_sys_command_string锟斤拷锟斤拷,锟斤拷同时锟斤拷锟斤拷AC6锟斤拷AC5模式 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* 锟斤拷锟斤拷_sys_exit()锟皆憋拷锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}


/* FILE 锟斤拷 stdio.h锟斤拷锟芥定锟斤拷. */
//FILE __stdout;

/* MDK锟斤拷锟斤拷要锟截讹拷锟斤拷fputc锟斤拷锟斤拷, printf锟斤拷锟斤拷锟斤拷锟秸伙拷通锟斤拷锟斤拷锟斤拷fputc锟斤拷锟斤拷址锟斤拷锟斤拷锟斤拷锟斤拷锟� */
//int fputc(int ch, FILE *f)
//{
//    while ((USART1 ->SR & 0X40) == 0);     /* 锟饺达拷锟斤拷一锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷锟� *///changed for new device
//
//    USART1 ->DR = (uint8_t)ch;             /* 锟斤拷要锟斤拷锟酵碉拷锟街凤拷 ch 写锟诫到DR锟侥达拷锟斤拷 */
//    return ch;
//}
#endif

/* USER CODE END 1 */
