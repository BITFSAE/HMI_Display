/************************************************************************************
//STM32连接引脚是指TFTLCD插槽或者插座引脚内部连接的STM32引脚
//=================================电源接线=======================================//
//     LCD模块                    STM32连接引脚
//      3.3V          --->             DC3.3V          //电源
//      GND           --->             GND              //电源地
//=============================液晶屏数据线接线===================================//
//本模块默认数据总线类型为16位并口总线(8位模式请使用DB0~DB7)
//     LCD模块                    STM32连接引脚
//      D0            --->            PD14        -|   
//      D1            --->            PD15         |  
//      D2            --->            PD0          | 
//      D3            --->            PD1          | 
//      D4            --->            PE7          |
//      D5            --->            PE8          |
//      D6            --->            PE9          |
//      D7            --->            PE10         |===>液晶屏16位并口数据信号
//      D8            --->            PE11         |
//      D9            --->            PE12         |
//      D10           --->            PE13         |
//      D11           --->            PE14         |
//      D12           --->            PE15         |
//      D13           --->            PD8          |
//      D14           --->            PD9          |
//      D15           --->            PD10        -|
//=============================液晶屏控制线接线===================================//
//     LCD模块 				            STM32连接引脚 
//      WR            --->            PD5             //液晶屏写数据控制信号
//      RD            --->            PD4             //液晶屏读数据控制信号
//      RS            --->            PD11            //液晶屏数据/命令控制信号
//      REST          --->          复位引脚（默认）  //液晶屏复位控制信号（也可选择PD13）
//      CS            --->            PD7             //液晶屏片选控制信号
//      LED_A         --->            PB15            //液晶屏背光控制信号
//===============================触摸屏触接线=====================================//
//如果模块不带触摸功能或者带有触摸功能，但是不需要触摸功能，则不需要进行触摸屏接线
//	   LCD模块                    STM32连接引脚 
//      T_IRQ         --->            PB1             //电容或电阻触摸屏触摸中断信号
//      T_DO          --->            PB2             //电阻触摸屏SPI总线读信号
//      T_DIN         --->            PC4             //电阻触摸屏SPI总线写信号或电容触摸屏IIC总线数据信号
//      T_CS          --->            PC13            //电阻触摸屏片选控制信号或电容触摸屏复位信号
//      T_CLK         --->            PB0             //电阻触摸屏SPI总线或电容触摸屏IIC总线时钟信号
*************************************************************************************/		
#include "lcd.h"
#include "stdlib.h"
#include "delay.h"	 

	   
//管理LCD重要参数
//默认为竖屏
_lcd_dev lcddev;

//画笔颜色,背景颜色
u16 POINT_COLOR = 0x0000,BACK_COLOR = 0xFFFF;  
u16 DeviceCode;	 


u16 LCD_read(void)
{
	vu16 data;  //防止被优化
	data=LCD->LCD_RAM;	
	return data;
}

