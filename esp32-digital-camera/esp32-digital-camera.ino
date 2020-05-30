#include <esp_camera.h>
#include <SD.h>
#include <rom/tjpgd.h>
#include <Arduino_HWSPI.h>
#include <Arduino_Display.h> // Various display driver

#define CAMERA_MODEL_TTGO_T_CAMERA // check camera_pins.h for other camera model
#include "camera_pins.h"
#include "tjpgdec.h"

#define PREVIEW_QUALITY 63 // 1-63, 1 is the best
#define SNAP_QUALITY 6 // 1-63, 1 is the best
#define SNAP_SIZE FRAMESIZE_UXGA

#define TFT_BL 2
#define SCK 21
#define MOSI 19
#define MISO 22
#define SS 0
Arduino_HWSPI *bus = new Arduino_HWSPI(15 /* DC */, 12 /* CS */, SCK, MOSI, MISO);
Arduino_ST7789 *gfx = new Arduino_ST7789(bus, -1 /* RST */, 2 /* rotation */, true /* IPS */, 240 /* width */, 240 /* height */, 0 /* col offset 1 */, 80 /* row offset 1 */);

char tmpStr[256];
char nextFilename[31];
uint16_t fileIdx = 0;
int i = 0;
sensor_t *s;
camera_fb_t *fb = 0;

void setup()
{
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  gfx->begin();
  gfx->fillScreen(BLACK);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  if (!SD.begin(SS, SPI, 80000000))
  {
    Serial.println("SD Init Fail!");
  }
  else
  {
    snprintf(tmpStr, sizeof(tmpStr), "SD Card Type: %d Size: %lu MB", SD.cardType(), (long unsigned int)SD.cardSize() / 1024 / 1024);
    Serial.println(tmpStr);

    init_folder();

    xTaskCreate(
        findNextFileIdxTask,   /* Task function. */
        "FindNextFileIdxTask", /* String with name of task. */
        10000,                 /* Stack size in bytes. */
        work,                  /* Parameter passed as input of the task */
        1,                     /* Priority of the task. */
        NULL);                 /* Task handle. */
  }

  esp_err_t err = cam_init();
  if (err != ESP_OK)
  {
    snprintf(tmpStr, sizeof(tmpStr), "Camera init failed with error 0x%x", err);
    Serial.println(tmpStr);
  }

  //drop down frame size for higher initial frame rate
  s = esp_camera_sensor_get();
  s->set_brightness(s, 2);
  s->set_contrast(s, 2);
  s->set_saturation(s, 2);
  s->set_sharpness(s, 2);
  s->set_aec2(s, true);
  s->set_denoise(s, true);
  s->set_lenc(s, true);
  // s->set_hmirror(s, true);
  // s->set_vflip(s, true);
  s->set_quality(s, PREVIEW_QUALITY);

  work = (char *)heap_caps_malloc(WORK_BUF_SIZE, MALLOC_CAP_DMA);
  dev.linbuf_idx = 0;
  dev.x = 0;
  dev.y = 0;
  dev.linbuf[0] = (color_t *)heap_caps_malloc(JPG_IMAGE_LINE_BUF_SIZE * 3, MALLOC_CAP_DMA);
  dev.linbuf[1] = (color_t *)heap_caps_malloc(JPG_IMAGE_LINE_BUF_SIZE * 3, MALLOC_CAP_DMA);
}

esp_err_t cam_init()
{
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // init with high specs to pre-allocate larger buffers
  config.frame_size = SNAP_SIZE;
  config.jpeg_quality = SNAP_QUALITY;
  config.fb_count = 2;

  // camera init
  return esp_camera_init(&config);
}

void init_folder()
{
  File file = SD.open("/DCIM");
  if (!file)
  {
    Serial.println(F("Create /DCIM"));
    SD.mkdir("/DCIM");
  }
  else
  {
    Serial.println(F("Found /DCIM"));
    file.close();
  }
  file = SD.open("/DCIM/100ESPDC");
  if (!file)
  {
    Serial.println(F("Create /DCIM/100ESPDC"));
    SD.mkdir("/DCIM/100ESPDC");
  }
  else
  {
    Serial.println(F("Found /DCIM/100ESPDC"));
    file.close();
  }
}

