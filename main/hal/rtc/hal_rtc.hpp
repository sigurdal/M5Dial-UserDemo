/**
 * @file hal_rtc.hpp
 * @author Forairaaaaa
 * @brief Thanks to https://github.com/nopnop2002/esp-idf-pcf8563
 * @version 0.1
 * @date 2023-05-21
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#pragma once
#include <driver/i2c.h>
#include <esp_log.h>
#include <ctime>
#include <cstdint>

/**
 * @brief Modify from "hal_tp.hpp"
 * 
 */
namespace PCF8563 {


    static const char* TAG = "PCF8563";


    struct Config_t {
        int pin_scl     = -1;
        int pin_sda     = -1;
        int pin_int     = -1;
        i2c_port_t i2c_port = I2C_NUM_0;

        uint8_t dev_addr = 0x51;
    };


    class PCF8563 {
        private:
            Config_t _cfg;
            uint8_t _data_buffer[8];

            inline esp_err_t _write_reg(uint8_t reg, uint8_t data)
            {
                _data_buffer[0] = reg;
                _data_buffer[1] = data; 
                return i2c_master_write_to_device(_cfg.i2c_port, _cfg.dev_addr, _data_buffer, 2, portMAX_DELAY);
            }

            inline esp_err_t _read_reg(uint8_t reg, uint8_t readSize)
            {
                /* Store data into buffer */
                return i2c_master_write_read_device(_cfg.i2c_port, _cfg.dev_addr, &reg, 1, _data_buffer, readSize, portMAX_DELAY);
            }

            inline uint8_t bcd2dec(uint8_t val)
            {
                return (val >> 4) * 10 + (val & 0x0f);
            }

            inline uint8_t dec2bcd(uint8_t val)
            {
                return ((val / 10) << 4) + (val % 10);
            }


        public:

            /* Config */
            inline Config_t config() { return _cfg; }
            inline void config(const Config_t& cfg) { _cfg = cfg; }
            inline void setPin(const int& sda, const int& scl, const int& intr)
            {
                _cfg.pin_sda = sda;
                _cfg.pin_scl = scl;
                _cfg.pin_int = intr;
            }


            inline bool init(const int& sda, const int& scl, const int& intr = -1)
            {
                setPin(sda, scl, intr);
                return init();
            }

            inline bool init()
            {
                gpioInit();


                /// TimerCameraの内蔵RTCが初期化に失敗することがあったため、最初に空打ちする; 
                // writeRegister8(0x00, 0x00);
                // _init = writeRegister8(0x00, 0x00) && writeRegister8(0x0E, 0x03);
                _write_reg(0x00, 0x00);
                _write_reg(0x00, 0x00);
                _write_reg(0x0E, 0x03);


                return true;
            }


            /* Setup gpio and reset */
            inline void gpioInit()
            {
                ESP_LOGD(TAG, "setup gpio");

                if (_cfg.pin_int > 0) {
                    gpio_reset_pin((gpio_num_t)_cfg.pin_int);
                    gpio_set_direction((gpio_num_t)_cfg.pin_int, GPIO_MODE_INPUT);
                }
            }


            inline esp_err_t getTime(tm& time)
            {
                /* Time and date registers */
                esp_err_t res = _read_reg(0x02, 7);
                if (res != ESP_OK) {
                    return res;
                }

                ESP_LOGD(TAG, "data=%02x %02x %02x %02x %02x %02x %02x\n", 
                    _data_buffer[0],_data_buffer[1],_data_buffer[2],_data_buffer[3],_data_buffer[4],_data_buffer[5],_data_buffer[6]);

                /* convert to unix time structure */
                time.tm_sec = bcd2dec(_data_buffer[0] & 0x7F);
                time.tm_min = bcd2dec(_data_buffer[1] & 0x7F);
                time.tm_hour = bcd2dec(_data_buffer[2] & 0x3F);
                time.tm_mday = bcd2dec(_data_buffer[3] & 0x3F);
                time.tm_wday = bcd2dec(_data_buffer[4] & 0x07);		    // tm_wday is 0 to 6
                time.tm_mon  = bcd2dec(_data_buffer[5] & 0x1F) - 1;	    // tm_mon is 0 to 11
                time.tm_year = bcd2dec(_data_buffer[6]) + 2000;
                time.tm_isdst = 0;

                ESP_LOGD(TAG, "%02d:%02d:%02d %d-%d-%d-%d\n", 
                    time.tm_hour, time.tm_min, time.tm_sec, time.tm_year, time.tm_mon, time.tm_mday, time.tm_wday);

                return ESP_OK;
            }