/*****************************************************************************
 * @name       :void LCD_WR_REG(u16 data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :data:Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(vu16 data)
{ 
	data=data;  //使用-O2优化的时候,必须插入的延时
	LCD->LCD_REG=data;//写入要写的寄存器序号	
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA(u16 data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit data to the LCD screen
 * @parameters :data:data value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_DATA(vu16 data)
{
	data=data;			//使用-O2优化的时候,必须插入的延时
	LCD->LCD_RAM=data;
}

/*****************************************************************************
 * @name       :u16 LCD_RD_DATA(void)
 * @date       :2018-11-13 
 * @function   :Read an 16-bit value from the LCD screen
 * @parameters :None
 * @retvalue   :read value
******************************************************************************/
u16 LCD_RD_DATA(void)
{
	return LCD_read();
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write data into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(u16 LCD_Reg, u16 LCD_RegValue)
{	
	LCD->LCD_REG = LCD_Reg;		//写入要写的寄存器序号	 
	LCD->LCD_RAM = LCD_RegValue;//写入数据	     		 
}	   

/*****************************************************************************
 * @name       :u16 LCD_ReadReg(u16 LCD_Reg)
 * @date       :2018-11-13 
 * @function   :read value from specially registers
 * @parameters :LCD_Reg:Register address
 * @retvalue   :read value
******************************************************************************/
void LCD_ReadReg(u16 LCD_Reg,u8 *Rval,int n)
{
	LCD_WR_REG(LCD_Reg); 
	while(n--)
	{		
		*(Rval++) = LCD_RD_DATA();
		delay_us(100);
	}
}

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 
void LCD_WriteRAM_Prepare(void)
{
	LCD_WR_REG(lcddev.wramcmd);
}	 

/*****************************************************************************
 * @name       :void LCD_ReadRAM_Prepare(void)
 * @date       :2018-11-13 
 * @function   :Read GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 
void LCD_ReadRAM_Prepare(void)
{
	LCD_WR_REG(lcddev.rramcmd);
}

/*****************************************************************************
 * @name       :void Lcd_WriteData_16Bit(u16 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/	 
void Lcd_WriteData_16Bit(u16 Data)
{
#if LCD_USE8BIT_MODEL	
	 LCD->LCD_RAM = (Data>>8)&0xF8;
	 LCD->LCD_RAM = (Data>>3)&0xFC;
	 LCD->LCD_RAM = Data<<3;
#else
	 LCD->LCD_RAM = Data;  //写十六位颜色值
#endif
}

u16 Color_To_565(u8 r, u8 g, u8 b)
{
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

/*****************************************************************************
 * @name       :u16 Lcd_ReadData_16Bit(void)
 * @date       :2018-11-13 
 * @function   :Read an 16-bit value from the LCD screen
 * @parameters :None
 * @retvalue   :read value
******************************************************************************/	
u16 Lcd_ReadData_16Bit(void)
{
	u16 r=0;
#if LCD_USE8BIT_MODEL	
	u16 g=0,b=0;
#endif
	//16bit: red green and blue data
	//8bit:red data
	r = LCD_RD_DATA();
	delay_us(1);//延时1us
#if LCD_USE8BIT_MODEL
	//8bit:green data	
	g = LCD_RD_DATA();
	delay_us(1);//延时1us
	//8bit:blue data
	b = LCD_RD_DATA();
	delay_us(1);//延时1us	
	r=Color_To_565(r, g, b);
#endif
	return r;
}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(u16 x,u16 y)
 * @date       :2018-08-09 
 * @function   :Write a pixel data at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/	
void LCD_DrawPoint(u16 x,u16 y)
{
	LCD_SetCursor(x,y);//设置光标位置 
	Lcd_WriteData_16Bit(POINT_COLOR); 
}

/*****************************************************************************
 * @name       :u16 LCD_ReadPoint(u16 x,u16 y)
 * @date       :2018-11-13 
 * @function   :Read a pixel color value at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :the read color value
******************************************************************************/	
u16 LCD_ReadPoint(u16 x,u16 y)
{
	u16 color;
	if(x>=lcddev.width||y>=lcddev.height)
	{
		return 0;	//超过了范围,直接返回	
	}
	LCD_SetCursor(x,y);//设置光标位置 
	LCD_ReadRAM_Prepare();
	color = Lcd_ReadData_16Bit();
	return color;
}

/*****************************************************************************
 * @name       :void LCD_Clear(u16 Color)
 * @date       :2018-08-09 
 * @function   :Full screen filled LCD screen
 * @parameters :color:Filled color
 * @retvalue   :None
******************************************************************************/	
void LCD_Clear(u16 Color)
{
  unsigned int i;//,m;  
	u32 total_point=lcddev.width*lcddev.height;
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);   
	for(i=0;i<total_point;i++)
	{ 
#if LCD_USE8BIT_MODEL	
	 LCD->LCD_RAM = (Color>>8)&0xF8;
	 LCD->LCD_RAM = (Color>>3)&0xFC;
	 LCD->LCD_RAM = Color<<3;
#else
		LCD->LCD_RAM = Color;
#endif
	}
} 

