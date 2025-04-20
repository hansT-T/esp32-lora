/*
   HelTec Automation(TM) LoRaWAN 1.0.2 OTAA example use OTAA, CLASS A

   Function summary:

   - use internal RTC(150KHz);

   - Include stop mode and deep sleep mode;

   - 15S data send cycle;

   - Informations output via serial(115200);

   - Only ESP32 + LoRa series boards can use this library, need a license
     to make the code run(check you license here: http://www.heltec.cn/search/);

   You can change some definition in "Commissioning.h" and "LoRaMac-definitions.h"

   HelTec AutoMation, Chengdu, China.
   成都惠利特自动化科技有限公司
   https://heltec.org
   support@heltec.cn

  this project also release in GitHub:
  https://github.com/HelTecAutomation/ESP32_LoRaWAN
*/

/*------------------------------------------------------------

@Description : 用于snick实验的节点代码，主要就是先接收网关的下行消息，然后发送响应，保持delta在sx1276.c中实现
                因为网关的特殊性，所以这里的发送和接收频率不一致，中间涉及频率切换
                其它和pingpong并无差别，理解pingpong也能理解这个

@author : william

------------------------------------------------------------*/


#include "LoRaWan_APP.h"
#include "Arduino.h"


#define RF_FREQUENCY                                868000000 // Hz

#define TX_OUTPUT_POWER                             40        // dBm

#define LORA_BANDWIDTH                              0         // [0: 125 kHz,
//  1: 250 kHz,
//  2: 500 kHz,
//  3: Reserved]
#define LORA_SPREADING_FACTOR                       7         // [SF7..SF12]
#define LORA_CODINGRATE                             1         // [1: 4/5,
//  2: 4/6,
//  3: 4/7,
//  4: 4/8]
#define LORA_PREAMBLE_LENGTH                        8         // Same for Tx and Rx
#define LORA_SYMBOL_TIMEOUT                         0         // Symbols
#define LORA_FIX_LENGTH_PAYLOAD_ON                  false
#define LORA_IQ_INVERSION_ON                        false


#define RX_TIMEOUT_VALUE                            1000
#define BUFFER_SIZE                                 30 // Define the payload size here
#define REG_LR_IRQFLAGS                             0x12
#define RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL            0x02

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];
int counter = 0;
extern void write0(uint16_t address, uint8_t value);
static RadioEvents_t RadioEvents;
void OnTxDone( void );
void OnTxTimeout( void );
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr );

bool Irq2Fired = false;

extern uint8_t temp[10];

extern unsigned long int sysTime;


typedef enum
{
  STATUS_LOWPOWER,
  STATUS_RX,
  STATUS_TX
} States_t;


int16_t txNumber;
States_t state;
bool sleepMode = false;
int16_t Rssi, rxSize;
unsigned long long int x1, x2;
uint32_t  license[4] = {0xD5397DF0, 0x8573F814, 0x7A38C73D, 0x48E68607};

void interruptDio2() {
  //write0( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL );
  Irq2Fired = true;
  //Serial.println("Interrept\n");

}


long long getXTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

// Add your initialization code here
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  Mcu.begin();
  pinMode(34, INPUT);
  attachInterrupt(digitalPinToInterrupt(34), interruptDio2, RISING);

  txNumber = 0;
  Rssi = 0;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;
  RadioEvents.RxDone = OnRxDone;

  Radio.Init( &RadioEvents );
  Radio.SetPublicNetwork(true);
  Radio.SetChannel( RF_FREQUENCY );
  Radio.SetTxConfig( MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                     LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                     LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                     true, 0, 1000, LORA_IQ_INVERSION_ON, 3000 );

  Radio.SetRxConfig( MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                     LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                     LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                     0, true, 0, 1000, LORA_IQ_INVERSION_ON, true );
    state = STATUS_RX;
}


void loop()
{
  switch (state)
  {
    case STATUS_TX:
      //这里把写入payload的过程转到了onRxDone
      Radio.Send( (uint8_t *)txpacket, strlen(txpacket) );
      Serial.print(txpacket);
      Serial.println(counter);
      counter++;
      state = STATUS_LOWPOWER;
      break;
    case STATUS_RX:
      Serial.println("into RX mode");
      Radio.Rx( 0 );
      state = STATUS_LOWPOWER;
      break;
    case STATUS_LOWPOWER:
      Radio.IrqProcess( );
      break;
    default:
      break;
  }
}

//发送完成之后切换到rx状态，并且频率切换到接收的频率
void OnTxDone( void )
{
  //Serial.printf("TxTime:%llu\n",XTime);
  Serial.printf("TX done......\n");
  Radio.SetChannel( 868000000 );
  state = STATUS_RX;
}

void OnTxTimeout( void )
{
  //废弃了
  //Radio.Sleep( );
  Serial.printf("TX Timeout......\n");
  state = STATUS_TX;
}


//同上
void OnRxDone( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr )
{
  //Serial.printf("RxTime:%llu\n",XTime);
  Rssi = rssi;
  rxSize = size;
  memset(rxpacket, '\0', BUFFER_SIZE);
  memcpy(rxpacket, payload, rxSize );
  rxpacket[size] = '\0';
  Radio.Sleep( );

  Serial.printf("\r\nreceived packet \"%s\" with Rssi %d , length %d\r\n", rxpacket, Rssi, rxSize);
  Serial.println("wait to send next packet");
  sprintf(txpacket, "HELLO BACK");
  Radio.SetChannel( 868500000 );
  state = STATUS_TX;
}