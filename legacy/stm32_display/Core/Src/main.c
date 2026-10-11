/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
  * 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
  * shaoqi 2024.8.21 锟斤拷锟斤拷说锟斤拷
  * 锟斤拷锟侥匡拷模锟絀nterface 锟斤拷频锟侥匡拷锟斤拷锟轿拷锟斤拷锟斤拷惴斤拷锟斤拷炭锟斤拷倏刹锟斤拷
  * 实锟斤拷原锟斤拷锟斤拷锟斤拷锟斤拷锟教诧拷片锟斤拷锟截碉拷锟脚猴拷通锟斤拷锟斤拷锟斤拷锟斤拷锟酵碉拷锟斤拷示锟斤拷锟斤拷锟接讹拷锟斤拷锟斤拷锟斤拷示锟斤拷锟斤拷示锟斤拷锟斤拷
  * 锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷锟斤拷
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "atk_mw1278d.h"
#include "atk_mw1278d_uart.h"
#include "demo.h"
#include "delay.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
//HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);
//HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define FALSE 0
#define TRUE 1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
extern int speed = -1;      //速度
extern int SOC = -1;        //SOC
extern int Mode_Index = 4;   //控制模式
extern int RPM[4] = {1000,-1,-1,999};
extern int Sum_Voltage=-1;           //总电压
extern int Top_Temperature=-1;           //单体最高温
extern int Sum_I=-1;                     //总电流
extern int acc_gl;                       //纵向加速度
extern int acc_gt;           //横向加速度
extern int torque_M[4];
extern uint8_t UART2_RX_Buffer = 0;
extern uint8_t mode_sent[8];
extern uint8_t UART2_RX_BUFFER [UART2_REC_LEN] = {0};
uint8_t *Rxbuf;
extern uint8_t RxFlag = 0;//send first
extern uint8_t g_rx_buffer;
int flag = 1 ;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_CAN_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  CAN_Filter_Config();    //锟斤拷锟斤拷CAN锟剿诧拷锟斤拷
  HAL_CAN_Start(&hcan);   //锟斤拷锟斤拷can
  HAL_CAN_ActivateNotification(&hcan, CAN_IT_RX_FIFO0_MSG_PENDING); //锟斤拷锟斤拷FIFO0锟叫讹拷
  //wireless sent settings
  delay_init(25);                     /* 延时初始化 */
  demo_run();                         /* 运行示例程序并完成配置 */
  HAL_TIM_Base_Stop_IT(&htim4); // 停止TIM4及其中断
    __HAL_RCC_TIM4_CLK_DISABLE();
  HAL_UART_Receive_IT(&huart2,&UART2_RX_Buffer,sizeof(UART2_RX_Buffer));
  HAL_UART_Receive_IT(&huart1, (uint8_t*)&g_rx_buffer, RXBUFFERSIZE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
	  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_4);
	 //锟斤拷询锟秸凤拷锟斤拷猓拷锟紺PU占锟矫较高ｏ拷JLY锟斤拷

//      /*原锟叫的凤拷锟酵硷拷锟斤拷锟斤拷*/
//	  if(flag==0)
//	  {
//		  uint8_t TX[4]={0x11,0x22,0x33,0x44};
//		  HAL_UART_Transmit_IT(&huart2,(unsigned char *)TX, strlen((char*)TX));
//		  HAL_UART_Transmit_IT(&huart2,(uint8_t*)"A",1);
//		  HAL_UART_Transmit(&huart2,"A", 1, 50);
//		  //HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_4);
//	  }else if(flag==1)
//	  {
//		  uint8_t TX[4]={0x11,0x22,0x33,0x45};
//		  HAL_UART_Transmit_IT(&huart2,(unsigned char *)TX, strlen((char*)TX));
//		  HAL_UART_Transmit_IT(&huart2,(uint8_t*)"B",1);
//		  HAL_UART_Transmit(&huart2,"B", 1, 50);
//		  HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
//	  }
      /*锟斤拷锟斤拷锟斤拷锟捷革拷锟斤拷示锟斤拷*/

	if(RxFlag==0)
	{
  switch(flag){
    case 1:	  {send('A',speed);break;}
    case 2:   {send('B',SOC);break;}
    case 3:   {send('C',Sum_Voltage);break;}
    case 4:   {send('D',Top_Temperature);break;}
    case 5:   {send('E',Sum_I);break;}
    case 6:   {send('F',Mode_Index);break;}
    case 7:   {send_array('R',RPM,4);break;}
    case 8:	  {send_array('T',torque_M,4);break;}
    case 9:   {send('G',acc_gl);break;}
    case 10:  {send('H',acc_gt);break;}
    default:  {atk_mw1278d_uart_printf("A%d,%d,%d\r\n",acc_gt,acc_gl,speed);break;}
}
	flag>=10? flag=1:flag++;

	}else{
//           HAL_UART_Transmit(&huart1,(uint8_t*)"receiveM!\r\n",11,100);
           HAL_Delay(50);
           RxFlag = 0;
	}
	User_CAN_Send_sq(0x310,mode_sent);

////put atk_uart_send() into send() function
////test example
//      speed=(speed>0)? speed-1:90;
//      	  SOC=(SOC>4)? SOC-1:99;
//        	  Top_Temperature=(Top_Temperature>75)? 30:Top_Temperature+2;
//        	  P_FL=(P_FL==20)?50:20;
//        	  Sum_Voltage=(Sum_Voltage>50)?Sum_Voltage-100:550;
//        	  Sum_I=(Sum_I+1)/2;
//      	  HAL_GPIO_TogglePin(GPIOA,GPIO_PIN_5);
//        User_CAN_Send_sq(0x03,CAN_RxData);
 //锟饺达拷,锟斤拷止锟斤拷锟斤拷锟斤拷锟斤拷jly)
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	HAL_Delay(200);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
int fputc(int ch, FILE*f)
{
	uint8_t temp[1] = {ch};
	//锟斤拷询锟斤拷锟斤拷1锟街斤拷锟斤拷锟斤拷
	HAL_UART_Transmit(&huart1,temp,1,2);
	return ch;
}
int fgetc(FILE *f)
{
	uint8_t ch;
	// 锟斤拷锟斤拷锟斤拷询锟斤拷式锟斤拷锟斤拷 1锟街斤拷锟斤拷锟捷ｏ拷锟斤拷时时锟斤拷锟斤拷锟斤拷为锟斤拷锟睫等达拷
	HAL_UART_Receive( &huart1,(uint8_t*)&ch,1, HAL_MAX_DELAY );

	return ch;
}

/**
  * @brief  EXTI线中断检测回调函数。
  * @param  GPIO_Pin: 触发中断的引脚号
  * @retval None
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* 判断是否是PB5（AUX引脚）触发的中断 */
    if (GPIO_Pin == AUX_Pin)
    {
        /* 再次确认当前引脚电平，确保是下降沿（模块闲） */
        if (HAL_GPIO_ReadPin(AUX_GPIO_Port, AUX_Pin) == GPIO_PIN_RESET)
        {
            // 在这里处理模块变为空闲的事件
            // 例如：设置一个标志位，通知主循环可以发送下一条数据了
            RxFlag = 1;

            // 或者你可以直接在这里点亮一个LED作为指示
//            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
