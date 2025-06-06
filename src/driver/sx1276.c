/*!
 * \file      sx1276.c
 *
 * \brief     SX1276 driver implementation
 *
 * \copyright Revised BSD License, see section \ref LICENSE.
 *
 * \code
 *                ______                              _
 *               / _____)             _              | |
 *              ( (____  _____ ____ _| |_ _____  ____| |__
 *               \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 *               _____) ) ____| | | || |_| ____( (___| | | |
 *              (______/|_____)_|_|_| \__)_____)\____)_| |_|
 *              (C)2013-2017 Semtech
 *
 * \endcode
 *
 * \author    Miguel Luis ( Semtech )
 *
 * \author    Gregory Cristian ( Semtech )
 *
 * \author    Wael Guibene ( Semtech )
 */
 #if defined(WIFI_LoRa_32)||defined(WIFI_LoRa_32_V2)||defined(wireless_stick)||defined(wireless_stick_lite)||defined(wireless_bridge)
#include <math.h>
#include <string.h>
#include "../loramac/utilities.h"
#include "../driver/timer.h"
#include "../radio/radio_sx127x.h"
#include "../driver/delay.h"
#include "../driver/sx1276-board.h"
#include "../driver/debug.h"
#include<time.h>
extern  void lora_printf(const char *format, ...);


/*------------------------------------------------------

@Description : 定义全局变量，记录时间戳
@author : william

------------------------------------------------------*/

unsigned long int sysTime;              //系统时间，触发RxDone和TxDone中断时会记录这个时间，还有要随消息发送时会更新。
unsigned long int t1,t2;                //用于Secure Identification实验来保证Delta的两个时间，时间同步不用
unsigned long int freqHoppingTime;      //用于记录触发跳频中断时的时间，主要是接收使用。
static uint8_t Time[10];                //用于将系统时间转为字符串，便于发送

/*--------------------------------------------------------------

@Description : 获取当前系统时间，并将系统时间转为字符串，存储在Time中，用于随消息发送
@author : William

----------------------------------------------------------------*/
void getTimeChar(){
    char t_data[20];
    ultoa(freqHoppingTime,t_data,10);
    memset(Time,'0',sizeof(Time));
    int length = strlen(t_data);
    for(int i=0;i<length;i++){
        Time[9-i]=t_data[length-i-1];
    }
}


/*!
 * Radio registers definition
 */
typedef struct
{
    RadioModems_t Modem;
    uint8_t       Addr;
    uint8_t       Value;
}RadioRegisters_t;

/*!
 * FSK bandwidth definition
 */
typedef struct
{
    uint32_t bandwidth;
    uint8_t  RegValue;
}FskBandwidth_t;


/*!
 * Performs the Rx chain calibration for LF and HF bands
 * \remark Must be called just after the reset so all registers are at their
 *         default values
 */
static void RxChainCalibration( void );

/*!
 * \brief Sets the SX1276 in transmission mode for the given time
 * \param [IN] timeout Transmission timeout [ms] [0: continuous, others timeout]
 */
void SX1276SetTx( uint32_t timeout );

/*!
 * \brief Writes the buffer contents to the SX1276 FIFO
 *
 * \param [IN] buffer Buffer containing data to be put on the FIFO.
 * \param [IN] size Number of bytes to be written to the FIFO
 */
void SX1276WriteFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Reads the contents of the SX1276 FIFO
 *
 * \param [OUT] buffer Buffer where to copy the FIFO read data.
 * \param [IN] size Number of bytes to be read from the FIFO
 */
void SX1276ReadFifo( uint8_t *buffer, uint8_t size );

/*!
 * \brief Sets the SX1276 operating mode
 *
 * \param [IN] opMode New operating mode
 */
void SX1276SetOpMode( uint8_t opMode );

/*
 * SX1276 DIO IRQ callback functions prototype
 */

/*!
 * \brief DIO 0 IRQ callback
 */
void SX1276OnDio0Irq( void );

/*!
 * \brief DIO 1 IRQ callback
 */
void SX1276OnDio1Irq( void );

/*!
 * \brief DIO 2 IRQ callback
 */
void SX1276OnDio2Irq( void );

/*!
 * \brief DIO 3 IRQ callback
 */
void SX1276OnDio3Irq( void );

/*!
 * \brief DIO 4 IRQ callback
 */
void SX1276OnDio4Irq( void );

/*!
 * \brief DIO 5 IRQ callback
 */
void SX1276OnDio5Irq( void );

/*!
 * \brief Tx & Rx timeout timer callback
 */
void SX1276OnTimeoutIrq( void );

/*
 * Private global constants
 */

/*!
 * Radio hardware registers initialization
 *
 * \remark RADIO_INIT_REGISTERS_VALUE is defined in sx1276-board.h file
 */
const RadioRegisters_t RadioRegsInit[] = RADIO_INIT_REGISTERS_VALUE;

/*!
 * Constant values need to compute the RSSI value
 */
#define RSSI_OFFSET_LF                              -164
#define RSSI_OFFSET_HF                              -157

/*!
 * Precomputed FSK bandwidth registers values
 */
const FskBandwidth_t FskBandwidths[] =
{
    { 2600  , 0x17 },
    { 3100  , 0x0F },
    { 3900  , 0x07 },
    { 5200  , 0x16 },
    { 6300  , 0x0E },
    { 7800  , 0x06 },
    { 10400 , 0x15 },
    { 12500 , 0x0D },
    { 15600 , 0x05 },
    { 20800 , 0x14 },
    { 25000 , 0x0C },
    { 31300 , 0x04 },
    { 41700 , 0x13 },
    { 50000 , 0x0B },
    { 62500 , 0x03 },
    { 83333 , 0x12 },
    { 100000, 0x0A },
    { 125000, 0x02 },
    { 166700, 0x11 },
    { 200000, 0x09 },
    { 250000, 0x01 },
    { 300000, 0x00 }, // Invalid Bandwidth
};

/*
 * Private global variables
 */

/*!
 * Radio callbacks variable
 */
static RadioEvents_t *RadioEvents;

/*!
 * Reception buffer
 */
static uint8_t RxTxBuffer[RX_BUFFER_SIZE];

/*
 * Public global variables
 */

/*!
 * Radio hardware and global parameters
 */
SX1276_t SX1276;

bool Irq0Fired = false;
bool Irq1Fired = false;

 void Dio0Irq()
{
    Irq0Fired = true;
}
void Dio1Irq()
{
    Irq1Fired = true;
}

/*!
 * Hardware DIO IRQ callback initialization
 */
DioIrqHandler *DioIrq[] = { Dio0Irq, Dio1Irq,
                            SX1276OnDio2Irq, SX1276OnDio3Irq,
                            SX1276OnDio4Irq, NULL };

/*!
 * Tx and Rx timers
 */
TimerEvent_t TxTimeoutTimer;
TimerEvent_t RxTimeoutTimer;
TimerEvent_t RxTimeoutSyncWord;

#if defined( USE_RADIO_DEBUG )
Gpio_t DbgPinTx;
Gpio_t DbgPinRx;
#endif

/*
 * Radio driver functions implementation
 */


void SX1276Init( RadioEvents_t *events )
{
    uint8_t i;

    RadioEvents = events;

    SX1276IoInit();

    SX1276Reset( );

    SX1276IoIrqInit( DioIrq );

    // Initialize driver timeout timers
    TimerInit( &TxTimeoutTimer, SX1276OnTimeoutIrq );
    TimerInit( &RxTimeoutTimer, SX1276OnTimeoutIrq );
    TimerInit( &RxTimeoutSyncWord, SX1276OnTimeoutIrq );

    RxChainCalibration( );

    SX1276SetOpMode( RF_OPMODE_SLEEP );


    for( i = 0; i < sizeof( RadioRegsInit ) / sizeof( RadioRegisters_t ); i++ )
    {
        SX1276SetModem( RadioRegsInit[i].Modem );
        SX1276Write( RadioRegsInit[i].Addr, RadioRegsInit[i].Value );
    }

    SX1276SetModem( MODEM_FSK );

    SX1276.Settings.State = RF_IDLE;
}

RadioState_t SX1276GetStatus( void )
{
    return SX1276.Settings.State;
}

void SX1276SetChannel( uint32_t freq )
{
    SX1276.Settings.Channel = freq;
    freq = ( uint32_t )( ( double )freq / ( double )FREQ_STEP );
    SX1276Write( REG_FRFMSB, ( uint8_t )( ( freq >> 16 ) & 0xFF ) );
    SX1276Write( REG_FRFMID, ( uint8_t )( ( freq >> 8 ) & 0xFF ) );
    SX1276Write( REG_FRFLSB, ( uint8_t )( freq & 0xFF ) );
}

bool SX1276IsChannelFree( RadioModems_t modem, uint32_t freq, int16_t rssiThresh, uint32_t maxCarrierSenseTime )
{
    bool status = true;
    int16_t rssi = 0;
    uint32_t carrierSenseTime = 0;

    SX1276SetModem( modem );

    SX1276SetChannel( freq );

    SX1276SetOpMode( RF_OPMODE_RECEIVER );

    DelayMs( 1 );

    carrierSenseTime = TimerGetCurrentTime( );

    // Perform carrier sense for maxCarrierSenseTime
    while( TimerGetElapsedTime( carrierSenseTime ) < maxCarrierSenseTime )
    {
        rssi = SX1276ReadRssi( modem );

        if( rssi > rssiThresh )
        {
            status = false;
            break;
        }
    }
    SX1276SetSleep( );
    return status;
}

