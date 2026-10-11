/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN 0 */
extern int speed;   //锟斤拷锟斤拷锟劫讹拷
extern int SOC;    //锟斤拷氐锟斤拷锟斤拷
extern int P_FL;
extern int P_FR;
extern int P_RL;
extern int P_RR;
extern int Sum_Voltage;
extern int Top_Temperature;
extern int Mode_Index;
extern int Sum_I;
extern int acc_gl = 88;
extern int acc_gt = 88;
extern int torque_M[4] = {-1,-1,-1,-1};
extern int RPM[4];
CAN_TxHeaderTypeDef TxHeader;
uint8_t CAN_TxData[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
uint32_t CAN_TxMail;
uint32_t CAN_ID = 0x102;

CAN_RxHeaderTypeDef RxHeader;
uint8_t CAN_RxData[8] = { 0 };

/* USER CODE END 0 */

CAN_HandleTypeDef hcan;

/* CAN init function */
void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 9;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_5TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = DISABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* CAN1 interrupt Init */
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_RX1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(CAN1_RX1_IRQn);
  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

    /* CAN1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX1_IRQn);
  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
// @func: 配置CAN的滤波
//shaoqi_change
uint16_t CAN_RX_MSG_ID[4] = {0x401, 0x501, 0x502, 0x50};

/*
 * @func: 锟斤拷锟斤拷CAN锟斤拷锟剿诧拷
 *
 */
void CAN_Filter_Config(void)
{
	CAN_FilterTypeDef CAN_FilterInitStructure = {
			.FilterActivation = ENABLE,                    //enable the filter
			.FilterBank = 0x00,                            //encode the filters family,range:0-13
			.FilterFIFOAssignment = CAN_FILTER_FIFO0,      //报文储存FIFO编号，FIFO0
			.FilterIdHigh = CAN_RX_MSG_ID[0]<<5,           //first ID
			.FilterIdLow = CAN_RX_MSG_ID[1]<<5,            //second ID
			.FilterMaskIdHigh = CAN_RX_MSG_ID[2]<<5,       //third ID
			.FilterMaskIdLow = CAN_RX_MSG_ID[3]<<5,        //fourth ID
			.FilterMode = CAN_FILTERMODE_IDLIST,           //ID列表模式
			.FilterScale = CAN_FILTERSCALE_16BIT,          //16位，一个过滤器组可设置4个可通过ID
			.SlaveStartFilterBank = 0                      //主从过滤器分界线，单CAN无意义
	};
	HAL_CAN_ConfigFilter(&hcan, &CAN_FilterInitStructure);
}

/*
 * @func: CAN报文发送[标准格式、数据帧]
 */
void User_CAN_Send()
{
	TxHeader.RTR = CAN_RTR_DATA;
	TxHeader.IDE = CAN_ID_STD;
	TxHeader.StdId = CAN_ID;
	TxHeader.TransmitGlobalTime = DISABLE;
	TxHeader.DLC = 8;
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, CAN_TxData, &CAN_TxMail);
}

/*
 * @func: CAN paper send[standard form、data]
 */
//shaoqi_add
//发送指定ID
void User_CAN_Send_sq(uint32_t CAN_ID_NEW,uint8_t* CAN_TxData_NEW)
{
	TxHeader.RTR = CAN_RTR_DATA;
	TxHeader.IDE = CAN_ID_STD;
	TxHeader.StdId = CAN_ID_NEW;
	TxHeader.TransmitGlobalTime = DISABLE;
	TxHeader.DLC = 8;
	HAL_CAN_AddTxMessage(&hcan, &TxHeader, CAN_TxData_NEW, &CAN_TxMail);
}

