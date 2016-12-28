#include "stdio.h" 
#include "string.h" 
#include "bsp.h" 
#include "CC1101.h"

// USART_CSB
#define Dis_Len 2
volatile u8 Distance[Dis_Len] = {0, 0};
volatile u8 Dis_Index = 0;
volatile u8 distance = 0; 

// CC1101
volatile u16  Cnt1ms = 0;     // 1ms����������ÿ1ms��һ 
int  RecvWaitTime = 0;        // ���յȴ�ʱ��                
u16  SendCnt = 0;             // �������͵����ݰ���                

                           // ֡ͷ  Դ��ַ  Ŀ���ַ  distance*10  ֡β
u8 SendBuffer[SEND_LENGTH] = {0x55,   0,    0xff,     15,          0xaa}; // �ӻ�����������
                           // ֡ͷ  Դ��ַ  Ŀ���ַ  ֡β
u8 AckBuffer[ACK_LENGTH]   = {0x55,  0xff,     0,     0xaa};        // ����Ӧ������
             
void System_Initial(void);                     // ϵͳ��ʼ��
u8   RF_SendPacket(u8 *Sendbuffer, u8 length);  // �ӻ��������ݰ�
void Get_TheTime(void);
void RTC_AWU_Initial(uint16_t time);            // time * 26.95 ms 
void DelayMs(u16 x);                            // TIM3��ʱ����
u8   Measured_Range(void);                      // ���������

// printf֧��
int putchar(int c)   
{  
    while(!USART_GetFlagStatus(USART1, USART_FLAG_TXE));//�ȴ��������
    USART_SendData8(USART1, (uint8_t)c);
    return (c);  
}

void main(void)
{
    u8 SendError_Time = 0;                             // �������ͳ������
    volatile u8 res = 0;
    volatile u8 Timer_30s = 6;                        // �ϵ緢��
    float ADC_Value = 0.0f;
       
    System_Initial();                                 // ��ʼ��ϵͳ��������               
    CC1101Init();                                     // ��ʼ��CC1101Ϊ����ģʽ 
    SendBuffer[1] = TX_Address;                       // ���ݰ�Դ��ַ���ӻ���ַ��
    
    while(1)
    {
        printf("Timer_30s=%d\r\n", (int)Timer_30s);  
        if(Timer_30s++ == 6)                   // Լ 3 Min     30s * 6
        {
            SWITCH_ON();                       // ��ͨCC1101��CSB��Դ
            LED_ON();                          // LED��˸������ָʾ���ͳɹ�
            CSB_Initial();                     // ��ʼ��������ģ��
            CC1101Init();                      // ��ʼ��CC1101ģ��
            SendError_Time = 0;                // �����������
              
            distance = Measured_Range();       // ��������� 
            if(distance)  
            {
                SendBuffer[3] = distance;
                printf("distance = %d cm\r\n", distance);
            }
            else 
            {
                SendBuffer[3] = 0;             // ��������  ����0
                printf("Measured_Error\r\n");
            } 
send:            
            res = RF_SendPacket(SendBuffer, SEND_LENGTH);
            if(res != 0) 
            {
                printf("Send ERROR:%d\r\n", (int)res);  // ����ʧ��
                DelayMs(25);
                if(++SendError_Time < 20) goto send;   //  ��������ﵽ20�Σ�������˴δ���
                printf("Send Canceled!\r\n");  // ����ʧ��
            }
            else printf("Send OK!\r\n");              // ���ͳɹ�
            
            SWITCH_OFF();
            LED_OFF();
            Timer_30s = 1;
        }
        RTC_AWU_Initial(1116);     // RTC �����ж�    1116 * 26.95 ms = 30s
        halt();//������͹���
    }
}

/*===========================================================================
* ���� : DelayMs() => ��ʱ����(ms��)                                        *
* ���� ��x, ��Ҫ��ʱ����(0-65535)                                             *
============================================================================*/
void DelayMs(u16 x)
{
    u16 timer_ms = x;
    
    Cnt1ms = 0;
    TIM3_Set(1);
    while(Cnt1ms < timer_ms);
    TIM3_Set(0);
}

/*===========================================================================
* ���� ��TIM3_1MS_ISR() => ��ʱ��3������, ��ʱʱ���׼Ϊ1ms               *
============================================================================*/
void TIM3_1MS_ISR(void)
{
    Cnt1ms++;
    if(RecvWaitTime > 0) RecvWaitTime--;    // ���ݽ��ռ�ʱ
}