uint32_t SX1276Random( void )
{
    uint8_t i;
    uint32_t rnd = 0;

    /*
     * Radio setup for random number generation
     */
    // Set LoRa modem ON
    SX1276SetModem( MODEM_LORA );

    // Disable LoRa modem interrupts
    SX1276Write( REG_LR_IRQFLAGSMASK, RFLR_IRQFLAGS_RXTIMEOUT |
                  RFLR_IRQFLAGS_RXDONE |
                  RFLR_IRQFLAGS_PAYLOADCRCERROR |
                  RFLR_IRQFLAGS_VALIDHEADER |
                  RFLR_IRQFLAGS_TXDONE |
                  RFLR_IRQFLAGS_CADDONE |
                  RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                  RFLR_IRQFLAGS_CADDETECTED );

    // Set radio in continuous reception
    SX1276SetOpMode( RF_OPMODE_RECEIVER );

    for( i = 0; i < 32; i++ )
    {
        DelayMs( 1 );
        // Unfiltered RSSI value reading. Only takes the LSB value
        rnd |= ( ( uint32_t )SX1276Read( REG_LR_RSSIWIDEBAND ) & 0x01 ) << i;
    }

    SX1276SetSleep( );

    return rnd;
}

/*!
 * Performs the Rx chain calibration for LF and HF bands
 * \remark Must be called just after the reset so all registers are at their
 *         default values
 */
static void RxChainCalibration( void )
{
    uint8_t regPaConfigInitVal;
    uint32_t initialFreq;

    // Save context
    regPaConfigInitVal = SX1276Read( REG_PACONFIG );
    initialFreq = ( double )( ( ( uint32_t )SX1276Read( REG_FRFMSB ) << 16 ) |
                              ( ( uint32_t )SX1276Read( REG_FRFMID ) << 8 ) |
                              ( ( uint32_t )SX1276Read( REG_FRFLSB ) ) ) * ( double )FREQ_STEP;

    // Cut the PA just in case, RFO output, power = -1 dBm
    SX1276Write( REG_PACONFIG, 0x00 );

    // Launch Rx chain calibration for LF band
    SX1276Write( REG_IMAGECAL, ( SX1276Read( REG_IMAGECAL ) & RF_IMAGECAL_IMAGECAL_MASK ) | RF_IMAGECAL_IMAGECAL_START );
    while( ( SX1276Read( REG_IMAGECAL ) & RF_IMAGECAL_IMAGECAL_RUNNING ) == RF_IMAGECAL_IMAGECAL_RUNNING )
    {
    }

    // Sets a Frequency in HF band
    SX1276SetChannel( 868000000 );

    // Launch Rx chain calibration for HF band
    SX1276Write( REG_IMAGECAL, ( SX1276Read( REG_IMAGECAL ) & RF_IMAGECAL_IMAGECAL_MASK ) | RF_IMAGECAL_IMAGECAL_START );
    while( ( SX1276Read( REG_IMAGECAL ) & RF_IMAGECAL_IMAGECAL_RUNNING ) == RF_IMAGECAL_IMAGECAL_RUNNING )
    {
    }

    // Restore context
    SX1276Write( REG_PACONFIG, regPaConfigInitVal );
    SX1276SetChannel( initialFreq );
}

/*!
 * Returns the known FSK bandwidth registers value
 *
 * \param [IN] bandwidth Bandwidth value in Hz
 * \retval regValue Bandwidth register value.
 */
static uint8_t GetFskBandwidthRegValue( uint32_t bandwidth )
{
    uint8_t i;

    for( i = 0; i < ( sizeof( FskBandwidths ) / sizeof( FskBandwidth_t ) ) - 1; i++ )
    {
        if( ( bandwidth >= FskBandwidths[i].bandwidth ) && ( bandwidth < FskBandwidths[i + 1].bandwidth ) )
        {
            return FskBandwidths[i].RegValue;
        }
    }
    // ERROR: Value not found
    while( 1 );
}

void SX1276SetRxConfig( RadioModems_t modem, uint32_t bandwidth,
                         uint32_t datarate, uint8_t coderate,
                         uint32_t bandwidthAfc, uint16_t preambleLen,
                         uint16_t symbTimeout, bool fixLen,
                         uint8_t payloadLen,
                         bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                         bool iqInverted, bool rxContinuous )
{
    SX1276SetModem( modem );

    switch( modem )
    {
    case MODEM_FSK:
        {
            SX1276.Settings.Fsk.Bandwidth = bandwidth;
            SX1276.Settings.Fsk.Datarate = datarate;
            SX1276.Settings.Fsk.BandwidthAfc = bandwidthAfc;
            SX1276.Settings.Fsk.FixLen = fixLen;
            SX1276.Settings.Fsk.PayloadLen = payloadLen;
            SX1276.Settings.Fsk.CrcOn = crcOn;
            SX1276.Settings.Fsk.IqInverted = iqInverted;
            SX1276.Settings.Fsk.RxContinuous = rxContinuous;
            SX1276.Settings.Fsk.PreambleLen = preambleLen;
            SX1276.Settings.Fsk.RxSingleTimeout = ( uint32_t )( symbTimeout * ( ( 1.0 / ( double )datarate ) * 8.0 ) * 1000 );

            datarate = ( uint16_t )( ( double )XTAL_FREQ / ( double )datarate );
            SX1276Write( REG_BITRATEMSB, ( uint8_t )( datarate >> 8 ) );
            SX1276Write( REG_BITRATELSB, ( uint8_t )( datarate & 0xFF ) );

            SX1276Write( REG_RXBW, GetFskBandwidthRegValue( bandwidth ) );
            SX1276Write( REG_AFCBW, GetFskBandwidthRegValue( bandwidthAfc ) );

            SX1276Write( REG_PREAMBLEMSB, ( uint8_t )( ( preambleLen >> 8 ) & 0xFF ) );
            SX1276Write( REG_PREAMBLELSB, ( uint8_t )( preambleLen & 0xFF ) );

            if( fixLen == 1 )
            {
                SX1276Write( REG_PAYLOADLENGTH, payloadLen );
            }
            else
            {
                SX1276Write( REG_PAYLOADLENGTH, 0xFF ); // Set payload length to the maximum
            }

            SX1276Write( REG_PACKETCONFIG1,
                         ( SX1276Read( REG_PACKETCONFIG1 ) &
                           RF_PACKETCONFIG1_CRC_MASK &
                           RF_PACKETCONFIG1_PACKETFORMAT_MASK ) |
                           ( ( fixLen == 1 ) ? RF_PACKETCONFIG1_PACKETFORMAT_FIXED : RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE ) |
                           ( crcOn << 4 ) );
            SX1276Write( REG_PACKETCONFIG2, ( SX1276Read( REG_PACKETCONFIG2 ) | RF_PACKETCONFIG2_DATAMODE_PACKET ) );
        }
        break;
    case MODEM_LORA:
        {
            if( bandwidth > 2 )
            {
                // Fatal error: When using LoRa modem only bandwidths 125, 250 and 500 kHz are supported
                while( 1 );
            }
            bandwidth += 7;
            SX1276.Settings.LoRa.Bandwidth = bandwidth;
            SX1276.Settings.LoRa.Datarate = datarate;
            SX1276.Settings.LoRa.Coderate = coderate;
            SX1276.Settings.LoRa.PreambleLen = preambleLen;
            SX1276.Settings.LoRa.FixLen = fixLen;
            SX1276.Settings.LoRa.PayloadLen = payloadLen;
            SX1276.Settings.LoRa.CrcOn = crcOn;
            SX1276.Settings.LoRa.FreqHopOn = freqHopOn;
            SX1276.Settings.LoRa.HopPeriod = hopPeriod;
            SX1276.Settings.LoRa.IqInverted = iqInverted;
            SX1276.Settings.LoRa.RxContinuous = rxContinuous;

            if( datarate > 12 )
            {
                datarate = 12;
            }
            else if( datarate < 6 )
            {
                datarate = 6;
            }

            if( ( ( bandwidth == 7 ) && ( ( datarate == 11 ) || ( datarate == 12 ) ) ) ||
                ( ( bandwidth == 8 ) && ( datarate == 12 ) ) )
            {
                SX1276.Settings.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX1276.Settings.LoRa.LowDatarateOptimize = 0x00;
            }

            SX1276Write( REG_LR_MODEMCONFIG1,
                         ( SX1276Read( REG_LR_MODEMCONFIG1 ) &
                           RFLR_MODEMCONFIG1_BW_MASK &
                           RFLR_MODEMCONFIG1_CODINGRATE_MASK &
                           RFLR_MODEMCONFIG1_IMPLICITHEADER_MASK ) |
                           ( bandwidth << 4 ) | ( coderate << 1 ) |
                           fixLen );

            SX1276Write( REG_LR_MODEMCONFIG2,
                         ( SX1276Read( REG_LR_MODEMCONFIG2 ) &
                           RFLR_MODEMCONFIG2_SF_MASK &
                           RFLR_MODEMCONFIG2_RXPAYLOADCRC_MASK &
                           RFLR_MODEMCONFIG2_SYMBTIMEOUTMSB_MASK ) |
                           ( datarate << 4 ) | ( crcOn << 2 ) |
                           ( ( symbTimeout >> 8 ) & ~RFLR_MODEMCONFIG2_SYMBTIMEOUTMSB_MASK ) );

            SX1276Write( REG_LR_MODEMCONFIG3,
                         ( SX1276Read( REG_LR_MODEMCONFIG3 ) &
                           RFLR_MODEMCONFIG3_LOWDATARATEOPTIMIZE_MASK ) |
                           ( SX1276.Settings.LoRa.LowDatarateOptimize << 3 ) );

            SX1276Write( REG_LR_SYMBTIMEOUTLSB, ( uint8_t )( symbTimeout & 0xFF ) );
            SX1276Write( REG_LR_PREAMBLEMSB, ( uint8_t )( ( preambleLen >> 8 ) & 0xFF ) );
            SX1276Write( REG_LR_PREAMBLELSB, ( uint8_t )( preambleLen & 0xFF ) );

            if( fixLen == 1 )
            {
                SX1276Write( REG_LR_PAYLOADLENGTH, payloadLen );
            }

            if( SX1276.Settings.LoRa.FreqHopOn == true )
            {
                SX1276Write( REG_LR_PLLHOP, ( SX1276Read( REG_LR_PLLHOP ) & RFLR_PLLHOP_FASTHOP_MASK ) | RFLR_PLLHOP_FASTHOP_ON );
                SX1276Write( REG_LR_HOPPERIOD, SX1276.Settings.LoRa.HopPeriod );
            }

            if( ( bandwidth == 9 ) && ( SX1276.Settings.Channel > RF_MID_BAND_THRESH ) )
            {
                // ERRATA 2.1 - Sensitivity Optimization with a 500 kHz Bandwidth
                SX1276Write( REG_LR_TEST36, 0x02 );
                SX1276Write( REG_LR_TEST3A, 0x64 );
            }
            else if( bandwidth == 9 )
            {
                // ERRATA 2.1 - Sensitivity Optimization with a 500 kHz Bandwidth
                SX1276Write( REG_LR_TEST36, 0x02 );
                SX1276Write( REG_LR_TEST3A, 0x7F );
            }
            else
            {
                // ERRATA 2.1 - Sensitivity Optimization with a 500 kHz Bandwidth
                SX1276Write( REG_LR_TEST36, 0x03 );
            }

            if( datarate == 6 )
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE,
                             ( SX1276Read( REG_LR_DETECTOPTIMIZE ) &
                               RFLR_DETECTIONOPTIMIZE_MASK ) |
                               RFLR_DETECTIONOPTIMIZE_SF6 );
                SX1276Write( REG_LR_DETECTIONTHRESHOLD,
                             RFLR_DETECTIONTHRESH_SF6 );
            }
            else
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE,
                             ( SX1276Read( REG_LR_DETECTOPTIMIZE ) &
                             RFLR_DETECTIONOPTIMIZE_MASK ) |
                             RFLR_DETECTIONOPTIMIZE_SF7_TO_SF12 );
                SX1276Write( REG_LR_DETECTIONTHRESHOLD,
                             RFLR_DETECTIONTHRESH_SF7_TO_SF12 );
            }
        }
        break;
    }
}