/*****************************************************************************
 * @name       :void LCD_PWM_BackLightSet(u8 pwm)
 * @date       :2020-05-13
 * @function   :Set the backlight brightness from PWM
 * @parameters :pwm:pwm value 0~100, The greater the value, the brighter
 * @retvalue   :None
******************************************************************************/
void LCD_PWM_BackLightSet(u8 pwm)
{	
	LCD_WR_REG(0xBE);	//配置PWM输出
	LCD_WR_DATA(0x05);	//1设置PWM频率
	LCD_WR_DATA(pwm*2.55);//2设置PWM占空比
	LCD_WR_DATA(0x01);	//3设置C
	LCD_WR_DATA(0xFF);	//4设置D
	LCD_WR_DATA(0x00);	//5设置E
	LCD_WR_DATA(0x00);	//6设置F
}

/*****************************************************************************
 * @name       :void LCD_GPIOInit(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen GPIO
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_GPIOInit(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	FSMC_NORSRAMInitTypeDef  FSMC_NORSRAMInitStructure;
  FSMC_NORSRAMTimingInitTypeDef  readWriteTiming; 
	FSMC_NORSRAMTimingInitTypeDef  writeTiming;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB|RCC_AHB1Periph_GPIOD|RCC_AHB1Periph_GPIOE, ENABLE);//使能PD,PE,PF,PG时钟  
  RCC_AHB3PeriphClockCmd(RCC_AHB3Periph_FSMC,ENABLE);//使能FSMC时钟  
	
 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;//PB15 推挽输出,控制背光
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;//普通输出模式
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;//100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//上拉
  GPIO_Init(GPIOB, &GPIO_InitStructure);//初始化 //PB15 推挽输出,控制背光
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;//PD13 推挽输出,控制复位
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;//普通输出模式
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;//100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//上拉
  GPIO_Init(GPIOD, &GPIO_InitStructure);//初始化 //PB15 推挽输出,控制背光
	
  GPIO_InitStructure.GPIO_Pin = (3<<0)|(3<<4)|(7<<8)|(3<<14);//PD0,1,4,5,8,9,10,14,15 AF OUT
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用输出
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;//100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//上拉
  GPIO_Init(GPIOD, &GPIO_InitStructure);//初始化  
	
  GPIO_InitStructure.GPIO_Pin = (0X1FF<<7);//PE7~15,AF OUT
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用输出
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;//100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//上拉
  GPIO_Init(GPIOE, &GPIO_InitStructure);//初始化  

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_11;//PF12,FSMC_A6
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;//复用输出
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;//推挽输出
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;//100MHz
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;//上拉
  GPIO_Init(GPIOD, &GPIO_InitStructure);//初始化  


  GPIO_PinAFConfig(GPIOD,GPIO_PinSource0,GPIO_AF_FSMC);//PD0,AF12
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource1,GPIO_AF_FSMC);//PD1,AF12
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource4,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource5,GPIO_AF_FSMC); 
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource8,GPIO_AF_FSMC); 
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource9,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource10,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource14,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource15,GPIO_AF_FSMC);//PD15,AF12
 
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource7,GPIO_AF_FSMC);//PE7,AF12
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource8,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource9,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource10,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource11,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource12,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource13,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource14,GPIO_AF_FSMC);
  GPIO_PinAFConfig(GPIOE,GPIO_PinSource15,GPIO_AF_FSMC);//PE15,AF12
 
  GPIO_PinAFConfig(GPIOD,GPIO_PinSource7,GPIO_AF_FSMC);
	GPIO_PinAFConfig(GPIOD,GPIO_PinSource11,GPIO_AF_FSMC);


  readWriteTiming.FSMC_AddressSetupTime = 0XF;	 //地址建立时间（ADDSET）为16个HCLK 1/168M=6ns*16=96ns	
  readWriteTiming.FSMC_AddressHoldTime = 0x00;	 //地址保持时间（ADDHLD）模式A未用到	
  readWriteTiming.FSMC_DataSetupTime = 60;			//数据保存时间为60个HCLK	=6*60=360ns
  readWriteTiming.FSMC_BusTurnAroundDuration = 0x00;
  readWriteTiming.FSMC_CLKDivision = 0x00;
  readWriteTiming.FSMC_DataLatency = 0x00;
  readWriteTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 //模式A 
    

	writeTiming.FSMC_AddressSetupTime =9;	      //地址建立时间（ADDSET）为9个HCLK =54ns 
  writeTiming.FSMC_AddressHoldTime = 0x00;	 //地址保持时间（A		
  writeTiming.FSMC_DataSetupTime = 8;		 //数据保存时间为6ns*9个HCLK=54ns
  writeTiming.FSMC_BusTurnAroundDuration = 0x00;
  writeTiming.FSMC_CLKDivision = 0x00;
  writeTiming.FSMC_DataLatency = 0x00;
  writeTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 //模式A 

 
  FSMC_NORSRAMInitStructure.FSMC_Bank = FSMC_Bank1_NORSRAM1;//  这里我们使用NE4 ，也就对应BTCR[6],[7]。
  FSMC_NORSRAMInitStructure.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable; // 不复用数据地址
  FSMC_NORSRAMInitStructure.FSMC_MemoryType =FSMC_MemoryType_SRAM;// FSMC_MemoryType_SRAM;  //SRAM   
#if LCD_USE8BIT_MODEL 	
  FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_8b;//存储器数据宽度为8bit
#else
  FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;//存储器数据宽度为16bit   
#endif  
  FSMC_NORSRAMInitStructure.FSMC_BurstAccessMode =FSMC_BurstAccessMode_Disable;// FSMC_BurstAccessMode_Disable; 
  FSMC_NORSRAMInitStructure.FSMC_WaitSignalPolarity = FSMC_WaitSignalPolarity_Low;
	FSMC_NORSRAMInitStructure.FSMC_AsynchronousWait=FSMC_AsynchronousWait_Disable; 
  FSMC_NORSRAMInitStructure.FSMC_WrapMode = FSMC_WrapMode_Disable;   
  FSMC_NORSRAMInitStructure.FSMC_WaitSignalActive = FSMC_WaitSignalActive_BeforeWaitState;  
  FSMC_NORSRAMInitStructure.FSMC_WriteOperation = FSMC_WriteOperation_Enable;	//  存储器写使能
  FSMC_NORSRAMInitStructure.FSMC_WaitSignal = FSMC_WaitSignal_Disable;   
  FSMC_NORSRAMInitStructure.FSMC_ExtendedMode = FSMC_ExtendedMode_Enable; // 读写使用不同的时序
  FSMC_NORSRAMInitStructure.FSMC_WriteBurst = FSMC_WriteBurst_Disable; 
  FSMC_NORSRAMInitStructure.FSMC_ReadWriteTimingStruct = &readWriteTiming; //读写时序
  FSMC_NORSRAMInitStructure.FSMC_WriteTimingStruct = &writeTiming;  //写时序

  FSMC_NORSRAMInit(&FSMC_NORSRAMInitStructure);  //初始化FSMC配置

  FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);  // 使能BANK1 
	
	//重新配置写时序控制寄存器的时序使WR时序为最快   	 							    
	FSMC_Bank1E->BWTR[0]&=~(0XF<<0);//地址建立时间(ADDSET)清零 	 
	FSMC_Bank1E->BWTR[0]&=~(0XF<<8);//数据保存时间清零
	FSMC_Bank1E->BWTR[0]|=4<<0;		//地址建立时间(ADDSET)为4个HCLK =24ns  	 
	FSMC_Bank1E->BWTR[0]|=4<<8; 	//数据保存时间(DATAST)为6ns*4个HCLK=24ns
}

/*****************************************************************************
 * @name       :void LCD_RESET(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_RESET(void)
{
	LCD_RST=0;
	delay_ms(100);	
	LCD_RST=1;
	delay_ms(50);
}

/*****************************************************************************
 * @name       :void LCD_Init(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 	 
void LCD_Init(void)
{  
	LCD_GPIOInit();//LCD GPIO初始化	
	delay_ms(100);
	//LCD_RESET();    //如果不使用开发板复位引脚，则调用此复位函数
//************* SSD1963初始化**********//	
	LCD_WR_REG(0xE2);		//Set PLL with OSC = 10MHz (hardware),	Multiplier N = 35, 250MHz < VCO < 800MHz = OSC*(N+1), VCO = 300MHz
	LCD_WR_DATA(0x1D);		//参数1 
	LCD_WR_DATA(0x02);		//参数2 Divider M = 2, PLL = 300/(M+1) = 100MHz
	LCD_WR_DATA(0x04);		//参数3 Validate M and N values   
	delay_us(100);
	LCD_WR_REG(0xE0);		// Start PLL command
	LCD_WR_DATA(0x01);		// enable PLL
	delay_ms(10);
	LCD_WR_REG(0xE0);		// Start PLL command again
	LCD_WR_DATA(0x03);		// now, use PLL output as system clock	
	delay_ms(12);  
	LCD_WR_REG(0x01);		//软复位
	delay_ms(10);
		
	LCD_WR_REG(0xE6);		//设置像素频率,33Mhz
	LCD_WR_DATA(0x03);
	LCD_WR_DATA(0xFF);
	LCD_WR_DATA(0xFF);
		
	LCD_WR_REG(0xB0);		//设置LCD模式
	LCD_WR_DATA(0x20);		//24位模式
	LCD_WR_DATA(0x00);		//TFT 模式 
	
	LCD_WR_DATA((SSD_HOR_RESOLUTION-1)>>8);//设置LCD水平像素
	LCD_WR_DATA(SSD_HOR_RESOLUTION-1);		 
	LCD_WR_DATA((SSD_VER_RESOLUTION-1)>>8);//设置LCD垂直像素
	LCD_WR_DATA(SSD_VER_RESOLUTION-1);		 
	LCD_WR_DATA(0x00);		//RGB序列 
		
	LCD_WR_REG(0xB4);		//Set horizontal period
	LCD_WR_DATA((SSD_HT-1)>>8);
	LCD_WR_DATA(SSD_HT-1);
	LCD_WR_DATA(SSD_HPS>>8);
	LCD_WR_DATA(SSD_HPS);
	LCD_WR_DATA(SSD_HOR_PULSE_WIDTH-1);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_REG(0xB6);		//Set vertical period
	LCD_WR_DATA((SSD_VT-1)>>8);
	LCD_WR_DATA(SSD_VT-1);
	LCD_WR_DATA(SSD_VPS>>8);
	LCD_WR_DATA(SSD_VPS);
	LCD_WR_DATA(SSD_VER_FRONT_PORCH-1);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
		
	LCD_WR_REG(0xF0);	//设置SSD1963与CPU接口为16bit  