/*===========================================================================
* ����: System_Initial() => ��ʼ��ϵͳ��������                              *
============================================================================*/
void System_Initial(void)
{
    SClK_Initial();         // ��ʼ��ϵͳʱ�ӣ�16M / 4 = 4M    
    GPIO_Initial();         // ��ʼ��GPIO   LED  SWITCH
 
    CSB_Initial();          // ��ʼ��������ģ��
    USART1_Initial();       // ��ʼ������1  ������ģ��ʹ�� 
    TIM3_Initial();         // ��ʼ����ʱ��3����׼1ms  
    SPI_Initial();          // ��ʼ��SPI  
    ADC_Initial();          // ��ʼ��ADC
    
    //RTC_Initial();            // ��ʼ��RTC   LSI
    //RTC_AWU_Initial(186);     // RTC �����ж�    186 * 26.95 ms = 5s
    enableInterrupts();     // ʹ��ϵͳ���ж�
    
    printf("Oil_Can_Drone\r\n");                      // �����ַ�����ĩβ����
}

/*===========================================================================
* ���� : BSP_RF_SendPacket() => ���߷������ݺ���                            *
* ���� ��Sendbufferָ������͵����ݣ�length�������ݳ���                      *
* ��� ��0�����ͳɹ�                                                      
         1���ȴ�Ӧ��ʱ
         2�����ݰ����ȴ���
         3�����ݰ�֡ͷ����
         4�����ݰ�Դ��ַ����
         5�����ݰ�Ŀ���ַ����
         6�����ݰ�֡β
         7��Ӧ���źŴ���
============================================================================*/
INT8U RF_SendPacket(INT8U *Sendbuffer, INT8U length)
{
    uint8_t  i = 0, ack_len = 0, ack_buffer[10] = {0};
    RecvWaitTime = (int)RECV_TIMEOUT;           // �ȴ�Ӧ��ʱ����1500ms
    
    CC1101SendPacket(SendBuffer, length, ADDRESS_CHECK);    // �������� 
    CC1101SetTRMode(RX_MODE);                               // ׼������Ӧ��

    TIM3_Set(1);                                // ����TIM3
    //printf("waiting for ack...\r\n");
    while(CC_IRQ_READ() != 0)                   // �ȴ��������ݰ�
    {
        if(RecvWaitTime <= 0)      
        {  
            TIM3_Set(0);                            // �ر�TIM3
            return 1;                              // �ȴ�Ӧ��ʱ
        }
    }
    RecvWaitTime = 50;           // �ȴ�Ӧ��ʱ����50ms
    while(CC_IRQ_READ() == 0)
    {
        if(RecvWaitTime <= 0)      
        {  
            TIM3_Set(0);                            // �ر�TIM3
            return 7;                              // �ȴ�Ӧ��ʱ
        }
    }
//    printf("RecvWaitTime2=%d\r\n", RecvWaitTime);
    TIM3_Set(0);                                // �ر�TIM3
    ack_len = CC1101RecPacket(ack_buffer);      // ��ȡ�յ�������
    
    if(ack_len <= 0 || ack_len > 10)  
    {
        CC1101Init(); 
        //printf("ack_len1=%d\r\n", ack_len);
        return 2;                                          // ���ݰ����ȴ���
    }
    if(ack_len != ACK_LENGTH) return 2;                    // ���ݰ����ȴ���
    if(ack_buffer[0] != 0x55) return 3;                    // ���ݰ�֡ͷ����
    if(ack_buffer[1] != 0xff) return 4;                    // ���ݰ�Դ��ַ����       
    if(ack_buffer[2] == 0xff) return 5;                    // ���ݰ�Ŀ���ַ����
    if(ack_buffer[3] != 0xaa) return 6;            // ���ݰ�֡β

    // Ӧ����ȷ
    printf("ack_len=%d;ack_buffer:", (int)ack_len);
    for(i = 0; i < ack_len; i++)                     
    {
        printf("%d ", (int)ack_buffer[i]);
    }
    printf("\r\n");

    return 0;  
}

