#include <esp_camera.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

#define CAMERA_MODEL_MAKERFABS // check camera_pins.h for other camera model
#include "camera_pins.h"

#define PREVIEW_QUALITY 12 // 1-63, 1 is the best
#define PREVIEW_SIZE FRAMESIZE_QVGA
#define SNAP_QUALITY 6 // 1-63, 1 is the best
#define SNAP_SIZE FRAMESIZE_UXGA

#define SCK 14
#define MOSI 13
#define MISO 12
#define SS 4
#define TFT_CS 15
#define TFT_DC 33
#define TFT_RST -1
Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, SCK, MOSI, MISO);
Arduino_TFT *gfx = new Arduino_ILI9488_18bit(bus, TFT_RST, 1 /* rotation */);

#include "src/JpegDec.h"
static JpegDec jpegDec;

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
  gfx->drawFastHLine(78, 38, 16, LIGHTGREY);
  gfx->drawFastHLine(386, 38, 16, LIGHTGREY);
  gfx->drawFastHLine(78, 281, 16, LIGHTGREY);
  gfx->drawFastHLine(386, 281, 16, LIGHTGREY);
  gfx->drawFastVLine(78, 38, 16, LIGHTGREY);
  gfx->drawFastVLine(401, 38, 16, LIGHTGREY);
  gfx->drawFastVLine(78, 266, 16, LIGHTGREY);
  gfx->drawFastVLine(401, 266, 16, LIGHTGREY);
  gfx->fillRect(79, 39, 322, 242, LIGHTGREY);

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  // SPIClass spi = SPIClass(VSPI_HOST);
  // spi.begin(14, 12, 13, 4);
  if (!SD.begin(4, SPI, 80000000))
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
        NULL,                  /* Parameter passed as input of the task */
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
  s->set_vflip(s, true);
  s->set_quality(s, PREVIEW_QUALITY);
  s->set_framesize(s, PREVIEW_SIZE);
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
  s->set_framesize(s, SNAP_SIZE);

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
  esp_deep_sleep_start();
}

void loop()
{
  if ((millis() > 5000) && (millis() < 6000))
  {
    Serial.println(F("Start snap!"));

    gfx->fillScreen(BLACK);

    snap();

    delay(5000);
    Serial.println("Enter deep sleep...");
    enterSleep();
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
      jpegDec.prepare(jpegDec.buff_reader, fb->buf);
      jpegDec.decode(JPG_SCALE_NONE, gfx_writer1, gfx);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
  }

  i++;
}

static bool gfx_writer1(JpegDec *jpegDec, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data)
{
  if (data)
  {
    // Serial.printf("%d, %d, %d, %d\n", x, y, w, h);
    if (x >= 207)
    {
      x += 81;
    }
    else if (x >= 111)
    {
      x += 80;
    }
    else
    {
      x += 79;
    }
    if (y >= 159)
    {
      y += 41;
    }
    else if (y >= 79)
    {
      y += 40;
    }
    else
    {
      y += 39;
    }
    gfx->draw24bitRGBBitmap(x, y, data, w, h);
  }
  return true; // Continue to decompression
}
