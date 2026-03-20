#include "nrf24_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "nRF24";
// nRF24L01+ register map
#define NRF_CONFIG  0x00
#define NRF_EN_AA   0x01
#define NRF_EN_RXADDR 0x02
#define NRF_RF_CH   0x05
#define NRF_RF_SETUP 0x06
#define NRF_STATUS  0x07
#define NRF_RX_ADDR_P0 0x0A
#define NRF_TX_ADDR 0x10
#define NRF_RX_PW_P0 0x11
#define NRF_W_TX_PAYLOAD 0xA0
#define NRF_R_RX_PAYLOAD 0x61
#define NRF_FLUSH_TX 0xE1
#define NRF_FLUSH_RX 0xE2
#define NRF_NOP      0xFF

NRF24Driver& NRF24Driver::getInstance(){ static NRF24Driver i; return i; }

esp_err_t NRF24Driver::init() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = PIN_NRF_MOSI; bus.miso_io_num = PIN_NRF_MISO;
    bus.sclk_io_num = PIN_NRF_SCLK; bus.quadwp_io_num=-1; bus.quadhd_io_num=-1;
    esp_err_t r = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (r!=ESP_OK && r!=ESP_ERR_INVALID_STATE) return r;

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = NRF_SPI_HZ; dev.mode=0;
    dev.spics_io_num = PIN_NRF_CS; dev.queue_size=3;
    if (spi_bus_add_device(SPI2_HOST, &dev, &m_spi) != ESP_OK) return ESP_FAIL;

    gpio_set_direction((gpio_num_t)PIN_NRF_CE, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)PIN_NRF_IRQ, GPIO_MODE_INPUT);
    ce(false);

    // Verify nRF presence by reading CONFIG register
    uint8_t cfg = readReg(NRF_CONFIG);
    if (cfg == 0xFF) { ESP_LOGW(TAG,"nRF24 not found"); return ESP_FAIL; }

    writeReg(NRF_CONFIG,   0x0B); // Power up, RX mode, CRC 1-byte
    writeReg(NRF_EN_AA,    0x01); // Auto-ACK pipe 0
    writeReg(NRF_EN_RXADDR,0x01); // Enable pipe 0
    writeReg(NRF_RF_CH,    NRF_CHANNEL);
    writeReg(NRF_RF_SETUP, 0x06); // 1Mbps, 0dBm
    writeReg(NRF_RX_PW_P0, NRF_PAYLOAD_SIZE);
    command(NRF_FLUSH_TX); command(NRF_FLUSH_RX);
    writeReg(NRF_STATUS, 0x70); // Clear IRQ flags

    ESP_LOGI(TAG,"nRF24 ready, ch=%d payload=%d",NRF_CHANNEL,NRF_PAYLOAD_SIZE);
    return ESP_OK;
}

void NRF24Driver::writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2]={uint8_t(0x20|reg),val};
    csn(true); spi_transaction_t t={}; t.length=16; t.tx_buffer=buf;
    spi_device_polling_transmit(m_spi,&t); csn(false);
}
uint8_t NRF24Driver::readReg(uint8_t reg) {
    uint8_t tx[2]={reg,NRF_NOP}, rx[2]={};
    csn(true); spi_transaction_t t={}; t.length=16; t.tx_buffer=tx; t.rx_buffer=rx;
    spi_device_polling_transmit(m_spi,&t); csn(false);
    return rx[1];
}
void NRF24Driver::command(uint8_t cmd) {
    csn(true); spi_transaction_t t={}; t.length=8; t.tx_buffer=&cmd;
    spi_device_polling_transmit(m_spi,&t); csn(false);
}
void NRF24Driver::csn(bool low){ gpio_set_level((gpio_num_t)PIN_NRF_CS, low?0:1); }
void NRF24Driver::ce(bool high){ gpio_set_level((gpio_num_t)PIN_NRF_CE, high?1:0); }