void Get_TheTime(void)
{
  RTC_TimeTypeDef GETRTC_Time;
  RTC_DateTypeDef GETRTC_Data;
  //unsigned char sec_st,sec_su , min_mt,min_mu ,hour_ht , hour_hu , midd ,status;
  if(RTC_GetFlagStatus(RTC_FLAG_RSF) == SET)  //��ʱ����� 
  {
    
    
    RTC_GetDate(RTC_Format_BIN , &GETRTC_Data);
    RTC_GetTime(RTC_Format_BIN , &GETRTC_Time);  
      
     RTC_ClearFlag(RTC_FLAG_RSF);   //�����־
     printf("20%d/%d/%d Day%d %d:%d:%d\r\n" , GETRTC_Data.RTC_Year , GETRTC_Data.RTC_Month  , GETRTC_Data.RTC_Date  ,  GETRTC_Data.RTC_WeekDay ,GETRTC_Time.RTC_Hours , GETRTC_Time.RTC_Minutes , GETRTC_Time.RTC_Seconds);
  }
}

void RTC_AWU_Initial(uint16_t time)    // time * 26.95 ms 
{ 
    RTC_DeInit(); //��ʼ��Ĭ��״̬ 
    
    CLK_PeripheralClockConfig(CLK_Peripheral_RTC, ENABLE);      // ����RTCʱ�� 
    CLK_RTCClockConfig(CLK_RTCCLKSource_LSI, CLK_RTCCLKDiv_64); // ѡ��RTCʱ��ԴLSI/64=593.75Hz 
    RTC_WakeUpClockConfig(RTC_WakeUpClock_RTCCLK_Div16);        // 593.75Hz/16=37.109375Hz t = 26.95ms 
    RTC_ITConfig(RTC_IT_WUT, ENABLE);  // �����ж� 
    RTC_SetWakeUpCounter(time);        // ����RTC Weakup��������ֵ 
    RTC_WakeUpCmd(ENABLE);             // ʹ���Զ����� 
} 

// ���ؾ���   0~255  cm
// 0:��������
u8 Measured_Range(void)
{
    u8 distance_cm, error_timer = 0;
    
Detectde:	
    distance_cm = 0;
    Distance[0] = 0;    // ���㣬���²��
    Distance[1] = 0;    
    Dis_Index = 0;
    CSB_Wakeup();
    DelayMs(1);       // ����50us ����
    
    DelayMs(4);       // ϵͳ����3ms�󣬷��Ͳ�ഥ���ź�0x55  
    
    //U1_Set(1);        // ����U1�����жϣ�׼�����ղ������
    while(!USART_GetFlagStatus(USART1, USART_FLAG_TXE));//�ȴ��������
    USART_SendData8(USART1, 0x55); 
    
    DelayMs(25);      // �ȴ����ڷ��ز������   25
    CSB_Sleep(); 
    //U1_Set(0);        // �رմ���1
    
    if(Dis_Index == Dis_Len) // ok
    {
        distance_cm = ( (( (u16)Distance[0] << 8 ) + Distance[1]) / 10 ) & 0xff;    // �޶�distance_cm��[0, 255]��Χ��
        return distance_cm;
    }
    else
    {
        if(++error_timer == 10) return 0;     // ����������0
        goto Detectde;
    }
}

//    // CSB����
//    while(1)
//    {
//        distance = Measured_Range();    // ��� 
//        if(distance)  
//        {
//            LED_ON();
//            printf("distance = %d cm\r\n", distance);
//        }
//        else 
//        {
//            LED_OFF();
//            printf("Measured_Error\r\n");
//        } 
//        
//        DelayMs(1000); 
//    }	
//    // ADC+RTC���� 
//    while(1)
//    {
//        ADC_Value = ADC_Data_Read();                  // PA6
//        ADC_Value = ADC_Value / 0x0FFF * Voltage_Refer;
//        printf("ADC_Value = %.2f V\r\n", ADC_Value);  
//        Get_TheTime();
//        DelayMs(1000);DelayMs(1000);
//    }
    
//    // RTC-AWU����
//    while(1)
//    {
//        LED_TOG();                // LED��˸������ָʾ���ͳɹ�
//        printf("OK!\r\n");            
//        RTC_AWU_Initial(186);     // RTC �����ж�    186 * 26.95 ms = 5s
//        halt();//������͹���
//    }
    
//    // ͨ�Ų���
//    while(1)
//    {
//        LED_ON();                          // LED��˸������ָʾ���ͳɹ�
// send:        
//        res = RF_SendPacket(SendBuffer, SEND_LENGTH);
//        if(res != 0) 
//        {
//          printf("Send ERROR:%d\r\nRetry now...\r\n", (int)res);  // ����ʧ��
//          DelayMs(15);
//          goto send;
//        }
//        else  printf("Send OK!\r\n");              // ���ͳɹ�
//        LED_OFF();
//        DelayMs(1000);DelayMs(1000);DelayMs(1000);DelayMs(1000);DelayMs(1000);
//    }