#if LCD_USE8BIT_MODEL 
	LCD_WR_DATA(0x00);	//8-bit(565 format) data for 16bpp 
#else
	LCD_WR_DATA(0x03);	//16-bit(565 format) data for 16bpp 
#endif
	LCD_WR_REG(0x29);	//开启显示
	//设置PWM输出  背光通过占空比可调 
	LCD_WR_REG(0xD0);	//设置自动白平衡DBC
	LCD_WR_DATA(0x00);	//disable
	
	LCD_WR_REG(0xBE);	//配置PWM输出
	LCD_WR_DATA(0x05);	//1设置PWM频率
	LCD_WR_DATA(0xFE);	//2设置PWM占空比
	LCD_WR_DATA(0x01);	//3设置C
	LCD_WR_DATA(0x00);	//4设置D
	LCD_WR_DATA(0x00);	//5设置E 
	LCD_WR_DATA(0x00);	//6设置F 
		
	LCD_WR_REG(0xB8);	//设置GPIO配置
	LCD_WR_DATA(0x03);	//2个IO口设置成输出
	LCD_WR_DATA(0x01);	//GPIO使用正常的IO功能 
	LCD_WR_REG(0xBA);
	LCD_WR_DATA(0X01);	//GPIO[1:0]=01,控制LCD方向
		
	LCD_PWM_BackLightSet(100);//背光设置为最亮

  LCD_direction(USE_HORIZONTAL);//设置LCD显示方向
	LCD_LED=1;//点亮背光	 
	LCD_Clear(WHITE);//清全屏白色
}
 
