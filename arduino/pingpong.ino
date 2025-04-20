/*
 * HelTec Automation(TM) LoRaWAN 1.0.2 OTAA example use OTAA, CLASS A
 *
 * Function summary:
 *
 * - use internal RTC(150KHz);
 *
 * - Include stop mode and deep sleep mode;
 *
 * - 15S data send cycle;
 *
 * - Informations output via serial(115200);
 *
 * - Only ESP32 + LoRa series boards can use this library, need a license
 *   to make the code run(check you license here: http://www.heltec.cn/search/);
 *
 * You can change some definition in "Commissioning.h" and "LoRaMac-definitions.h"
 *
 * HelTec AutoMation, Chengdu, China.
 * 成都惠利特自动化科技有限公司
 * https://heltec.org
 * support@heltec.cn
 *
 *this project also release in GitHub:
 *https://github.com/HelTecAutomation/ESP32_LoRaWAN
*/

/*-------------------------------------------------------------------------------------

@Hardware : esp32 + sx1276 (heltec wifi lora v2)
@Description : pingpong程序用于控制节点发送和接收消息，发了之后进入接收状态，等待接收，接收完了之后又切换到发送，一发一收，循环。
@author : william
@comment : 所有的程序都可基于这个程序改动而来，如果想一直发，就TX之后继续保持TX，想一直收就RX之后继续保持RX。或者是先接收再发送。
            或是换频率发送，换功率发送等等。


---------------------------------------------------------------------------------------*/


#include "LoRaWan_APP.h"
#include "Arduino.h"


//定义一堆射频参数

//频率
#define RF_FREQUENCY                                868500000 // Hz

//功率
#define TX_OUTPUT_POWER                             15        // dBm

//带宽
#define LORA_BANDWIDTH                              0         // [0: 125 kHz,
                                                              //  1: 250 kHz,
                                                              //  2: 500 kHz,
                                                              //  3: Reserved]
//扩频因子
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]

//编码率
#define LORA_CODINGRATE                             1         // [1: 4/5,
                                                              //  2: 4/6,
                                                              //  3: 4/7,
                                                              //  4: 4/8]

//前导码长度
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx

//基本没用
#define LORA_SYMBOL_TIMEOUT                         0         // Symbols 
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false
#define RX_TIMEOUT_VALUE                            1000


//发送的最大长度
#define BUFFER_SIZE                                 30 // Define the payload size here

//废弃方案
#define REG_LR_IRQFLAGS                             0x12
#define RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL            0x02

//定义发送和接收的payload
char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

//引入外部的写函数
extern void write0(uint16_t address, uint8_t value);

//射频事件
static RadioEvents_t RadioEvents;
void OnTxDone( void );
void OnTxTimeout( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );

//新增的用于跳频的中断信号，irq0和irq1被官方在mcu.s中定义了
bool Irq2Fired = false;

//状态类型，用于切换
typedef enum
{
    STATUS_LOWPOWER,
    STATUS_RX,
    STATUS_TX
}States_t;

//
int16_t txNumber;
States_t state;

//废弃了
bool sleepMode = false;

//接收参数，用于打印
int16_t Rssi,rxSize;

//官方为了防止第三方硬件使用相应的库
uint32_t  license[4] = {0xD5397DF0, 0x8573F814, 0x7A38C73D, 0x48E68607};


/*----------------------------------------------------------

@Description : 绑定dio2中断信号,dio2触发后就会把irq2fired设为true，然后在Radio.IrqProcess( )函数中就会处理对应的中断，（定义在sx1276.c中）
@author : William

---------------------------------------------------------*/
void interruptDio2(){
  //write0( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL );
  Irq2Fired = true;
  //Serial.println("Interrept\n");
  
}


// Add your initialization code here
void setup()
{
    //设置波特率并启动串口，esp32默认
    Serial.begin(115200);
    while (!Serial);

    //启动mcu，官方定义的代码，校验licence，绑定dio0和dio1中断，还有设置esp32和sx1276的引脚之类的
    Mcu.begin();

    //绑定dio2，dio2在引脚图中为34号
    pinMode(34,INPUT);
    attachInterrupt(digitalPinToInterrupt(34),interruptDio2,RISING);
  
    txNumber=0;
    Rssi=0;

    //绑定射频事件
    RadioEvents.TxDone = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxDone = OnRxDone;

    //初始化radio
    Radio.Init( &RadioEvents );

    //设置是否可以与网关通信，同步字不一样，但只要双方一致就可通信，节点和节点一致也可以通信。
    Radio.SetPublicNetwork(true);

    //设置频率
    Radio.SetChannel( RF_FREQUENCY );

    //设置发送和接收参数，具体见sx1276.h中有详细解释，跳频也在这里面开启
    Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                                   LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                                   LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                                   true, 1, 1000, LORA_IQ_INVERSION_ON, 3000 );

    Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                                   LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                                   LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                                   0, true, 1, 1000, LORA_IQ_INVERSION_ON, true );

    //初始化状态，表示启动之后先TX还是先RX
    state=STATUS_TX;
}

//主要的循环，mcu启动后就一直执行这个函数
void loop()
{
    //主函数就是不停判断状态，然后执行对应的部分
    switch(state)
    {
        //发送
        case STATUS_TX:
            delay(2500);
            txNumber++;
            //用sprintf函数把要发送的内容写入payload
            sprintf(txpacket,"%s","hello#");
            //发送函数
            Radio.Send( (uint8_t *)txpacket, strlen(txpacket) );
            //切换到LOWPOWER状态，就是一直等待中断信号，会在sx1276.c中去处理
            state=STATUS_LOWPOWER;
            break;
        //接收
        case STATUS_RX:
            Serial.println("into RX mode");
            //0表示一直监听，其余值表示监听多久之后放弃监听，也许会转到RxTimeOut。（好像试过，然后没用，有点忘了，应该是有用的）
            Radio.Rx( 0 );
            state=STATUS_LOWPOWER;
            break;
        case STATUS_LOWPOWER:
            //一直调用这个函数，等待中断信号
            Radio.IrqProcess( );
            break;
        default:
            break;
    }
}

//会在sx1276.c中的处理TXDOne的中断函数的时候调用———————>RadioEvents->TxDone( );
//可以修改这个函数来处理你想达成的，此处就是发送完之后切换到接收状态
void OnTxDone( void )
{
  Serial.print("TX done......");
  state=STATUS_RX;
}

//没用
void OnTxTimeout( void )
{   
    //radio.sleep函数废弃了
    //Radio.Sleep( );
    //Radio.IrqProcess();
    Serial.print("TX Timeout......");
    state=STATUS_TX;
}

//同上
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
    Rssi=rssi;
    rxSize=size;
    memset(rxpacket,'\0',BUFFER_SIZE);
    memcpy(rxpacket, payload, size );
    rxpacket[size]='\0';
    Radio.Sleep( );

    Serial.printf("\r\nreceived packet \"%c\" with Rssi %d , length %d\r\n",rxpacket[0],Rssi,rxSize);
    Serial.println("wait to send next packet");

    state=STATUS_TX;
}