void SX1276SetTxConfig( RadioModems_t modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool freqHopOn,
                        uint8_t hopPeriod, bool iqInverted, uint32_t timeout )
{
    SX1276SetModem( modem );

    SX1276SetRfTxPower( power );

    switch( modem )
    {
    case MODEM_FSK:
        {
            SX1276.Settings.Fsk.Power = power;
            SX1276.Settings.Fsk.Fdev = fdev;
            SX1276.Settings.Fsk.Bandwidth = bandwidth;
            SX1276.Settings.Fsk.Datarate = datarate;
            SX1276.Settings.Fsk.PreambleLen = preambleLen;
            SX1276.Settings.Fsk.FixLen = fixLen;
            SX1276.Settings.Fsk.CrcOn = crcOn;
            SX1276.Settings.Fsk.IqInverted = iqInverted;
            SX1276.Settings.Fsk.TxTimeout = timeout;

            fdev = ( uint16_t )( ( double )fdev / ( double )FREQ_STEP );
            SX1276Write( REG_FDEVMSB, ( uint8_t )( fdev >> 8 ) );
            SX1276Write( REG_FDEVLSB, ( uint8_t )( fdev & 0xFF ) );

            datarate = ( uint16_t )( ( double )XTAL_FREQ / ( double )datarate );
            SX1276Write( REG_BITRATEMSB, ( uint8_t )( datarate >> 8 ) );
            SX1276Write( REG_BITRATELSB, ( uint8_t )( datarate & 0xFF ) );

            SX1276Write( REG_PREAMBLEMSB, ( preambleLen >> 8 ) & 0x00FF );
            SX1276Write( REG_PREAMBLELSB, preambleLen & 0xFF );

            SX1276Write( REG_PACKETCONFIG1,
                         ( SX1276Read( REG_PACKETCONFIG1 ) &
                           RF_PACKETCONFIG1_CRC_MASK &
                           RF_PACKETCONFIG1_PACKETFORMAT_MASK ) |
                           ( ( fixLen == 1 ) ? RF_PACKETCONFIG1_PACKETFORMAT_FIXED : RF_PACKETCONFIG1_PACKETFORMAT_VARIABLE ) |
                           ( crcOn << 4 ) );
            SX1276Write( REG_PACKETCONFIG2, ( SX1276Read( REG_PACKETCONFIG2 ) | RF_PACKETCONFIG2_DATAMODE_PACKET ) );
        }
        break;
    case MODEM_LORA:
        {
            SX1276.Settings.LoRa.Power = power;
            if( bandwidth > 2 )
            {
                // Fatal error: When using LoRa modem only bandwidths 125, 250 and 500 kHz are supported
                while( 1 );
            }
            bandwidth += 7;
            SX1276.Settings.LoRa.Bandwidth = bandwidth;
            SX1276.Settings.LoRa.Datarate = datarate;
            SX1276.Settings.LoRa.Coderate = coderate;
            SX1276.Settings.LoRa.PreambleLen = preambleLen;
            SX1276.Settings.LoRa.FixLen = fixLen;
            SX1276.Settings.LoRa.FreqHopOn = freqHopOn;
            SX1276.Settings.LoRa.HopPeriod = hopPeriod;
            SX1276.Settings.LoRa.CrcOn = crcOn;
            SX1276.Settings.LoRa.IqInverted = iqInverted;
            SX1276.Settings.LoRa.TxTimeout = timeout;

            if( datarate > 12 )
            {
                datarate = 12;
            }
            else if( datarate < 6 )
            {
                datarate = 6;
            }
            if( ( ( bandwidth == 7 ) && ( ( datarate == 11 ) || ( datarate == 12 ) ) ) ||
                ( ( bandwidth == 8 ) && ( datarate == 12 ) ) )
            {
                SX1276.Settings.LoRa.LowDatarateOptimize = 0x01;
            }
            else
            {
                SX1276.Settings.LoRa.LowDatarateOptimize = 0x00;
            }

            if( SX1276.Settings.LoRa.FreqHopOn == true )
            {
                SX1276Write( REG_LR_PLLHOP, ( SX1276Read( REG_LR_PLLHOP ) & RFLR_PLLHOP_FASTHOP_MASK ) | RFLR_PLLHOP_FASTHOP_ON );
                SX1276Write( REG_LR_HOPPERIOD, SX1276.Settings.LoRa.HopPeriod );
            }

            SX1276Write( REG_LR_MODEMCONFIG1,
                         ( SX1276Read( REG_LR_MODEMCONFIG1 ) &
                           RFLR_MODEMCONFIG1_BW_MASK &
                           RFLR_MODEMCONFIG1_CODINGRATE_MASK &
                           RFLR_MODEMCONFIG1_IMPLICITHEADER_MASK ) |
                           ( bandwidth << 4 ) | ( coderate << 1 ) |
                           fixLen );

            SX1276Write( REG_LR_MODEMCONFIG2,
                         ( SX1276Read( REG_LR_MODEMCONFIG2 ) &
                           RFLR_MODEMCONFIG2_SF_MASK &
                           RFLR_MODEMCONFIG2_RXPAYLOADCRC_MASK ) |
                           ( datarate << 4 ) | ( crcOn << 2 ) );

            SX1276Write( REG_LR_MODEMCONFIG3,
                         ( SX1276Read( REG_LR_MODEMCONFIG3 ) &
                           RFLR_MODEMCONFIG3_LOWDATARATEOPTIMIZE_MASK ) |
                           ( SX1276.Settings.LoRa.LowDatarateOptimize << 3 ) );

            SX1276Write( REG_LR_PREAMBLEMSB, ( preambleLen >> 8 ) & 0x00FF );
            SX1276Write( REG_LR_PREAMBLELSB, preambleLen & 0xFF );

            if( datarate == 6 )
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE,
                             ( SX1276Read( REG_LR_DETECTOPTIMIZE ) &
                               RFLR_DETECTIONOPTIMIZE_MASK ) |
                               RFLR_DETECTIONOPTIMIZE_SF6 );
                SX1276Write( REG_LR_DETECTIONTHRESHOLD,
                             RFLR_DETECTIONTHRESH_SF6 );
            }
            else
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE,
                             ( SX1276Read( REG_LR_DETECTOPTIMIZE ) &
                             RFLR_DETECTIONOPTIMIZE_MASK ) |
                             RFLR_DETECTIONOPTIMIZE_SF7_TO_SF12 );
                SX1276Write( REG_LR_DETECTIONTHRESHOLD,
                             RFLR_DETECTIONTHRESH_SF7_TO_SF12 );
            }
        }
        break;
    }
}