void NRF24Driver::setChannel(uint8_t ch){ writeReg(NRF_RF_CH, ch&0x7F); }
void NRF24Driver::setAddress(const uint8_t addr[5]){
    writeRegs(NRF_TX_ADDR,     addr, 5);
    writeRegs(NRF_RX_ADDR_P0,  addr, 5);
}
void NRF24Driver::writeRegs(uint8_t reg, const uint8_t* buf, uint8_t len){
    uint8_t cmd = 0x20|reg;
    csn(true); spi_transaction_t t={}; t.length=8; t.tx_buffer=&cmd;
    spi_device_polling_transmit(m_spi,&t);
    t.length=len*8; t.tx_buffer=buf;
    spi_device_polling_transmit(m_spi,&t); csn(false);
}
bool NRF24Driver::send(const uint8_t* data, uint8_t len){
    stopListening();
    writeReg(NRF_CONFIG, 0x0A); // TX mode
    uint8_t cmd=NRF_W_TX_PAYLOAD;
    csn(true); spi_transaction_t t={}; t.length=8; t.tx_buffer=&cmd;
    spi_device_polling_transmit(m_spi,&t);
    t.length=len*8; t.tx_buffer=data;
    spi_device_polling_transmit(m_spi,&t); csn(false);
    ce(true); vTaskDelay(pdMS_TO_TICKS(1)); ce(false);
    uint32_t timeout=5000; uint8_t st;
    do{ st=readReg(NRF_STATUS); vTaskDelay(1); } while(!(st&0x30)&&--timeout);
    writeReg(NRF_STATUS,0x70); command(NRF_FLUSH_TX);
    return (st&0x20)!=0; // TX_DS bit
}
bool NRF24Driver::available(){ return (readReg(NRF_STATUS)&0x40)!=0; }
uint8_t NRF24Driver::read(uint8_t* buf){
    csn(true); spi_transaction_t t={}; uint8_t cmd=NRF_R_RX_PAYLOAD;
    t.length=8; t.tx_buffer=&cmd; spi_device_polling_transmit(m_spi,&t);
    t.length=NRF_PAYLOAD_SIZE*8; t.rxlength=NRF_PAYLOAD_SIZE*8;
    t.tx_buffer=nullptr; t.rx_buffer=buf;
    spi_device_polling_transmit(m_spi,&t); csn(false);
    writeReg(NRF_STATUS,0x40);
    return NRF_PAYLOAD_SIZE;
}
void NRF24Driver::startListening(){ m_listening=true; writeReg(NRF_CONFIG,0x0B); ce(true); }
void NRF24Driver::stopListening() { ce(false); m_listening=false; }
void NRF24Driver::pollTask(){
    while(true){
        if(m_listening && available() && m_cb){
            uint8_t buf[NRF_PAYLOAD_SIZE]; uint8_t n=read(buf); m_cb(buf,n);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool NRF24Driver::readCarrierDetect() {
    // Read register 0x09 (CD = Carrier Detect)
    // SPI transaction: send reg address | 0x00 (read), receive byte
    gpio_set_level((gpio_num_t)PIN_NRF_CS, 0);
    spi_transaction_t t = {};
    t.flags   = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
    t.length  = 16;
    t.tx_data[0] = 0x09;  // CD register
    t.tx_data[1] = 0xFF;
    spi_device_polling_transmit(m_spi, &t);
    gpio_set_level((gpio_num_t)PIN_NRF_CS, 1);
    return (t.rx_data[1] & 0x01) != 0;
}

// BlueJammer control stubs
void NRF24Driver::setAutoAck(int en){ writeRegister(0x01,en?0x3F:0x00); }
void NRF24Driver::setPALevel(int lv){ uint8_t v=readRegByte(0x06); v=(v&0xF9)|((lv&0x3)<<1); writeRegister(0x06,v); }
void NRF24Driver::setDataRate(int dr){ uint8_t v=readRegByte(0x06); if(dr==2)v|=(1<<3); else v&=~(1<<3); writeRegister(0x06,v); }
void NRF24Driver::setCRCLength(int len){ uint8_t v=readRegByte(0x00); if(len==0)v&=~(1<<3); else v|=(1<<3); writeRegister(0x00,v); }
void NRF24Driver::setPayloadSize(int sz){ writeRegister(0x11,(uint8_t)sz); }
void NRF24Driver::setAddressWidth(int w){ writeRegister(0x03,(uint8_t)(w-2)); }
void NRF24Driver::setCE(bool high){ gpio_set_level((gpio_num_t)PIN_NRF_CE, high?1:0); }
bool NRF24Driver::writeRegister(uint8_t reg, uint8_t val){ uint8_t buf[2]={reg,val}; spi_transaction_t t={}; t.length=16; t.tx_buffer=buf; return spi_device_polling_transmit(m_spi,&t)==ESP_OK; }
