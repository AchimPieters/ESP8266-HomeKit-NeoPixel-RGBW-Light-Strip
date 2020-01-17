/*
   Copyright 2019 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   More information: https://www.studiopieters.nl

   NOTE:
   1) the ws2812_i2s library uses hardware I2S so output pin is GPIO3 and cannot be changed.
   2) on some ESP8266 such as the Wemos D1 mini, GPIO3 is the same pin used for serial comms.

 */


#include <stdio.h>
#include <stdlib.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <math.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>
#include "ws2812_i2s/ws2812_i2s.h"

#include "button.h"

#define LED_ON 0                // this is the value to write to GPIO for led on (0 = GPIO low)
#define LED_INBUILT_GPIO 2      // this is the onboard LED used to show on/off only
#define LED_COUNT 15            // this is the number of WS2812B leds on the strip
#define LED_RGBW_SCALE 255       // this is the scaling factor used for color conversion

// Global variables
float led_hue = 0;              // hue is scaled 0 to 360
float led_saturation = 59;      // saturation is scaled 0 to 100
float led_brightness = 100;     // brightness is scaled 0 to 100
bool led_on = false;            // on is boolean on or off
ws2812_pixel_t pixels[LED_COUNT];

// The GPIO pin that is connected to the button on the esp.
const int button_gpio = 0;
void button_callback(uint8_t gpio, button_event_t event);

void reset_configuration_task() {
        //Flash the LED first before we start the reset
        for (int i=0; i<3; i++) {
                gpio_write(LED_INBUILT_GPIO, LED_ON);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
                vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        printf("Resetting Wifi Config\n");

        wifi_config_reset();

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Resetting HomeKit Config\n");

        homekit_server_reset();

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("Restarting\n");

        sdk_system_restart();

        vTaskDelete(NULL);
}

void reset_configuration() {
        printf("Resetting configuration\n");
        xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);
}

void button_callback(uint8_t gpio, button_event_t event) {
        switch (event) {
        case button_event_single_press:
                //your_function_here();
                //printf("your text here\n");
                break;
        case button_event_long_press:
                // press for 4 Seconds
                reset_configuration();
                break;
        default:
                printf("Unknown button event: %d\n", event);
        }
}

//http://blog.saikoled.com/post/44677718712/how-to-convert-from-hsi-to-rgb-white
static void hsi2rgbw(float h, float s, float i, ws2812_pixel_t* rgbw) {

        int r, g, b, w;

        while (h < 0) { h += 360.0F; }; // cycle h around to 0-360 degrees
        while (h >= 360) { h -= 360.0F; };
        h = 3.14159F*h / 180.0F;        // convert to radians.
        s /= 100.0F;                    // from percentage to ratio
        i /= 100.0F;                    // from percentage to ratio
        s = s > 0 ? (s < 1 ? s : 1) : 0; // clamp s and i to interval [0,1]
        i = i > 0 ? (i < 1 ? i : 1) : 0; // clamp s and i to interval [0,1]
        i = i * sqrt(i);                // shape intensity to have finer granularity near 0

        if (h < 2.09439) {
                r = LED_RGBW_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                g = LED_RGBW_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                b = LED_RGBW_SCALE * i / 3 * (1 - s);
                w = 255*(1-s)*i;
        }
        else if (h < 4.188787) {
                h = h - 2.09439;
                g = LED_RGBW_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                b = LED_RGBW_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                r = LED_RGBW_SCALE * i / 3 * (1 - s);
                w = 255*(1-s)*i;
        }
        else {
                h = h - 4.188787;
                b = LED_RGBW_SCALE * i / 3 * (1 + s * cos(h) / cos(1.047196667 - h));
                r = LED_RGBW_SCALE * i / 3 * (1 + s * (1 - cos(h) / cos(1.047196667 - h)));
                g = LED_RGBW_SCALE * i / 3 * (1 - s);
                w = 255*(1-s)*i;
        }

        rgbw->red = (uint8_t) r;
        rgbw->green = (uint8_t) g;
        rgbw->blue = (uint8_t) b;
        rgbw->white = (uint8_t) w;
}

void led_string_fill(ws2812_pixel_t rgbw) {

        // write out the new color to each pixel
        for (int i = 0; i < LED_COUNT; i++) {
                pixels[i] = rgbw;
        }
        ws2812_i2s_update(pixels, PIXEL_RGBW);
}