uint32_t SX1276GetTimeOnAir( RadioModems_t modem, uint8_t pktLen )
{
    uint32_t airTime = 0;

    switch( modem )
    {
    case MODEM_FSK:
        {
            airTime = round( ( 8 * ( SX1276.Settings.Fsk.PreambleLen +
                                     ( ( SX1276Read( REG_SYNCCONFIG ) & ~RF_SYNCCONFIG_SYNCSIZE_MASK ) + 1 ) +
                                     ( ( SX1276.Settings.Fsk.FixLen == 0x01 ) ? 0.0 : 1.0 ) +
                                     ( ( ( SX1276Read( REG_PACKETCONFIG1 ) & ~RF_PACKETCONFIG1_ADDRSFILTERING_MASK ) != 0x00 ) ? 1.0 : 0 ) +
                                     pktLen +
                                     ( ( SX1276.Settings.Fsk.CrcOn == 0x01 ) ? 2.0 : 0 ) ) /
                                     SX1276.Settings.Fsk.Datarate ) * 1000 );
        }
        break;
    case MODEM_LORA:
        {
            double bw = 0.0;
            // REMARK: When using LoRa modem only bandwidths 125, 250 and 500 kHz are supported
            switch( SX1276.Settings.LoRa.Bandwidth )
            {
            //case 0: // 7.8 kHz
            //    bw = 7800;
            //    break;
            //case 1: // 10.4 kHz
            //    bw = 10400;
            //    break;
            //case 2: // 15.6 kHz
            //    bw = 15600;
            //    break;
            //case 3: // 20.8 kHz
            //    bw = 20800;
            //    break;
            //case 4: // 31.2 kHz
            //    bw = 31200;
            //    break;
            //case 5: // 41.4 kHz
            //    bw = 41400;
            //    break;
            //case 6: // 62.5 kHz
            //    bw = 62500;
            //    break;
            case 7: // 125 kHz
                bw = 125000;
                break;
            case 8: // 250 kHz
                bw = 250000;
                break;
            case 9: // 500 kHz
                bw = 500000;
                break;
            }

            // Symbol rate : time for one symbol (secs)
            double rs = bw / ( 1 << SX1276.Settings.LoRa.Datarate );
            double ts = 1 / rs;
            // time of preamble
            double tPreamble = ( SX1276.Settings.LoRa.PreambleLen + 4.25 ) * ts;
            // Symbol length of payload and time
            double tmp = ceil( ( 8 * pktLen - 4 * SX1276.Settings.LoRa.Datarate +
                                 28 + 16 * SX1276.Settings.LoRa.CrcOn -
                                 ( SX1276.Settings.LoRa.FixLen ? 20 : 0 ) ) /
                                 ( double )( 4 * ( SX1276.Settings.LoRa.Datarate -
                                 ( ( SX1276.Settings.LoRa.LowDatarateOptimize > 0 ) ? 2 : 0 ) ) ) ) *
                                 ( SX1276.Settings.LoRa.Coderate + 4 );
            double nPayload = 8 + ( ( tmp > 0 ) ? tmp : 0 );
            double tPayload = nPayload * ts;
            // Time on air
            double tOnAir = tPreamble + tPayload;
            // return ms secs
            airTime = floor( tOnAir * 1000 + 0.999 );
        }
        break;
    }
    return airTime;
}


void SX1276Send( uint8_t *buffer, uint8_t size )
{
    uint32_t txTimeout = 0;

    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        {
            SX1276.Settings.FskPacketHandler.NbBytes = 0;
            SX1276.Settings.FskPacketHandler.Size = size;

            if( SX1276.Settings.Fsk.FixLen == false )
            {
                SX1276WriteFifo( ( uint8_t* )&size, 1 );
            }
            else
            {
                SX1276Write( REG_PAYLOADLENGTH, size );
            }

            if( ( size > 0 ) && ( size <= 64 ) )
            {
                SX1276.Settings.FskPacketHandler.ChunkSize = size;
            }
            else
            {
                memcpy1( RxTxBuffer, buffer, size );
                SX1276.Settings.FskPacketHandler.ChunkSize = 32;
            }

            // Write payload buffer
            SX1276WriteFifo( buffer, SX1276.Settings.FskPacketHandler.ChunkSize );
            SX1276.Settings.FskPacketHandler.NbBytes += SX1276.Settings.FskPacketHandler.ChunkSize;
            txTimeout = SX1276.Settings.Fsk.TxTimeout;
        }
        break;
    case MODEM_LORA:
        {
            if( SX1276.Settings.LoRa.IqInverted == true )
            {
                SX1276Write( REG_LR_INVERTIQ, ( ( SX1276Read( REG_LR_INVERTIQ ) & RFLR_INVERTIQ_TX_MASK & RFLR_INVERTIQ_RX_MASK ) | RFLR_INVERTIQ_RX_OFF | RFLR_INVERTIQ_TX_ON ) );
                SX1276Write( REG_LR_INVERTIQ2, RFLR_INVERTIQ2_ON );
            }
            else
            {
                SX1276Write( REG_LR_INVERTIQ, ( ( SX1276Read( REG_LR_INVERTIQ ) & RFLR_INVERTIQ_TX_MASK & RFLR_INVERTIQ_RX_MASK ) | RFLR_INVERTIQ_RX_OFF | RFLR_INVERTIQ_TX_OFF ) );
                SX1276Write( REG_LR_INVERTIQ2, RFLR_INVERTIQ2_OFF );
            }
            
            /*---------------------------------------------------------------------
            
            @Description : 当使用跳频中断时，将记录的时间戳随消息一起发送，因此将PAYLOADLENGTH在原来的基础上加10，正好是Time的长度。
            @author : william
            
            ---------------------------------------------------------------------*/

            if(SX1276.Settings.LoRa.FreqHopOn == true){
                SX1276.Settings.LoRaPacketHandler.Size = size + 10;
                SX1276Write( REG_LR_PAYLOADLENGTH, size + 10);
            }else{
                SX1276.Settings.LoRaPacketHandler.Size = size;
                SX1276Write( REG_LR_PAYLOADLENGTH, size);
            }

            // Full buffer used for Tx
            SX1276Write( REG_LR_FIFOTXBASEADDR, 0 );
            SX1276Write( REG_LR_FIFOADDRPTR, 0 );

            // FIFO operations can not take place in Sleep mode
            if( ( SX1276Read( REG_OPMODE ) & ~RF_OPMODE_MASK ) == RF_OPMODE_SLEEP )
            {
                SX1276SetStby( );
                DelayMs( 1 );
            }
            // Write payload buffer
            SX1276WriteFifo( buffer, size );
            txTimeout = SX1276.Settings.LoRa.TxTimeout;
        }
        break;
    }

    SX1276SetTx( txTimeout );
}

void SX1276SetSleep( void )
{
    TimerStop( &RxTimeoutTimer );
    TimerStop( &TxTimeoutTimer );

    SX1276SetOpMode( RF_OPMODE_SLEEP );
    SX1276.Settings.State = RF_IDLE;
}

void SX1276SetStby( void )
{
    TimerStop( &RxTimeoutTimer );
    TimerStop( &TxTimeoutTimer );

    SX1276SetOpMode( RF_OPMODE_STANDBY );
    SX1276.Settings.State = RF_IDLE;
}