void findNextFileIdxTask(void *parameter)
{
  findNextFileIdx();
  vTaskDelete(NULL);
}

void findNextFileIdx()
{ // TODO: revise ugly code
  fileIdx++;
  File file;
  snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05d.JPG", fileIdx);
  file = SD.open(nextFilename);
  if (!file)
  {
    Serial.printf("Next file: %s\n", nextFilename);
    return;
  }
  else
  {
    for (int k = 1000; k <= 30000; k += 1000)
    {
      snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05d.JPG", fileIdx + k);
      file = SD.open(nextFilename);
      if (file)
      {
        Serial.printf("Found %s\n", nextFilename);
        file.close();
      }
      else
      {
        Serial.printf("Not found %s\n", nextFilename);
        k -= 1000;
        for (int h = 100; h <= 1000; h += 100)
        {
          snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05d.JPG", fileIdx + k + h);
          file = SD.open(nextFilename);
          if (file)
          {
            Serial.printf("Found %s\n", nextFilename);
            file.close();
          }
          else
          {
            Serial.printf("Not found %s\n", nextFilename);
            h -= 100;
            for (int t = 10; t <= 100; t += 10)
            {
              snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05d.JPG", fileIdx + k + h + t);
              file = SD.open(nextFilename);
              if (file)
              {
                Serial.printf("Found %s\n", nextFilename);
                file.close();
              }
              else
              {
                Serial.printf("Not found %s\n", nextFilename);
                t -= 10;
                for (int d = 1; d <= 10; d++)
                {
                  snprintf(nextFilename, sizeof(nextFilename), "/DCIM/100ESPDC/DSC%05d.JPG", fileIdx + k + h + t + d);
                  file = SD.open(nextFilename);
                  if (file)
                  {
                    Serial.printf("Found %s\n", nextFilename);
                    file.close();
                  }
                  else
                  {
                    Serial.printf("Next file: %s\n", nextFilename);
                    fileIdx += k + h + t + d;
                    return;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

unsigned int tjd_output(
    JDEC *jd,     // Decompression object of current session
    void *bitmap, // Bitmap data to be output
    JRECT *rect   // Rectangular region to output
)
{
  // Serial.printf("%d, %d, %d, %d\n", rect->top, rect->left, rect->bottom, rect->right);
  gfx->draw24bitRGBBitmap(rect->left + 20, rect->top + 20, bitmap, rect->right - rect->left + 1, rect->bottom - rect->top + 1);

  return 1; // Continue to decompression
}

void saveFile()
{
  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println(F("Camera capture JPG failed"));
  }
  else
  {
    snprintf(tmpStr, sizeof(tmpStr), "File open: %s", nextFilename);
    Serial.println(tmpStr);
    File file = SD.open(nextFilename, FILE_WRITE);
    if (file.write(fb->buf, fb->len))
    {
      snprintf(tmpStr, sizeof(tmpStr), "File written: %luKB\n%s", (long unsigned int)fb->len / 1024, nextFilename);
      Serial.println(tmpStr);
    }
    else
    {
      Serial.println(F("Write failed!"));
    }

    esp_camera_fb_return(fb);
    fb = NULL;
    file.close();
  }
}

void snap()
{
  // s->set_hmirror(s, false);
  // s->set_vflip(s, false);
  s->set_quality(s, SNAP_QUALITY);

  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  fb = NULL;

  saveFile();
  findNextFileIdx();
  saveFile();
  findNextFileIdx();
  saveFile();
  findNextFileIdx();

  // s->set_hmirror(s, true);
  // s->set_vflip(s, true);
  s->set_quality(s, PREVIEW_QUALITY);

  fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  fb = NULL;
}

void enterSleep()
{
  gfx->end();
  esp_deep_sleep_start();
}

void loop()
{
  if ((millis() > 5000) && (millis() < 6000))
  {
    Serial.println(F("Start snap!"));

    gfx->fillScreen(BLACK);

    snap();

    // delay(5000);
    // Serial.println("Enter deep sleep...");
    // enterSleep();
  }
  else
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      Serial.println(F("Camera capture failed!"));
    }
    else
    {
      decodeJpegBuff(fb->buf, fb->len, 3);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
  }

  i++;
}