void led_string_set(void) {
        ws2812_pixel_t rgbw = { { 0, 0, 0, 0 } };

        if (led_on) {
                // convert HSI to RGBW
                hsi2rgbw(led_hue, led_saturation, led_brightness, &rgbw);
                //printf("h=%d,s=%d,b=%d => ", (int)led_hue, (int)led_saturation, (int)led_brightness);
                //printf("r=%d,g=%d,b=%d,w=%d\n", rgbw.red, rgbw.green, rgbw.blue, rgbw.white);

                // set the inbuilt led
                gpio_write(LED_INBUILT_GPIO, LED_ON);
        }
        else {
                // printf("off\n");
                gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
        }

        // write out the new color
        led_string_fill(rgbw);
}

void led_init() {
        // initialise the onboard led as a secondary indicator (handy for testing)
        gpio_enable(LED_INBUILT_GPIO, GPIO_OUTPUT);

        // initialise the LED strip
        ws2812_i2s_init(LED_COUNT, PIXEL_RGBW);

        // set the initial state
        led_string_set();
}

void identify_task(void *_args) {
        const ws2812_pixel_t COLOR_PINK = { { 255, 0, 127, 0 } };
        const ws2812_pixel_t COLOR_BLACK = { { 0, 0, 0, 0 } };


        for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                        gpio_write(LED_INBUILT_GPIO, LED_ON);
                        led_string_fill(COLOR_PINK);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        gpio_write(LED_INBUILT_GPIO, 1 - LED_ON);
                        led_string_fill(COLOR_BLACK);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }

        led_string_set();
        vTaskDelete(NULL);
}

void identify(homekit_value_t _value) {
        // printf("LED identify\n");
        xTaskCreate(identify_task, "identify", 128, NULL, 2, NULL);
}

homekit_value_t led_on_get() {
        return HOMEKIT_BOOL(led_on);
}

void led_on_set(homekit_value_t value) {
        if (value.format != homekit_format_bool) {
                // printf("Invalid on-value format: %d\n", value.format);
                return;
        }

        led_on = value.bool_value;
        led_string_set();
}

homekit_value_t led_brightness_get() {
        return HOMEKIT_INT(led_brightness);
}
void led_brightness_set(homekit_value_t value) {
        if (value.format != homekit_format_int) {
                // printf("Invalid brightness-value format: %d\n", value.format);
                return;
        }
        led_brightness = value.int_value;
        led_string_set();
}

homekit_value_t led_hue_get() {
        return HOMEKIT_FLOAT(led_hue);
}

void led_hue_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                // printf("Invalid hue-value format: %d\n", value.format);
                return;
        }
        led_hue = value.float_value;
        led_string_set();
}

homekit_value_t led_saturation_get() {
        return HOMEKIT_FLOAT(led_saturation);
}

void led_saturation_set(homekit_value_t value) {
        if (value.format != homekit_format_float) {
                // printf("Invalid sat-value format: %d\n", value.format);
                return;
        }
        led_saturation = value.float_value;
        led_string_set();
}


homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_lightbulb, .services = (homekit_service_t*[]) {
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "RGBW Strip"),
                        HOMEKIT_CHARACTERISTIC(MANUFACTURER, "StudioPieters®"),
                        HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "Q39QDPS7GRX7"),
                        HOMEKIT_CHARACTERISTIC(MODEL, "HKSP1T/S"),
                        HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.0.1"),
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
                        NULL
                }),
                HOMEKIT_SERVICE(LIGHTBULB, .primary = true, .characteristics = (homekit_characteristic_t*[]) {
                        HOMEKIT_CHARACTERISTIC(NAME, "RGBW Strip"),
                        HOMEKIT_CHARACTERISTIC(
                                ON, true,
                                .getter = led_on_get,
                                .setter = led_on_set
                                ),
                        HOMEKIT_CHARACTERISTIC(
                                BRIGHTNESS, 100,
                                .getter = led_brightness_get,
                                .setter = led_brightness_set
                                ),
                        HOMEKIT_CHARACTERISTIC(
                                HUE, 0,
                                .getter = led_hue_get,
                                .setter = led_hue_set
                                ),
                        HOMEKIT_CHARACTERISTIC(
                                SATURATION, 0,
                                .getter = led_saturation_get,
                                .setter = led_saturation_set
                                ),
                        NULL

                }),
                NULL
        }),
        NULL
};

homekit_server_config_t config = {
        .accessories = accessories,
        .password = "070-45-077",
        .setupId="1NP7",
};

void user_init(void) {
        uart_set_baud(0, 115200);

        void on_wifi_ready();
        led_init();
        homekit_server_init(&config);

        if (button_create(button_gpio, 0, 4000, button_callback)) {
                printf("Failed to initialize button\n");
        }
}