void SX1276SetRx( uint32_t timeout )
{
    bool rxContinuous = false;

    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        {
            rxContinuous = SX1276.Settings.Fsk.RxContinuous;

            // DIO0=PayloadReady
            // DIO1=FifoLevel
            // DIO2=SyncAddr
            // DIO3=FifoEmpty
            // DIO4=Preamble
            // DIO5=ModeReady
            SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RF_DIOMAPPING1_DIO0_MASK &
                                                                            RF_DIOMAPPING1_DIO1_MASK &
                                                                            RF_DIOMAPPING1_DIO2_MASK ) |
                                                                            RF_DIOMAPPING1_DIO0_00 |
                                                                            RF_DIOMAPPING1_DIO1_00 |
                                                                            RF_DIOMAPPING1_DIO2_11 );

            SX1276Write( REG_DIOMAPPING2, ( SX1276Read( REG_DIOMAPPING2 ) & RF_DIOMAPPING2_DIO4_MASK &
                                                                            RF_DIOMAPPING2_MAP_MASK ) |
                                                                            RF_DIOMAPPING2_DIO4_11 |
                                                                            RF_DIOMAPPING2_MAP_PREAMBLEDETECT );

            SX1276.Settings.FskPacketHandler.FifoThresh = SX1276Read( REG_FIFOTHRESH ) & 0x3F;

            SX1276Write( REG_RXCONFIG, RF_RXCONFIG_AFCAUTO_ON | RF_RXCONFIG_AGCAUTO_ON | RF_RXCONFIG_RXTRIGER_PREAMBLEDETECT );

            SX1276.Settings.FskPacketHandler.PreambleDetected = false;
            SX1276.Settings.FskPacketHandler.SyncWordDetected = false;
            SX1276.Settings.FskPacketHandler.NbBytes = 0;
            SX1276.Settings.FskPacketHandler.Size = 0;
        }
        break;
    case MODEM_LORA:
        {
            if( SX1276.Settings.LoRa.IqInverted == true )
            {
                SX1276Write( REG_LR_INVERTIQ, ( ( SX1276Read( REG_LR_INVERTIQ ) & RFLR_INVERTIQ_TX_MASK & RFLR_INVERTIQ_RX_MASK ) | RFLR_INVERTIQ_RX_ON | RFLR_INVERTIQ_TX_OFF ) );
                SX1276Write( REG_LR_INVERTIQ2, RFLR_INVERTIQ2_ON );
            }
            else
            {
                SX1276Write( REG_LR_INVERTIQ, ( ( SX1276Read( REG_LR_INVERTIQ ) & RFLR_INVERTIQ_TX_MASK & RFLR_INVERTIQ_RX_MASK ) | RFLR_INVERTIQ_RX_OFF | RFLR_INVERTIQ_TX_OFF ) );
                SX1276Write( REG_LR_INVERTIQ2, RFLR_INVERTIQ2_OFF );
            }

            // ERRATA 2.3 - Receiver Spurious Reception of a LoRa Signal
            if( SX1276.Settings.LoRa.Bandwidth < 9 )
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE, SX1276Read( REG_LR_DETECTOPTIMIZE ) & 0x7F );
                SX1276Write( REG_LR_TEST30, 0x00 );
                switch( SX1276.Settings.LoRa.Bandwidth )
                {
                case 0: // 7.8 kHz
                    SX1276Write( REG_LR_TEST2F, 0x48 );
                    SX1276SetChannel(SX1276.Settings.Channel + 7810 );
                    break;
                case 1: // 10.4 kHz
                    SX1276Write( REG_LR_TEST2F, 0x44 );
                    SX1276SetChannel(SX1276.Settings.Channel + 10420 );
                    break;
                case 2: // 15.6 kHz
                    SX1276Write( REG_LR_TEST2F, 0x44 );
                    SX1276SetChannel(SX1276.Settings.Channel + 15620 );
                    break;
                case 3: // 20.8 kHz
                    SX1276Write( REG_LR_TEST2F, 0x44 );
                    SX1276SetChannel(SX1276.Settings.Channel + 20830 );
                    break;
                case 4: // 31.2 kHz
                    SX1276Write( REG_LR_TEST2F, 0x44 );
                    SX1276SetChannel(SX1276.Settings.Channel + 31250 );
                    break;
                case 5: // 41.4 kHz
                    SX1276Write( REG_LR_TEST2F, 0x44 );
                    SX1276SetChannel(SX1276.Settings.Channel + 41670 );
                    break;
                case 6: // 62.5 kHz
                    SX1276Write( REG_LR_TEST2F, 0x40 );
                    break;
                case 7: // 125 kHz
                    SX1276Write( REG_LR_TEST2F, 0x40 );
                    break;
                case 8: // 250 kHz
                    SX1276Write( REG_LR_TEST2F, 0x40 );
                    break;
                }
            }
            else
            {
                SX1276Write( REG_LR_DETECTOPTIMIZE, SX1276Read( REG_LR_DETECTOPTIMIZE ) | 0x80 );
            }

            rxContinuous = SX1276.Settings.LoRa.RxContinuous;

            if( SX1276.Settings.LoRa.FreqHopOn == true )
            {
                SX1276Write( REG_LR_IRQFLAGSMASK, //RFLR_IRQFLAGS_RXTIMEOUT |
                                                  //RFLR_IRQFLAGS_RXDONE |
                                                  //RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                                  RFLR_IRQFLAGS_VALIDHEADER |
                                                  RFLR_IRQFLAGS_TXDONE |
                                                  RFLR_IRQFLAGS_CADDONE |
                                                  //RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                                  RFLR_IRQFLAGS_CADDETECTED );

                // DIO0=RxDone, DIO2=FhssChangeChannel
                SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK & RFLR_DIOMAPPING1_DIO2_MASK  ) | RFLR_DIOMAPPING1_DIO0_00 | RFLR_DIOMAPPING1_DIO2_00 );
            }
            else
            {
                SX1276Write( REG_LR_IRQFLAGSMASK, //RFLR_IRQFLAGS_RXTIMEOUT |
                                                  //RFLR_IRQFLAGS_RXDONE |
                                                  //RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                                  RFLR_IRQFLAGS_VALIDHEADER |
                                                  RFLR_IRQFLAGS_TXDONE |
                                                  RFLR_IRQFLAGS_CADDONE |
                                                  RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                                  RFLR_IRQFLAGS_CADDETECTED );

                // DIO0=RxDone
                SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK ) | RFLR_DIOMAPPING1_DIO0_00 );
            }
            SX1276Write( REG_LR_FIFORXBASEADDR, 0 );
            SX1276Write( REG_LR_FIFOADDRPTR, 0 );
        }
        break;
    }

    memset( RxTxBuffer, 0, ( size_t )RX_BUFFER_SIZE );

    SX1276.Settings.State = RF_RX_RUNNING;
    if( timeout != 0 )
    {
        TimerSetValue( &RxTimeoutTimer, timeout );
        TimerStart( &RxTimeoutTimer );
    }

    if( SX1276.Settings.Modem == MODEM_FSK )
    {
        SX1276SetOpMode( RF_OPMODE_RECEIVER );

        if( rxContinuous == false )
        {
            TimerSetValue( &RxTimeoutSyncWord, SX1276.Settings.Fsk.RxSingleTimeout );
            TimerStart( &RxTimeoutSyncWord );
        }
    }
    else
    {
        if( rxContinuous == true )
        {
            SX1276SetOpMode( RFLR_OPMODE_RECEIVER );
        }
        else
        {
            SX1276SetOpMode( RFLR_OPMODE_RECEIVER_SINGLE );
        }
    }
}

void SX1276SetTx( uint32_t timeout )
{
    TimerSetValue( &TxTimeoutTimer, timeout );

    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        {
            // DIO0=PacketSent
            // DIO1=FifoEmpty
            // DIO2=FifoFull
            // DIO3=FifoEmpty
            // DIO4=LowBat
            // DIO5=ModeReady
            SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RF_DIOMAPPING1_DIO0_MASK &
                                                                            RF_DIOMAPPING1_DIO1_MASK &
                                                                            RF_DIOMAPPING1_DIO2_MASK ) |
                                                                            RF_DIOMAPPING1_DIO1_01 );

            SX1276Write( REG_DIOMAPPING2, ( SX1276Read( REG_DIOMAPPING2 ) & RF_DIOMAPPING2_DIO4_MASK &
                                                                            RF_DIOMAPPING2_MAP_MASK ) );
            SX1276.Settings.FskPacketHandler.FifoThresh = SX1276Read( REG_FIFOTHRESH ) & 0x3F;
        }
        break;
    case MODEM_LORA:
        {
            if( SX1276.Settings.LoRa.FreqHopOn == true )
            {
                SX1276Write( REG_LR_IRQFLAGSMASK, RFLR_IRQFLAGS_RXTIMEOUT |
                                                  RFLR_IRQFLAGS_RXDONE |
                                                  RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                                  RFLR_IRQFLAGS_VALIDHEADER |
                                                  //RFLR_IRQFLAGS_TXDONE |
                                                  RFLR_IRQFLAGS_CADDONE |
                                                  //RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                                  RFLR_IRQFLAGS_CADDETECTED );

                // DIO0=TxDone, DIO2=FhssChangeChannel
                SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK & RFLR_DIOMAPPING1_DIO2_MASK ) | RFLR_DIOMAPPING1_DIO0_01 | RFLR_DIOMAPPING1_DIO2_00 );
            }
            else
            {
                SX1276Write( REG_LR_IRQFLAGSMASK, RFLR_IRQFLAGS_RXTIMEOUT |
                                                  RFLR_IRQFLAGS_RXDONE |
                                                  RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                                  RFLR_IRQFLAGS_VALIDHEADER |
                                                  //RFLR_IRQFLAGS_TXDONE |
                                                  RFLR_IRQFLAGS_CADDONE |
                                                  RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL |
                                                  RFLR_IRQFLAGS_CADDETECTED );

                // DIO0=TxDone
                SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO0_MASK ) | RFLR_DIOMAPPING1_DIO0_01 );
            }
        }
        break;
    }
    //wait
    /**
    t1 = micros();
    ets_delay_us(1000000-(t1-sysTime));
    t2 = micros();
    lora_printf("delay:%lu\n",t2-t1);
    lora_printf("StartSendTime:%lu\n",t2);
    */

    SX1276.Settings.State = RF_TX_RUNNING;
    TimerStart( &TxTimeoutTimer );
    SX1276SetOpMode( RF_OPMODE_TRANSMITTER );
}

void SX1276StartCad( void )
{
    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        {

        }
        break;
    case MODEM_LORA:
        {
            SX1276Write( REG_LR_IRQFLAGSMASK, RFLR_IRQFLAGS_RXTIMEOUT |
                                        RFLR_IRQFLAGS_RXDONE |
                                        RFLR_IRQFLAGS_PAYLOADCRCERROR |
                                        RFLR_IRQFLAGS_VALIDHEADER |
                                        RFLR_IRQFLAGS_TXDONE |
                                        //RFLR_IRQFLAGS_CADDONE |
                                        RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL // |
                                        //RFLR_IRQFLAGS_CADDETECTED
                                        );

            // DIO3=CADDone
            SX1276Write( REG_DIOMAPPING1, ( SX1276Read( REG_DIOMAPPING1 ) & RFLR_DIOMAPPING1_DIO3_MASK ) | RFLR_DIOMAPPING1_DIO3_00 );

            SX1276.Settings.State = RF_CAD;
            SX1276SetOpMode( RFLR_OPMODE_CAD );
        }
        break;
    default:
        break;
    }
}

void SX1276SetTxContinuousWave( uint32_t freq, int8_t power, uint16_t time )
{
    uint32_t timeout = ( uint32_t )( time * 1000 );

    SX1276SetChannel( freq );

    SX1276SetTxConfig( MODEM_FSK, power, 0, 0, 4800, 0, 5, false, false, 0, 0, 0, timeout );

    SX1276Write( REG_PACKETCONFIG2, ( SX1276Read( REG_PACKETCONFIG2 ) & RF_PACKETCONFIG2_DATAMODE_MASK ) );
    // Disable radio interrupts
    SX1276Write( REG_DIOMAPPING1, RF_DIOMAPPING1_DIO0_11 | RF_DIOMAPPING1_DIO1_11 );
    SX1276Write( REG_DIOMAPPING2, RF_DIOMAPPING2_DIO4_10 | RF_DIOMAPPING2_DIO5_10 );

    TimerSetValue( &TxTimeoutTimer, timeout );

    SX1276.Settings.State = RF_TX_RUNNING;
    TimerStart( &TxTimeoutTimer );
    SX1276SetOpMode( RF_OPMODE_TRANSMITTER );
}