/*****************************************************************************
 * @name       :void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
								yStar:the bebinning y coordinate of the LCD display window
								xEnd:the endning x coordinate of the LCD display window
								yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
{	
	switch(lcddev.dir)
	{
		case 0:
			xStar=xStar+xEnd;
			xEnd=xStar-xEnd;
			xStar=xStar-xEnd;
			xStar=lcddev.width-xStar-1;
			xEnd=lcddev.width-xEnd-1;
		break;
		case 2:
			yStar=yStar+yEnd;
			yEnd=yStar-yEnd;
			yStar=yStar-yEnd;
			yStar=lcddev.height-yStar-1;
			yEnd=lcddev.height-yEnd-1;
		break;
		case 3:
			xStar=xStar+xEnd;
			xEnd=xStar-xEnd;
			xStar=xStar-xEnd;
			xStar=lcddev.width-xStar-1;
			xEnd=lcddev.width-xEnd-1;
			yStar=yStar+yEnd;
			yEnd=yStar-yEnd;
			yStar=yStar-yEnd;
			yStar=lcddev.height-yStar-1;
			yEnd=lcddev.height-yEnd-1;
		break;
		default:
		break;
	}
	LCD_WR_REG(lcddev.setxcmd);	
	LCD_WR_DATA(xStar>>8);
	LCD_WR_DATA(0x00FF&xStar);		
	LCD_WR_DATA(xEnd>>8);
	LCD_WR_DATA(0x00FF&xEnd);

	LCD_WR_REG(lcddev.setycmd);	
	LCD_WR_DATA(yStar>>8);
	LCD_WR_DATA(0x00FF&yStar);		
	LCD_WR_DATA(yEnd>>8);
	LCD_WR_DATA(0x00FF&yEnd);

	LCD_WriteRAM_Prepare();	//开始写入GRAM			
}   

/*****************************************************************************
 * @name       :void LCD_SetCursor(u16 Xpos, u16 Ypos)
 * @date       :2018-08-09 
 * @function   :Set coordinate value
 * @parameters :Xpos:the  x coordinate of the pixel
								Ypos:the  y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{	  	    			
	LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);	
} 

/*****************************************************************************
 * @name       :void LCD_direction(u8 direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
													2-180 degree
													3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(u8 direction)
{ 
	lcddev.wramcmd=0x2C;
	lcddev.rramcmd=0x2E;
	lcddev.dir=direction%4;
	switch(lcddev.dir){		  
		case 0:	
			lcddev.setxcmd=0x2B;
			lcddev.setycmd=0x2A;
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;		
			LCD_WriteReg(0x36,(0<<3)|(1<<7)|(0<<6)|(1<<5));
		break;
		case 1:
			lcddev.setxcmd=0x2A;
			lcddev.setycmd=0x2B;
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(0<<3)|(0<<7)|(0<<6)|(0<<5));
		break;
		case 2:
			lcddev.setxcmd=0x2B;
			lcddev.setycmd=0x2A;		
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;	
			LCD_WriteReg(0x36,(0<<3)|(0<<7)|(1<<6)|(1<<5));
		break;
		case 3:
			lcddev.setxcmd=0x2A;
			lcddev.setycmd=0x2B;
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(0<<3)|(1<<7)|(1<<6)|(0<<5));
		break;	
		default:break;
	}		
}	 

/*****************************************************************************
 * @name       :u16 LCD_Read_ID(void)
 * @date       :2018-11-13 
 * @function   :Read ID
 * @parameters :None
 * @retvalue   :ID value
******************************************************************************/ 
u16 LCD_Read_ID(void)
{
	u8 val[3] = {0};
	LCD_ReadReg(0xA1,val,3);
	return (val[1]<<8)|val[2];
}