/*
 * @func: CAN报文接收中断[FIFO0]
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
	if(HAL_CAN_GetRxMessage(hcan, CAN_FILTER_FIFO0, &RxHeader, CAN_RxData)!= HAL_OK)
	{
		// 错误处理
	    Error_Handler();
	}
//	printf("ID:0x%h\r\n",RxHeader.StdId);

	//根据不同的ID，获得不同的信息，具体请看车队的CAN协议
	if(RxHeader.StdId==0x401)
	{
		SOC=(int)CAN_RxData[6];
		Sum_Voltage=(int)CAN_RxData[0]+(int)(CAN_RxData[1])*256;
		Top_Temperature = (int)CAN_RxData[7];
		Sum_I = (int)CAN_RxData[4]+(int)(CAN_RxData[5])*256;

	}
	if(RxHeader.StdId==0x501)//新增电机扭矩与常态化驾驶模式
	{
		speed=(int)CAN_RxData[0];
		acc_gl=(int)CAN_RxData[1];
		acc_gt=(int)CAN_RxData[2];
		for(int i=0;i<4;i++)
	    {torque_M[i]=(int)CAN_RxData[i+3];}
		Mode_Index=(int)CAN_RxData[7];
	}
	if(RxHeader.StdId==0x502)
	{
		/*四轮扭矩，RL,FL,RR,FR */
		for(int i=0;i<4;i++)
		{RPM[i] = (int)CAN_RxData[i];}
//		P_FL=(256*(int)CAN_RxData[3]+(int)CAN_RxData[2])/9549;
//		P_FR=(256*(int)CAN_RxData[7]+(int)CAN_RxData[6])/9549;
//		P_RL=(256*(int)CAN_RxData[1]+(int)CAN_RxData[0])/9549;
//		P_RR=(256*(int)CAN_RxData[5]+(int)CAN_RxData[4])/9549;
	}
//	if(RxHeader.StdId==0x50)//IMU 回发与数据处理
//	{
////		User_CAN_Send_sq(0x03,CAN_RxData);
//		if(CAN_RxData[1]==0x50)
//		{
//			User_CAN_Send_sq(0x60,CAN_RxData);
//		}else if(CAN_RxData[1]==0x51)
//		{
//			User_CAN_Send_sq(0x61,CAN_RxData);
//
//		}else if(CAN_RxData[1]==0x52){
//			User_CAN_Send_sq(0x62,CAN_RxData);
//
//		}else if(CAN_RxData[1]==0x53){
//			if(CAN_RxData[2]==0x01)
//			{
//				User_CAN_Send_sq(0x63,CAN_RxData);
//			}else if(CAN_RxData[2]==0x02)
//			{
//				User_CAN_Send_sq(0x64,CAN_RxData);
//			}else if(CAN_RxData[2]==0x03)
//			{
//				User_CAN_Send_sq(0x65,CAN_RxData);
//			}
//
//		}else if(CAN_RxData[1]==0x54){
//			User_CAN_Send_sq(0x66,CAN_RxData);
//		}
//	}
}

/*
 * @func: CAN锟斤拷锟侥斤拷锟斤拷锟叫讹拷[FIFO1],锟斤拷锟�0x50
 */

//void HAL_CAN_RxFifo1MsgPendingCallback(CAN_HandleTypeDef *hcan)
//{
//	if(HAL_CAN_GetRxMessage(hcan, CAN_FILTER_FIFO0, &RxHeader, CAN_RxData)!= HAL_OK)
//	{
//	    // 锟斤拷锟斤拷锟斤拷
//	    Error_Handler();
//	}
//	printf("ID:0x%X\r\n",RxHeader.StdId);
//	if(RxHeader.StdId==0x50)
//	{
//		if(CAN_RxData[1]==0x50)
//		{
//			User_CAN_Send_sq(0x60,CAN_RxData);
//		}else if(CAN_RxData[1]==0x51)
//		{
//			User_CAN_Send_sq(0x61,CAN_RxData);
//		}else if(CAN_RxData[1]==0x52){
//			User_CAN_Send_sq(0x62,CAN_RxData);
//		}else if(CAN_RxData[1]==0x53){
//			if(CAN_RxData[2]==0x01)
//			{
//				User_CAN_Send_sq(0x63,CAN_RxData);
//			}else if(CAN_RxData[2]==0x02)
//			{
//				User_CAN_Send_sq(0x64,CAN_RxData);
//			}else if(CAN_RxData[2]==0x03)
//			{
//				User_CAN_Send_sq(0x65,CAN_RxData);
//			}
//
//		}else if(CAN_RxData[1]==0x54){
//			User_CAN_Send_sq(0x66,CAN_RxData);
//		}
//	}
//
//}
/* USER CODE END 1 */