int16_t SX1276ReadRssi( RadioModems_t modem )
{
    int16_t rssi = 0;

    switch( modem )
    {
    case MODEM_FSK:
        rssi = -( SX1276Read( REG_RSSIVALUE ) >> 1 );
        break;
    case MODEM_LORA:
        if( SX1276.Settings.Channel > RF_MID_BAND_THRESH )
        {
            rssi = RSSI_OFFSET_HF + SX1276Read( REG_LR_RSSIVALUE );
        }
        else
        {
            rssi = RSSI_OFFSET_LF + SX1276Read( REG_LR_RSSIVALUE );
        }
        break;
    default:
        rssi = -1;
        break;
    }
    return rssi;
}

void SX1276SetOpMode( uint8_t opMode )
{
#if defined( USE_RADIO_DEBUG )
    switch( opMode )
    {
        case RF_OPMODE_TRANSMITTER:
            GpioWrite( &DbgPinTx, 1 );
            break;
        case RF_OPMODE_RECEIVER:
        case RFLR_OPMODE_RECEIVER_SINGLE:
            GpioWrite( &DbgPinRx, 1 );
            break;
        default:
            GpioWrite( &DbgPinTx, 0 );
            GpioWrite( &DbgPinRx, 0 );
            break;
    }
#endif
    if( opMode == RF_OPMODE_SLEEP )
    {
        SX1276SetAntSwLowPower( true );
    }
    else
    {
        SX1276SetAntSwLowPower( false );
        SX1276SetAntSw( opMode );
    }
	/*
	if(opMode==RF_OPMODE_RECEIVER||opMode==RFLR_OPMODE_RECEIVER_SINGLE)
	{
		lora_printf("Rx\r\n");
	}
	if(opMode==RF_OPMODE_TRANSMITTER)
	{
		lora_printf("Tx\r\n");
	}
	*/
    SX1276Write( REG_OPMODE, ( SX1276Read( REG_OPMODE ) & RF_OPMODE_MASK ) | opMode );
}

void SX1276SetModem( RadioModems_t modem )
{
    if( ( SX1276Read( REG_OPMODE ) & RFLR_OPMODE_LONGRANGEMODE_ON ) != 0 )
    {
        SX1276.Settings.Modem = MODEM_LORA;
    }
    else
    {
        SX1276.Settings.Modem = MODEM_FSK;
    }

    if( SX1276.Settings.Modem == modem )
    {
        return;
    }

    SX1276.Settings.Modem = modem;
    switch( SX1276.Settings.Modem )
    {
    default:
    case MODEM_FSK:
        SX1276SetSleep( );
        SX1276Write( REG_OPMODE, ( SX1276Read( REG_OPMODE ) & RFLR_OPMODE_LONGRANGEMODE_MASK ) | RFLR_OPMODE_LONGRANGEMODE_OFF );

        SX1276Write( REG_DIOMAPPING1, 0x00 );
        SX1276Write( REG_DIOMAPPING2, 0x30 ); // DIO5=ModeReady
        break;
    case MODEM_LORA:
        SX1276SetSleep( );
        SX1276Write( REG_OPMODE, ( SX1276Read( REG_OPMODE ) & RFLR_OPMODE_LONGRANGEMODE_MASK ) | RFLR_OPMODE_LONGRANGEMODE_ON );

        SX1276Write( REG_DIOMAPPING1, 0x00 );
        SX1276Write( REG_DIOMAPPING2, 0x00 );
        break;
    }
}

extern void write0(uint16_t address, uint8_t value);
extern uint8_t read0(uint16_t address);
extern void writefifo(uint16_t address, uint8_t *buffer, uint8_t size);
extern void readfifo(uint16_t address, uint8_t *buffer, uint8_t size);

void SX1276Write( uint16_t addr, uint8_t data )
{
    GpioWrite( &SX1276.Spi.Nss, 0 );

    SpiInOut( &SX1276.Spi, addr | 0x80 );
    SpiInOut( &SX1276.Spi, data );

    GpioWrite( &SX1276.Spi.Nss, 1 );
}

uint8_t SX1276Read( uint16_t addr )
{

    GpioWrite( &SX1276.Spi.Nss, 0 );

    SpiInOut( &SX1276.Spi, addr & 0x7f);
    uint8_t res=SpiInOut( &SX1276.Spi, 0 );

    GpioWrite( &SX1276.Spi.Nss, 1 );
    return res;
}

void SX1276WriteBuffer( uint16_t addr, uint8_t *buffer, uint8_t size )
{

}

void SX1276ReadBuffer( uint16_t addr, uint8_t *buffer, uint8_t size )
{

}

void SX1276WriteFifo( uint8_t *buffer, uint8_t size )
{
	GpioWrite( &SX1276.Spi.Nss, 0 );
	SpiInOut( &SX1276.Spi, 0x80 );
	for( uint16_t i = 0; i < size; i++ )
	{
		SpiInOut( &SX1276.Spi, buffer[i] );
	}
	GpioWrite( &SX1276.Spi.Nss, 1 );
	
}

void SX1276ReadFifo( uint8_t *buffer, uint8_t size )
{
	GpioWrite( &SX1276.Spi.Nss, 0 );
	SpiInOut( &SX1276.Spi, 0x00);
	for( uint16_t i = 0; i < size; i++ )
	{
		buffer[i] = SpiInOut( &SX1276.Spi, 0 );
	}
	GpioWrite( &SX1276.Spi.Nss, 1 );
	
}

void SX1276SetMaxPayloadLength( RadioModems_t modem, uint8_t max )
{
    SX1276SetModem( modem );

    switch( modem )
    {
    case MODEM_FSK:
        if( SX1276.Settings.Fsk.FixLen == false )
        {
            SX1276Write( REG_PAYLOADLENGTH, max );
        }
        break;
    case MODEM_LORA:
        SX1276Write( REG_LR_PAYLOADMAXLENGTH, max );
        break;
    }
}

void SX1276SetPublicNetwork( bool enable )
{
    SX1276SetModem( MODEM_LORA );
    SX1276.Settings.LoRa.PublicNetwork = enable;
    if( enable == true )
    {
        // Change LoRa modem SyncWord
        SX1276Write( REG_LR_SYNCWORD, LORA_MAC_PUBLIC_SYNCWORD );
    }
    else
    {
        // Change LoRa modem SyncWord
        SX1276Write( REG_LR_SYNCWORD, LORA_MAC_PRIVATE_SYNCWORD );
    }
}

uint32_t SX1276GetWakeupTime( void )
{
    return SX1276GetBoardTcxoWakeupTime( ) + RADIO_WAKEUP_TIME;
}

void SX1276OnTimeoutIrq( void )
{
    switch( SX1276.Settings.State )
    {
    case RF_RX_RUNNING:
        if( SX1276.Settings.Modem == MODEM_FSK )
        {
            SX1276.Settings.FskPacketHandler.PreambleDetected = false;
            SX1276.Settings.FskPacketHandler.SyncWordDetected = false;
            SX1276.Settings.FskPacketHandler.NbBytes = 0;
            SX1276.Settings.FskPacketHandler.Size = 0;

            // Clear Irqs
            SX1276Write( REG_IRQFLAGS1, RF_IRQFLAGS1_RSSI |
                                        RF_IRQFLAGS1_PREAMBLEDETECT |
                                        RF_IRQFLAGS1_SYNCADDRESSMATCH );
            SX1276Write( REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN );

            if( SX1276.Settings.Fsk.RxContinuous == true )
            {
                // Continuous mode restart Rx chain
                SX1276Write( REG_RXCONFIG, SX1276Read( REG_RXCONFIG ) | RF_RXCONFIG_RESTARTRXWITHOUTPLLLOCK );
                TimerStart( &RxTimeoutSyncWord );
            }
            else
            {
                SX1276.Settings.State = RF_IDLE;
                TimerStop( &RxTimeoutSyncWord );
            }
        }
        if( ( RadioEvents != NULL ) && ( RadioEvents->RxTimeout != NULL ) )
        {
            RadioEvents->RxTimeout( );
        }
        break;
    case RF_TX_RUNNING:
        // Tx timeout shouldn't happen.
        // But it has been observed that when it happens it is a result of a corrupted SPI transfer
        // it depends on the platform design.
        //
        // The workaround is to put the radio in a known state. Thus, we re-initialize it.

        // BEGIN WORKAROUND

        // Reset the radio
        SX1276Reset( );

        // Calibrate Rx chain
        RxChainCalibration( );

        // Initialize radio default values
        SX1276SetOpMode( RF_OPMODE_SLEEP );

        for( uint8_t i = 0; i < sizeof( RadioRegsInit ) / sizeof( RadioRegisters_t ); i++ )
        {
            SX1276SetModem( RadioRegsInit[i].Modem );
            SX1276Write( RadioRegsInit[i].Addr, RadioRegsInit[i].Value );
        }
        SX1276SetModem( MODEM_FSK );

        // Restore previous network type setting.
        SX1276SetPublicNetwork( SX1276.Settings.LoRa.PublicNetwork );
        // END WORKAROUND

        SX1276.Settings.State = RF_IDLE;
        if( ( RadioEvents != NULL ) && ( RadioEvents->TxTimeout != NULL ) )
        {
            RadioEvents->TxTimeout( );
        }
        break;
    default:
        break;
    }
}


/*--------------------------------------------------------

@Description : 当触发跳频发送中断时，会触发这个函数，用于将触发跳频中断时记录的时间戳写入FIFO中，随payload一起发送
@author : william

--------------------------------------------------------*/

void ChannelHoppingTx(){
    int base = SX1276Read( REG_LR_FIFOTXBASEADDR );
    int ptr = SX1276Read( REG_LR_FIFOADDRPTR );
    lora_printf("base:%d,ptr:%d\n",base,ptr);
    getTimeChar();
    lora_printf("TxHeaderTime:%s\n",Time);
    //SX1276.Settings.LoRaPacketHandler.Size = SX1276.Settings.LoRaPacketHandler.Size +10;
    SX1276WriteFifo(Time,10);
}