            inline esp_err_t setTime(const tm& time)
            {
                /* Time and date registers */
                _data_buffer[0] = 0x02;

                _data_buffer[1] = dec2bcd(time.tm_sec);
                _data_buffer[2] = dec2bcd(time.tm_min);
                _data_buffer[3] = dec2bcd(time.tm_hour);
                _data_buffer[4] = dec2bcd(time.tm_mday);
                _data_buffer[5] = dec2bcd(time.tm_wday);		        // tm_wday is 0 to 6
                _data_buffer[6] = dec2bcd(time.tm_mon + 1);	            // tm_mon is 0 to 11
                _data_buffer[7] = dec2bcd(time.tm_year - 2000);
                    
                return i2c_master_write_to_device(_cfg.i2c_port, _cfg.dev_addr, _data_buffer, 8, portMAX_DELAY);
            }


            /// Set timer IRQ
            /// @param afterSeconds 1 - 15,300. If 256 or more, 1-minute cycle.  (max 255 minute.)
            /// @return the set number of seconds.
            inline int setAlarmIRQ(int afterSeconds)
            {
                // std::uint8_t reg_value = readRegister8(0x01) & ~0x0C;
                _read_reg(0x01, 1);
                std::uint8_t reg_value = _data_buffer[0] & ~0x0C;


                if (afterSeconds < 0)
                { // disable timer
                    // writeRegister8(0x01, reg_value & ~0x01);
                    _write_reg(0x01, reg_value & ~0x01);

                    // writeRegister8(0x0E, 0x03);
                    _write_reg(0x0E, 0x03);
                

                    return -1;
                }

                std::size_t div = 1;
                std::uint8_t type_value = 0x82;
                if (afterSeconds < 270)
                {
                    if (afterSeconds > 255)
                    {
                        afterSeconds = 255;
                    }
                }
                else
                {
                    div = 60;
                    afterSeconds = (afterSeconds + 30) / div;
                    if (afterSeconds > 255)
                    {
                        afterSeconds = 255;
                    }
                    type_value = 0x83;
                }

                // writeRegister8(0x0E, type_value);
                _write_reg(0x0E, type_value);

                // writeRegister8(0x0F, afterSeconds);
                _write_reg(0x0F, afterSeconds);

                // writeRegister8(0x01, (reg_value | 0x01) & ~0x80);
                _write_reg(0x01, (reg_value | 0x01) & ~0x80);

                return afterSeconds * div;
            }


            inline void clearIRQ(void)
            {
                // if (!_init) { return; }
                // bitOff(0x01, 0x0C);

                _read_reg(0x01, 1);
                _data_buffer[0] = _data_buffer[0] & 0B11110011;
                _write_reg(0x01, _data_buffer[0]);

            }


            inline void disableIRQ(void)
            {
                // if (!_init) { return; }
                // // disable alerm (bit7:1=disabled)
                // static constexpr const std::uint8_t buf[4] = { 0x80, 0x80, 0x80, 0x80 };
                // writeRegister(0x09, buf, 4);

                _data_buffer[0] = 0x09;

                _data_buffer[1] = 0x80;
                _data_buffer[2] = 0x80;
                _data_buffer[3] = 0x80;
                _data_buffer[4] = 0x80;

                i2c_master_write_to_device(_cfg.i2c_port, _cfg.dev_addr, _data_buffer, 5, portMAX_DELAY);



                // // disable timer (bit7:0=disabled)
                // writeRegister8(0x0E, 0);
                
                _write_reg(0x0E, 0);

                // // clear flag and INT enable bits
                // writeRegister8(0x01, 0x00);

                _write_reg(0x01, 0x00);
            }


    };


}