/*---------------------------------------------------------------

@Description : 触发中断函数，DIO0主要是RxDone和TxDone，主要改动就是触发时打印记录的时间戳。
@author : william

---------------------------------------------------------*/

void SX1276OnDio0Irq( void )
{
	//lora_printf("0\r\n");
    volatile uint8_t irqFlags = 0;
    switch( SX1276.Settings.State )
    {
        case RF_RX_RUNNING:
            //TimerStop( &RxTimeoutTimer );
            // RxDone interrupt
            switch( SX1276.Settings.Modem )
            {
            case MODEM_FSK:
                if( SX1276.Settings.Fsk.CrcOn == true )
                {
                    irqFlags = SX1276Read( REG_IRQFLAGS2 );
                    if( ( irqFlags & RF_IRQFLAGS2_CRCOK ) != RF_IRQFLAGS2_CRCOK )
                    {
                        // Clear Irqs
                        SX1276Write( REG_IRQFLAGS1, RF_IRQFLAGS1_RSSI |
                                                    RF_IRQFLAGS1_PREAMBLEDETECT |
                                                    RF_IRQFLAGS1_SYNCADDRESSMATCH );
                        SX1276Write( REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN );

                        TimerStop( &RxTimeoutTimer );

                        if( SX1276.Settings.Fsk.RxContinuous == false )
                        {
                            TimerStop( &RxTimeoutSyncWord );
                            SX1276.Settings.State = RF_IDLE;
                        }
                        else
                        {
                            // Continuous mode restart Rx chain
                            SX1276Write( REG_RXCONFIG, SX1276Read( REG_RXCONFIG ) | RF_RXCONFIG_RESTARTRXWITHOUTPLLLOCK );
                            TimerStart( &RxTimeoutSyncWord );
                        }

                        if( ( RadioEvents != NULL ) && ( RadioEvents->RxError != NULL ) )
                        {
                            RadioEvents->RxError( );
                        }
                        SX1276.Settings.FskPacketHandler.PreambleDetected = false;
                        SX1276.Settings.FskPacketHandler.SyncWordDetected = false;
                        SX1276.Settings.FskPacketHandler.NbBytes = 0;
                        SX1276.Settings.FskPacketHandler.Size = 0;
                        break;
                    }
                }

                // Read received packet size
                if( ( SX1276.Settings.FskPacketHandler.Size == 0 ) && ( SX1276.Settings.FskPacketHandler.NbBytes == 0 ) )
                {
                    if( SX1276.Settings.Fsk.FixLen == false )
                    {
                        SX1276ReadFifo( ( uint8_t* )&SX1276.Settings.FskPacketHandler.Size, 1 );
                    }
                    else
                    {
                        SX1276.Settings.FskPacketHandler.Size = SX1276Read( REG_PAYLOADLENGTH );
                    }
                    SX1276ReadFifo( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes, SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                    SX1276.Settings.FskPacketHandler.NbBytes += ( SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                }
                else
                {
                    SX1276ReadFifo( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes, SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                    SX1276.Settings.FskPacketHandler.NbBytes += ( SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                }

                TimerStop( &RxTimeoutTimer );

                if( SX1276.Settings.Fsk.RxContinuous == false )
                {
                    SX1276.Settings.State = RF_IDLE;
                    TimerStop( &RxTimeoutSyncWord );
                }
                else
                {
                    // Continuous mode restart Rx chain
                    SX1276Write( REG_RXCONFIG, SX1276Read( REG_RXCONFIG ) | RF_RXCONFIG_RESTARTRXWITHOUTPLLLOCK );
                    TimerStart( &RxTimeoutSyncWord );
                }

                if( ( RadioEvents != NULL ) && ( RadioEvents->RxDone != NULL ) )
                {
                    RadioEvents->RxDone( RxTxBuffer, SX1276.Settings.FskPacketHandler.Size, SX1276.Settings.FskPacketHandler.RssiValue, 0 );
                }
                SX1276.Settings.FskPacketHandler.PreambleDetected = false;
                SX1276.Settings.FskPacketHandler.SyncWordDetected = false;
                SX1276.Settings.FskPacketHandler.NbBytes = 0;
                SX1276.Settings.FskPacketHandler.Size = 0;
                break;
            case MODEM_LORA:
                {
                    int8_t snr = 0;

                    // Clear Irq
                    SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_RXDONE );

                    irqFlags = SX1276Read( REG_LR_IRQFLAGS );
                    if( ( irqFlags & RFLR_IRQFLAGS_PAYLOADCRCERROR_MASK ) == RFLR_IRQFLAGS_PAYLOADCRCERROR )
                    {
                        // Clear Irq
                        SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_PAYLOADCRCERROR );

                        if( SX1276.Settings.LoRa.RxContinuous == false )
                        {
                            SX1276.Settings.State = RF_IDLE;
                        }
                        TimerStop( &RxTimeoutTimer );

                        if( ( RadioEvents != NULL ) && ( RadioEvents->RxError != NULL ) )
                        {
                        	DIO_PRINTF("DIO0:RX Error\r\n");
                            RadioEvents->RxError( );
                        }
                        break;
                    }

                    SX1276.Settings.LoRaPacketHandler.SnrValue = ( ( ( int8_t )SX1276Read( REG_LR_PKTSNRVALUE ) ) + 2 ) >> 2;
                    /*
                    if( SX1276.Settings.LoRaPacketHandler.SnrValue & 0x80 ) // The SNR sign bit is 1
                    {
                        // Invert and divide by 4
                        snr = ( ( ~SX1276.Settings.LoRaPacketHandler.SnrValue + 1 ) & 0xFF ) >> 2;
                        snr = -snr;
                    }
                    else
                    {
                        // Divide by 4
                        snr = ( SX1276.Settings.LoRaPacketHandler.SnrValue & 0xFF ) >> 2;
                    }*/

                    int16_t rssi = SX1276Read( REG_LR_PKTRSSIVALUE );
                    if( snr < 0 )
                    {
                        if( SX1276.Settings.Channel > RF_MID_BAND_THRESH )
                        {
                            SX1276.Settings.LoRaPacketHandler.RssiValue = RSSI_OFFSET_HF + rssi + ( rssi >> 4 ) +
                                                                          snr;
                        }
                        else
                        {
                            SX1276.Settings.LoRaPacketHandler.RssiValue = RSSI_OFFSET_LF + rssi + ( rssi >> 4 ) +
                                                                          snr;
                        }
                    }
                    else
                    {
                        if( SX1276.Settings.Channel > RF_MID_BAND_THRESH )
                        {
                            SX1276.Settings.LoRaPacketHandler.RssiValue = RSSI_OFFSET_HF + rssi + ( rssi >> 4 );
                        }
                        else
                        {
                            SX1276.Settings.LoRaPacketHandler.RssiValue = RSSI_OFFSET_LF + rssi + ( rssi >> 4 );
                        }
                    }

                    SX1276.Settings.LoRaPacketHandler.Size = SX1276Read( REG_LR_RXNBBYTES );
                    SX1276Write( REG_LR_FIFOADDRPTR, SX1276Read( REG_LR_FIFORXCURRENTADDR ) );
                    SX1276ReadFifo( RxTxBuffer, SX1276.Settings.LoRaPacketHandler.Size);
                    if( SX1276.Settings.LoRa.RxContinuous == false )
                    {
                        SX1276.Settings.State = RF_IDLE;
                    }
                    TimerStop( &RxTimeoutTimer );

                    if( ( RadioEvents != NULL ) && ( RadioEvents->RxDone != NULL ) )
                    {

                        //@Description : 这里是废弃的用于区分：1.接收到用于时间同步的时间，2.用于测试同步精度，来自第三方消息的时间，我选择用发送不同的消息长度来作区分。
                        /*
                        if(SX1276.Settings.LoRaPacketHandler.Size == 5){
                            lora_printf("RxDoneTime:%lu\n",sysTime);
                        }
                        if(SX1276.Settings.LoRaPacketHandler.Size == 10){
                            lora_printf("RxTime:%lu\n",sysTime);
                        }
                        */


                        //@Description : 如果是开启跳频，会在这里打印跳频接收的时间，主要是接收完Header时的时间。
                        if(SX1276.Settings.LoRa.FreqHopOn == true){
                            lora_printf("RxHeaderTime:%lu\n",freqHoppingTime);
                        }

                        //@Description : 打印触发RxDone时的时间，以及多计算了一个空中传输时间。
                        lora_printf("RxDoneTime:%lu\n",sysTime);
                    	DIO_PRINTF("DIO0:RX Done\r\n");
                        lora_printf("TimeOnAir:%d\n",SX1276GetTimeOnAir(MODEM_LORA,SX1276.Settings.LoRaPacketHandler.Size)*1000);
                        RadioEvents->RxDone( RxTxBuffer, SX1276.Settings.LoRaPacketHandler.Size, SX1276.Settings.LoRaPacketHandler.RssiValue, SX1276.Settings.LoRaPacketHandler.SnrValue );
                    }
                }
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            TimerStop( &TxTimeoutTimer );
            // TxDone interrupt
            switch( SX1276.Settings.Modem )
            {
            case MODEM_LORA:
                // Clear Irq
                SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_TXDONE );
                // Intentional fall through
            case MODEM_FSK:
            default:
                SX1276.Settings.State = RF_IDLE;
                if( ( RadioEvents != NULL ) && ( RadioEvents->TxDone != NULL ) )
                {
                    //@Description : 打印触发TxDone时的时间
                    lora_printf("TxDoneTime:%lu\n",sysTime);
                	DIO_PRINTF("DIO0:TX Done\r\n");
                    RadioEvents->TxDone( );
                }
                break;
            }
            break;
        default:
            break;
    }
}

void SX1276OnDio1Irq( void )
{
	lora_printf("1\r\n");
    switch( SX1276.Settings.State )
    {
        case RF_RX_RUNNING:
            switch( SX1276.Settings.Modem )
            {
            case MODEM_FSK:
                // FifoLevel interrupt
                // Read received packet size
                if( ( SX1276.Settings.FskPacketHandler.Size == 0 ) && ( SX1276.Settings.FskPacketHandler.NbBytes == 0 ) )
                {
                    if( SX1276.Settings.Fsk.FixLen == false )
                    {
                        SX1276ReadFifo( ( uint8_t* )&SX1276.Settings.FskPacketHandler.Size, 1 );
                    }
                    else
                    {
                        SX1276.Settings.FskPacketHandler.Size = SX1276Read( REG_PAYLOADLENGTH );
                    }
                }

                if( ( SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes ) > SX1276.Settings.FskPacketHandler.FifoThresh )
                {
                    SX1276ReadFifo( ( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes ), SX1276.Settings.FskPacketHandler.FifoThresh );
                    SX1276.Settings.FskPacketHandler.NbBytes += SX1276.Settings.FskPacketHandler.FifoThresh;
                }
                else
                {
                    SX1276ReadFifo( ( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes ), SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                    SX1276.Settings.FskPacketHandler.NbBytes += ( SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                }
                break;
            case MODEM_LORA:
                // Sync time out
                TimerStop( &RxTimeoutTimer );
                // Clear Irq
                SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_RXTIMEOUT );

                SX1276.Settings.State = RF_IDLE;
                if( ( RadioEvents != NULL ) && ( RadioEvents->RxTimeout != NULL ) )
                {
                	DIO_PRINTF("DIO1:RX Timeout\r\n");
                    RadioEvents->RxTimeout( );
                }
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            switch( SX1276.Settings.Modem )
            {
            case MODEM_FSK:
                // FifoEmpty interrupt
                if( ( SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes ) > SX1276.Settings.FskPacketHandler.ChunkSize )
                {
                    SX1276WriteFifo( ( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes ), SX1276.Settings.FskPacketHandler.ChunkSize );
                    SX1276.Settings.FskPacketHandler.NbBytes += SX1276.Settings.FskPacketHandler.ChunkSize;
                }
                else
                {
                    // Write the last chunk of data
                    SX1276WriteFifo( RxTxBuffer + SX1276.Settings.FskPacketHandler.NbBytes, SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes );
                    SX1276.Settings.FskPacketHandler.NbBytes += SX1276.Settings.FskPacketHandler.Size - SX1276.Settings.FskPacketHandler.NbBytes;
                }
                break;
            case MODEM_LORA:
                break;
            default:
                break;
            }
            break;
        default:
            break;
    }
}


void SX1276OnDio2Irq( void )
{
    switch( SX1276.Settings.State )
    {
        case RF_RX_RUNNING:
            switch( SX1276.Settings.Modem )
            {
            case MODEM_FSK:
                // Checks if DIO4 is connected. If it is not PreambleDetected is set to true.
                if( SX1276.DIO4.port == NULL )
                {
                    SX1276.Settings.FskPacketHandler.PreambleDetected = true;
                }

                if( ( SX1276.Settings.FskPacketHandler.PreambleDetected == true ) && ( SX1276.Settings.FskPacketHandler.SyncWordDetected == false ) )
                {
                    TimerStop( &RxTimeoutSyncWord );

                    SX1276.Settings.FskPacketHandler.SyncWordDetected = true;

                    SX1276.Settings.FskPacketHandler.RssiValue = -( SX1276Read( REG_RSSIVALUE ) >> 1 );

                    SX1276.Settings.FskPacketHandler.AfcValue = ( int32_t )( double )( ( ( uint16_t )SX1276Read( REG_AFCMSB ) << 8 ) |
                                                                           ( uint16_t )SX1276Read( REG_AFCLSB ) ) *
                                                                           ( double )FREQ_STEP;
                    SX1276.Settings.FskPacketHandler.RxGain = ( SX1276Read( REG_LNA ) >> 5 ) & 0x07;
                }
                break;
            case MODEM_LORA:
                if( SX1276.Settings.LoRa.FreqHopOn == true )
                {
                    // Clear Irq
                    SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL );
                    //lora_printf("Interrupt\n");
                    

                    /*-------------------------------------------------------------
                    
                    @Description : 这里触发跳频中断，由于实验是记录发送/接收完Header的时间，因此通过hopchannel来查看是否发送/接收完成
                                    很重要的是，手册说明前导码和header是在channel0上发送，发送完成后跳到channnel1，
                                    但实测，在程序上是channel == 2时，发送完成前导码和header
                                    具体实测为，改变前导码的长度时，若channel == 2，这个时间与发送完成的时间之间相差的值并不怎么变化，说明都是发送完payload的时间
                                    相反，channel == 1时，改变前导码的长度，这个差会变长，也就是说channel == 1时，前导码并没有发送。
                                    还有，由于接收时，channel是一直在改变，以防止错过接收的时间，所以可能会触发好几次channel == 2，但是只取最后一次，因为最后一次
                                    才是真正的接收，时间在rxdone处打印。
                    @author : william
                    -------------------------------------------------------------*/
                    int presentChannel = SX1276Read(REG_LR_HOPCHANNEL); 
                    lora_printf("channel:%d\n",presentChannel);
                    if(presentChannel == 2){
                        freqHoppingTime = micros();
                    }
                    if( ( RadioEvents != NULL ) && ( RadioEvents->FhssChangeChannel != NULL ) )
                    {
                        RadioEvents->FhssChangeChannel( ( SX1276Read( REG_LR_HOPCHANNEL ) & RFLR_HOPCHANNEL_CHANNEL_MASK ) );
                    }
                }
                break;
            default:
                break;
            }
            break;
        case RF_TX_RUNNING:
            switch( SX1276.Settings.Modem )
            {
            case MODEM_FSK:
                break;
            case MODEM_LORA:
                if( SX1276.Settings.LoRa.FreqHopOn == true )
                {
                    // Clear Irq
                    SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_FHSSCHANGEDCHANNEL );
                    
                    //lora_printf("Interrupt\n");

                    /*---------------------------------------------------
                    @Description : 中断的原理同上面接收的一致，此处仅多一个函数，用于将记录的时间写入fifo，同payload一起发送
                    @author : william

                    ----------------------------------------------------*/
                    int presentChannel = SX1276Read(REG_LR_HOPCHANNEL); 

                    lora_printf("channel:%d\n",presentChannel);
                    if(presentChannel == 2){
                        freqHoppingTime = micros();
                        ChannelHoppingTx();
                    }
                    if( ( RadioEvents != NULL ) && ( RadioEvents->FhssChangeChannel != NULL ) )
                    {
                        RadioEvents->FhssChangeChannel( ( SX1276Read( REG_LR_HOPCHANNEL ) & RFLR_HOPCHANNEL_CHANNEL_MASK ) );
                    }
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
    }
}

void SX1276OnDio3Irq( void )
{
    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        break;
    case MODEM_LORA:
        if( ( SX1276Read( REG_LR_IRQFLAGS ) & RFLR_IRQFLAGS_CADDETECTED ) == RFLR_IRQFLAGS_CADDETECTED )
        {
            // Clear Irq
            SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_CADDETECTED | RFLR_IRQFLAGS_CADDONE );
            if( ( RadioEvents != NULL ) && ( RadioEvents->CadDone != NULL ) )
            {
                RadioEvents->CadDone( true );
            }
        }
        else
        {
            // Clear Irq
            SX1276Write( REG_LR_IRQFLAGS, RFLR_IRQFLAGS_CADDONE );
            if( ( RadioEvents != NULL ) && ( RadioEvents->CadDone != NULL ) )
            {
                RadioEvents->CadDone( false );
            }
        }
        break;
    default:
        break;
    }
}

void SX1276OnDio4Irq( void )
{
    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        {
            if( SX1276.Settings.FskPacketHandler.PreambleDetected == false )
            {
                SX1276.Settings.FskPacketHandler.PreambleDetected = true;
            }
        }
        break;
    case MODEM_LORA:
        break;
    default:
        break;
    }
}

void SX1276OnDio5Irq( void )
{
    switch( SX1276.Settings.Modem )
    {
    case MODEM_FSK:
        break;
    case MODEM_LORA:
        break;
    default:
        break;
    }
}

/*-----------------------------------------------

@Description : 此处为触发中断的函数，extern引入的变量为在arduino文件中绑定中断信号，触发中断信号后会进入相应的判断，然后进入具体的处理函数。
                原文件仅定义Irq0和Irq1，我们引入Irq2。
@author : william
------------------------------------------------*/

extern bool Irq0Fired;
extern bool Irq1Fired;
extern bool Irq2Fired;


void  sx1276RadioIrqProcess(void)
{
    /*------------------------------------------------
    
    @Description : 触发Irq0时，主要就是RxDone和TxDone，因此在这里记录，是最近的。
                    但由于跳频Irq2会多次触发，因此就不在这里记录，去具体的处理函数中，判断channel再记录。

    ----------------------------------------------*/
	if(Irq0Fired)
	{
        sysTime = micros();
		Irq0Fired = false;
		SX1276OnDio0Irq();
	}
	if(Irq1Fired)
	{
		Irq1Fired = false;
		SX1276OnDio1Irq();
	}
    if(Irq2Fired)
	{
		Irq2Fired = false;
		SX1276OnDio2Irq();
	}
}
#endif
