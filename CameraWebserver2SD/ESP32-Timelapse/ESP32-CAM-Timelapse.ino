.
/*
 * YOUTUBE beta version v13
 
 
  this code  = https://github.com/v12345vtm/CameraWebserver2SD/blob/master/CameraWebserver2SD/ESP32-Timelapse/ESP32-CAM-Timelapse.ino
  04/03/2019 v12345vtm 
  please subscribe to my youtube channel to support and to recieve more updates.
   https://www.youtube.com/user/v12345vtm  please subscribe if this code helped you
   https://www.paypal.me/v12345vtm/3 if you the video/code helped you , buy me a coffee :)
   
   
   this arduino sketch uses :
   -websockets to communicate to the webserver.
   -Jpg pictures can be stored into the SDcard on the board.
   -the webserver uses JSON format to parse the esp32-cam variables
   -toggle the small led on the pcb via webinterface ( socket)

   TO program this code to your ESP32-CAM use these settings : 
ESP32 Wrovermodule
Huge app (3Mb no Ota)
Qio , 80Mhz , 921600 .

these lib need to be installed on your pc ( arduino ide ) 
https://dl.espressif.com/dl/package_esp32_index.json
 and install ESP32 lib !!
JSON version : ArduinoJson by benoit blanchon  v 5.13.4

     
   this code is beta , and tested on ESP32-CAM with  OV2640 (sold with board) and is not tested with OV7670 cameras .
   
  
  
   ESP32-CAM board uses  the SD card to the following pins:
   SD Card | ESP32    |esp32-cam
      D2       -          -
      D3       SS         gpio13
      CMD      MOSI       gpio15
      VSS      GND        gnd
      VDD      3.3V       3.3v
      CLK      SCK        gpio14
      VSS      GND        gnd
      D0       MISO       gpio2
      D1       -          gpio4 + LED flash also  :(

  FLASHLED                gpio4
  led1                    gpio33 (mini smd ledje below ESP32-controler)
      SD card socket : pin 9 is SD ( = CARD DETECTION , is a card inserted ? ) 

  
  ESP.restart();

*/
#include "WebSocketsServer.h"//WebSocketsServer
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "FS.h" //sd card esp32
#include "SD_MMC.h" //sd card esp32
#include "soc/soc.h" //disable brownour problems
#include "soc/rtc_cntl_reg.h"  //disable brownour problems 
#include <WiFi.h>
#include "dl_lib.h"
#include "esp_http_server.h"
#include "time.h"
#define CAMERA_MODEL_AI_THINKER  //this ode is only for ESP32-CAM !
#define PART_BOUNDARY "123456789000000000000987654321"

const int LED_1 = 33; //small led1 on back of the ESP32-CAM
const int FOTO_FLASH = 4; //BIG led1 on front of the ESP32-CAM (default is dual use with sdcard data
const char* ssid = "WiFiSSID";
const char* password = "wifipassword";


WebSocketsServer webSocket = WebSocketsServer(81);
static void writeLED(bool);
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
int filename = 1 ; //filename when internettime fails
String vith ;
//{"SDmax":"socket",
String sdmaxvalue = "11";
//"SDused":"socket"
String sdusedvalue = "22";
//"Flashled":"socket"
String flashledvalue = "33";
//Led
bool LEDStatus ;
bool tlrunningvalue = false;// we zijn nog niet ant timelapsen
//"Timelapseinterval""socket"
int TLinterval = 1;    //  "timelapsedelay TLint:60
//"Timelapsecounter":"socket"
String Timelapsecountervalue = "0";//voor als we een coutndoenw timelapse willen doen
//"Lastfilename":"socket"}
String Lastfilenamevalue = "55";
//"RTC":"socket"}
//String RTCvalue = "rtc"; //ingvuld via realtimeklok()
String payloadstring = "";
String plkey , plvalue ; //payload split op :
String streamportvalue = "9601"; // nog updaten in index javascript
 

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

String timestampforfilename() //creates a filename for jpg saved picture for SD card
{
  String bestandnaam = String(filename) + ".jpg";
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain timestamp used counter");
    //String bestandnaam = String(filename) + ".jpg";
    return bestandnaam ;//in case of no internettime ntp connection
  }
  Serial.print(&timeinfo, "%y%m%d-%H%M%S.jpg");//timestamp for pictures eg. 190222-164559.jpg
   char timeStringBuff[50]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff),  "%y%m%d-%H%M%S.jpg", &timeinfo);
  Serial.println(timeStringBuff);
  //Optional: Construct String object
  //String asString(timeStringBuff);
  return  (timeStringBuff);
}





static void toggleTimelapse()
{
 
  if (tlrunningvalue) {
  tlrunningvalue = false;
  Timelapsecountervalue = "0";
  }
  else {
  tlrunningvalue  = true;
resetTLcounter();

  
  }
}




void resetTLcounter()
{
  
 String  tl_start = longlong2string(esp_timer_get_time());
   int tl_end = tl_start.toInt()+ (TLinterval+60);
Timelapsecountervalue = (String)tl_end;
    
 Serial.println("nu"); 
 Serial.println(tl_start);
 Serial.println("tlint");
 Serial.println(TLinterval*5);
 Serial.println("berek .eindtijd"); 
 Serial.println(Timelapsecountervalue);

  
  }





String realtimeklok() //creates a filename for jpg saved picture for SD card
{  
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("RTC failed");
    //String bestandnaam = String(filename) + ".jpg";
    return "geenRTC" ;//in case of no internettime ntp connection
  }
 // Serial.print(&timeinfo, "%y%m%d-%H%M%S");//timestamp for pictures eg. 190222-164559.jpg
   char timeStringBuff[20]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff),  "%y%m%d-%H:%M:%S", &timeinfo);
  Serial.println(timeStringBuff);
  //Optional: Construct String object
  //String asString(timeStringBuff);
  return  (timeStringBuff);
}

void makejsoncontainer()
{
  //de json is een string die we sturen via socket
  //de browser javascript zullen we dit socket doen parsen naar json object
  vith = "" ; //   \" is een quote
  vith += '{';
  vith +=   "\"RTC\":\"" ; //  
 vith += realtimeklok(); //

  vith +=   "\",\"ESPtimer\":\"" ; //
 vith += longlong2string (esp_timer_get_time()); //runtime van de esp in seconden

vith +=   "\",\"Streamport\":\"" ; //
 vith += streamportvalue; //runtime van de esp in seconden

  vith +=   "\",\"TLrunning\":\"" ; //
  vith += String(tlrunningvalue); //is timelapse bezig of niet ? ja of nee

 
  vith +=   "\",\"SDmax\":\"" ; //
  vith += sdmaxvalue;
  vith +=   "\",\"SDused\":\"" ; //
  vith += sdusedvalue;
  vith +=   "\",\"Flashled\":\"" ; //
  vith += flashledvalue;
  vith +=   "\",\"Led\":\"" ; //
  vith += String(LEDStatus);
  vith +=   "\",\"Timelapseinterval\":\"" ; //
  vith += String(TLinterval); //TLint:60 komt via socket vd browser en bewaren we als int in de esp
  vith +=   "\",\"Timelapsecounter\":\"" ; //
  vith += Timelapsecountervalue;
  vith +=   "\",\"Lastfilename\":\"" ; //
  vith += Lastfilenamevalue;
  vith += "\"}";
  Serial.print(vith);
}

String longlong2string ( uint64_t longlong )
{
  static char sdgeheugen[20];
  //Serial.println("convert sd memory in stringformat");
  char * p = sdgeheugen;
  p += sprintf(p, "%llu", longlong / (1024 * 1024));
  *p++ = 0;
  // Serial.print("Camera sdgeheugen! ");
  //Serial.println(sdgeheugen); // 3813
  return sdgeheugen;
}


void ImageToSd()
{
  if (!SD_MMC.begin()) {
    Serial.println("sdcard gone?");
  }
  else {
    Lastfilenamevalue =  timestampforfilename(); //take a numeber from the ticketmachine
    Lastfilenamevalue = "/" + Lastfilenamevalue;
    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get(); //take picture from cam
    if (!fb) {
      Serial.println("ImageToSd failed");
      return ;
    }
    Serial.println ("fb lengte=");
    Serial.println ( fb->len );
    Serial.println ("fb naam=");
    Serial.println ( Lastfilenamevalue);
    fs::FS &fs = SD_MMC;
    // Serial.printf("Writing file: %s\n", destinationpath);
    File file = fs.open(Lastfilenamevalue, FILE_WRITE);
    if (!file) {
      Serial.println("FImageToSd (Error)File");
    }
    else
    {
      file.write(fb->buf , fb->len);
      Serial.println("sd write ImageToSd OK");
      // sdmaxvalue = longlong2string (SD_MMC.totalBytes());
      sdusedvalue = longlong2string (SD_MMC.usedBytes());// hoeveel plaats is er verbruikt op de sd?
    }
    esp_camera_fb_return(fb);//free buffer
  }
}//end function

void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);
  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}
void createDir(fs::FS &fs, const char * path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}
void removeDir(fs::FS &fs, const char * path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}
void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\n", path);
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
}
void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
}
void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
}
void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}
void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}
void testFileIO(fs::FS &fs, const char * path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  } else {
    Serial.println("Failed to open file for reading");
  }
  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}
static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
    Serial.println("lijn459-jpg-encode-stream !index (met FACE recog. passeren we hier");
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    Serial.println("lijn462 httpsendchunk");
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  int64_t fr_start = esp_timer_get_time();
  fb = esp_camera_fb_get(); //take picture from cam
  if (!fb) {
    Serial.println("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  size_t out_len, out_width, out_height;
  uint8_t * out_buf;
  bool s;
  // bool detected = false;
  // int face_id = 0;
  if ( fb->width > 400) {
    Serial.println("lijn493");
    size_t fb_len = 0;
    if (fb->format == PIXFORMAT_JPEG) {
      Serial.println("pixformatjpg for httpd send");
      fb_len = fb->len;
      res = httpd_resp_send(req, (const char *)fb->buf, fb->len);

    } else {
      jpg_chunking_t jchunk = {req, 0};
      Serial.println(jchunk.len);
      res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
      Serial.println("frame2jpg");
      httpd_resp_send_chunk(req, NULL, 0);
      fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);


    int64_t fr_end = esp_timer_get_time();
    Serial.println(res);
    Serial.printf("JPG: %uB %ums\n\n\n\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
    return res;
  }

  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  if (!image_matrix) {
    esp_camera_fb_return(fb);
    Serial.println("dl_matrix3du_alloc failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  out_buf = image_matrix->item;
  out_len = fb->width * fb->height * 3;
  out_width = fb->width;
  out_height = fb->height;
  s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
  esp_camera_fb_return(fb);
  if (!s) {
    dl_matrix3du_free(image_matrix);
    Serial.println("to rgb888 failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  jpg_chunking_t jchunk = {req, 0};
  s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);

  if (!s) {
    Serial.println("JPEG compression failed");
    return ESP_FAIL;
  }
  // int64_t fr_end = esp_timer_get_time();
  // Serial.printf("FACE: %uB %ums %s%d\n", (uint32_t)(jchunk.len), (uint32_t)((fr_end - fr_start) / 1000), detected ? "DETECTED " : "", face_id);
  return res;
}
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  dl_matrix3du_t *image_matrix = NULL;
  int64_t fr_start = 0;
  int64_t fr_ready = 0;
  //int64_t fr_face = 0;
  int64_t fr_encode = 0;
  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      fr_start = esp_timer_get_time();
      fr_ready = fr_start;
     // fr_face = fr_start;
      fr_encode = fr_start;
      if (  fb->width > 400) {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
      else {
        image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
        if (!image_matrix) {
          Serial.println("dl_matrix3du_alloc failed");
          res = ESP_FAIL;
        }
        else {
          if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
            Serial.println("fmt2rgb888 failed");
            res = ESP_FAIL;
          } else {
            fr_ready = esp_timer_get_time();
            box_array_t *net_boxes = NULL;
//            fr_face = esp_timer_get_time();

            if (  fb->format != PIXFORMAT_JPEG) {
              if (!fmt2jpg(image_matrix->item, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len)) {
                Serial.println("fmt2jpg failed");
                res = ESP_FAIL;
              }
              esp_camera_fb_return(fb);
              fb = NULL;
            }
            else {
              _jpg_buf = fb->buf;
              _jpg_buf_len = fb->len;
            }
            fr_encode = esp_timer_get_time();
          }

        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    //    int64_t fr_end = esp_timer_get_time();
    //    int64_t ready_time = (fr_ready - fr_start) / 1000;
    //    int64_t process_time = (fr_encode - fr_start) / 1000;
    //    int64_t frame_time = fr_end - last_frame;
    //    last_frame = fr_end;
    //    frame_time /= 1000;
    Serial.printf("MJPG: ");
  }
  last_frame = 0;
  return res;
}
static esp_err_t cmd_handler(httpd_req_t *req) {
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};  //from http request : parameters var
  char value[32] = {0,};//parameter value
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {//reaquest parameters ontleden
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {

        Serial.printf("\n req parameters var ");
        Serial.printf(variable);
        Serial.printf("\n req value ");
        Serial.printf(value);

      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;
  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) res = s->set_framesize(s, (framesize_t)val);
  }
  //  else if (!strcmp(variable, "timelapsedelay")) {
  //    res = 1;
  //    vertragingTL = atoi(value); //string to intiger
  //    Serial.println("timelapse delay set to " + String(vertragingTL) + "minuten" );
  //  }

  else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
  else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
  else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
  else if (!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
  else if (!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
  else if (!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
  else if (!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
  else if (!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
  else if (!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
  else if (!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
  else if (!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
  else if (!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
  else if (!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
  else if (!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
  else if (!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
  else if (!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
  else if (!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
  else if (!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
  else if (!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
  else if (!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val); //set_ae_level is methode in de klasse s van de camerasettings

  else if (!strcmp(value, "fotoaub"))
  { res = 1;
    ImageToSd(); //get image ans store to SD
    Serial.println("button foto");
  }


  //  else if (!strcmp(value, "Stop_timelapse"))
  //  { res = 1;
  //    ImageToSd(); //get image ans store to SD
  //    Serial.print("timelapst is running elke ");
  //    Serial.print(vertragingTL);
  //    Serial.println("minuten");
  //  }
  //
  //
  //  else if (!strcmp(value, "Start_timelapse"))
  //  { res = 1 ;//
  //    ImageToSd(); //get image ans store to SD
  //    Serial.println("timelapse stopped");
  //  }

  if (res) {
    return httpd_resp_send_500(req);
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];
  Serial.println("writing statuspagina");
  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize); //status uitlezen van camerasensor
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"timelapsedelay\":%u,", 1);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static const char PROGMEM INDEX2_HTML[] = R"rawliteral(
<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>v12345vtm ESP32-IPCAM</title>
<style>body {
  font-family: Arial, Helvetica, sans-serif;
  background: #181818;
  color: #EFEFEF;
  font-size: 16px
}

h2 {
  font-size: 18px
}

section.main {
  display: flex
}

#menu,
section.main {
  flex-direction: column
}

#menu {
  display: none;
  flex-wrap: nowrap;
  min-width: 340px;
  background: #363636;
  padding: 8px;
  border-radius: 4px;
  margin-top: -10px;
  margin-right: 10px
}

#content {
  display: flex;
  flex-wrap: wrap;
  align-items: stretch
}

figure {
  padding: 0;
  margin: 0;
  -webkit-margin-before: 0;
  margin-block-start: 0;
  -webkit-margin-after: 0;
  margin-block-end: 0;
  -webkit-margin-start: 0;
  margin-inline-start: 0;
  -webkit-margin-end: 0;
  margin-inline-end: 0
}

figure img {
  display: block;
  width: 100%;
  height: auto;
  border-radius: 4px;
  margin-top: 8px
}

@media (min-width: 800px) and (orientation:landscape) {
  #content {
    display: flex;
    flex-wrap: nowrap;
    align-items: stretch
  }
  figure img {
    display: block;
    max-width: 100%;
    max-height: calc(100vh - 40px);
    width: auto;
    height: auto
  }
  figure {
    padding: 0;
    margin: 0;
    -webkit-margin-before: 0;
    margin-block-start: 0;
    -webkit-margin-after: 0;
    margin-block-end: 0;
    -webkit-margin-start: 0;
    margin-inline-start: 0;
    -webkit-margin-end: 0;
    margin-inline-end: 0
  }
}

section#buttons {
  display: flex;
  flex-wrap: nowrap;
  justify-content: space-between
}

#nav-toggle {
  cursor: pointer;
  display: block
}

#nav-toggle-cb {
  outline: 0;
  opacity: 0;
  width: 0;
  height: 0
}

#nav-toggle-cb:checked+#menu {
  display: flex
}

.input-group {
  display: flex;
  flex-wrap: nowrap;
  line-height: 22px;
  margin: 5px 0
}

.input-group>label {
  display: inline-block;
  padding-right: 10px;
  min-width: 47%
}

.input-group input,
.input-group select {
  flex-grow: 1
}

.range-max,
.range-min {
  display: inline-block;
  padding: 0 5px
}

button {
  display: block;
  margin: 5px;
  padding: 0 12px;
  border: 0;
  line-height: 28px;
  cursor: pointer;
  color: #fff;
  background: #ff3034;
  border-radius: 5px;
  font-size: 16px;
  outline: 0
}

button:hover {
  background: #ff494d
}

button:active {
  background: #f21c21
}

button.disabled {
  cursor: default;
  background: #a0a0a0
}

input[type=range] {
  -webkit-appearance: none;
  width: 100%;
  height: 22px;
  background: #363636;
  cursor: pointer;
  margin: 0
}

input[type=range]:focus {
  outline: 0
}

input[type=range]::-webkit-slider-runnable-track {
  width: 100%;
  height: 2px;
  cursor: pointer;
  background: #EFEFEF;
  border-radius: 0;
  border: 0 solid #EFEFEF
}

input[type=range]::-webkit-slider-thumb {
  border: 1px solid rgba(0, 0, 30, 0);
  height: 22px;
  width: 22px;
  border-radius: 50px;
  background: #ff3034;
  cursor: pointer;
  -webkit-appearance: none;
  margin-top: -11.5px
}

input[type=range]:focus::-webkit-slider-runnable-track {
  background: #EFEFEF
}

input[type=range]::-moz-range-track {
  width: 100%;
  height: 2px;
  cursor: pointer;
  background: #EFEFEF;
  border-radius: 0;
  border: 0 solid #EFEFEF
}

input[type=range]::-moz-range-thumb {
  border: 1px solid rgba(0, 0, 30, 0);
  height: 22px;
  width: 22px;
  border-radius: 50px;
  background: #ff3034;
  cursor: pointer
}

input[type=range]::-ms-track {
  width: 100%;
  height: 2px;
  cursor: pointer;
  background: 0 0;
  border-color: transparent;
  color: transparent
}

input[type=range]::-ms-fill-lower {
  background: #EFEFEF;
  border: 0 solid #EFEFEF;
  border-radius: 0
}

input[type=range]::-ms-fill-upper {
  background: #EFEFEF;
  border: 0 solid #EFEFEF;
  border-radius: 0
}

input[type=range]::-ms-thumb {
  border: 1px solid rgba(0, 0, 30, 0);
  height: 22px;
  width: 22px;
  border-radius: 50px;
  background: #ff3034;
  cursor: pointer;
  height: 2px
}

input[type=range]:focus::-ms-fill-lower {
  background: #EFEFEF
}

input[type=range]:focus::-ms-fill-upper {
  background: #363636
}

.switch {
  display: block;
  position: relative;
  line-height: 22px;
  font-size: 16px;
  height: 22px
}

.switch input {
  outline: 0;
  opacity: 0;
  width: 0;
  height: 0
}

.slider {
  width: 50px;
  height: 22px;
  border-radius: 22px;
  cursor: pointer;
  background-color: grey
}

.slider,
.slider:before {
  display: inline-block;
  transition: .4s
}

.slider:before {
  position: relative;
  content: "";
  border-radius: 50%;
  height: 16px;
  width: 16px;
  left: 4px;
  top: 3px;
  background-color: #fff
}

input:checked+.slider {
  background-color: #ff3034
}

input:checked+.slider:before {
  -webkit-transform: translateX(26px);
  transform: translateX(26px)
}

select {
  border: 1px solid #363636;
  font-size: 14px;
  height: 22px;
  outline: 0;
  border-radius: 5px
}

.image-container {
  position: relative;
  min-width: 160px
}

.close {
  position: absolute;
  right: 5px;
  top: 5px;
  background: #ff3034;
  width: 16px;
  height: 16px;
  border-radius: 100px;
  color: #fff;
  text-align: center;
  line-height: 18px;
  cursor: pointer
}

.hidden {
  display: none
}

</style>
    </head>
   <body > 
 <a href="https://www.youtube.com/user/v12345vtm" target="_blank" style="color:white">please subscribe to my channel</a>
<a href=" /status" target="_blank" style="color:white">esp_status</a>

<a href=" /export"   style="color:white">websocket page : export</a>
        <section class="main">
  <div id="logo">
                <label for="nav-toggle-cb" id="nav-toggle">&#9776;&nbsp;&nbsp;Toggle settings</label>
            </div>
            <div id="content">
                <div id="sidebar">
                    <input type="checkbox" id="nav-toggle-cb" checked="checked">
                    <nav id="menu">
                        <div class="input-group" id="framesize-group">
                            <label for="framesize">Resolution</label>
                            <select id="framesize" class="default-action">
                                <option value="10" selected="selected">UXGA(1600x1200)</option>
                                <option value="9">SXGA(1280x1024)</option>
                                <option value="8">XGA(1024x768)</option>
                                <option value="7">SVGA(800x600)</option>
                                <option value="6">VGA(640x480)</option>
                               </select>
                        </div>
                        <div class="input-group" id="quality-group">
                            <label for="quality">Quality</label>
                            <div class="range-min">10</div>
                            <input type="range" id="quality" min="10" max="63" value="10" class="default-action">
                            <div class="range-max">63</div>
                        </div>
                        <div class="input-group" id="brightness-group">
                            <label for="brightness">Brightness</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="brightness" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="contrast-group">
                            <label for="contrast">Contrast</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="contrast" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="saturation-group">
                            <label for="saturation">Saturation</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="saturation" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="special_effect-group">
                            <label for="special_effect">Special Effect</label>
                            <select id="special_effect" class="default-action">
                                <option value="0" selected="selected">No Effect</option>
                                <option value="1">Negative</option>
                                <option value="2">Grayscale</option>
                                <option value="3">Red Tint</option>
                                <option value="4">Green Tint</option>
                                <option value="5">Blue Tint</option>
                                <option value="6">Sepia</option>
                            </select>
                        </div>
                        <div class="input-group" id="awb-group">
                            <label for="awb">AWB</label>
                            <div class="switch">
                                <input id="awb" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="awb"></label>
                            </div>
                        </div>
                        <div class="input-group" id="awb_gain-group">
                            <label for="awb_gain">AWB Gain</label>
                            <div class="switch">
                                <input id="awb_gain" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="awb_gain"></label>
                            </div>
                        </div>
                        <div class="input-group" id="wb_mode-group">
                            <label for="wb_mode">WB Mode</label>
                            <select id="wb_mode" class="default-action">
                                <option value="0" selected="selected">Auto</option>
                                <option value="1">Sunny</option>
                                <option value="2">Cloudy</option>
                                <option value="3">Office</option>
                                <option value="4">Home</option>
                            </select>
                        </div>
                        <div class="input-group" id="aec-group">
                            <label for="aec">AEC SENSOR</label>
                            <div class="switch">
                                <input id="aec" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="aec"></label>
                            </div>
                        </div>
                        <div class="input-group" id="aec2-group">
                            <label for="aec2">AEC DSP</label>
                            <div class="switch">
                                <input id="aec2" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="aec2"></label>
                            </div>
                        </div>
                        <div class="input-group" id="ae_level-group">
                            <label for="ae_level">AE Level</label>
                            <div class="range-min">-2</div>
                            <input type="range" id="ae_level" min="-2" max="2" value="0" class="default-action">
                            <div class="range-max">2</div>
                        </div>
                        <div class="input-group" id="aec_value-group">
                            <label for="aec_value">Exposure</label>
                            <div class="range-min">0</div>
                            <input type="range" id="aec_value" min="0" max="1200" value="204" class="default-action">
                            <div class="range-max">1200</div>
                        </div>
                        <div class="input-group" id="agc-group">
                            <label for="agc">AGC</label>
                            <div class="switch">
                                <input id="agc" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="agc"></label>
                            </div>
                        </div>
                        <div class="input-group hidden" id="agc_gain-group">
                            <label for="agc_gain">Gain</label>
                            <div class="range-min">1x</div>
                            <input type="range" id="agc_gain" min="0" max="30" value="5" class="default-action">
                            <div class="range-max">31x</div>
                        </div>
                        <div class="input-group" id="gainceiling-group">
                            <label for="gainceiling">Gain Ceiling</label>
                            <div class="range-min">2x</div>
                            <input type="range" id="gainceiling" min="0" max="6" value="0" class="default-action">
                            <div class="range-max">128x</div>
                        </div>
                        <div class="input-group" id="bpc-group">
                            <label for="bpc">BPC</label>
                            <div class="switch">
                                <input id="bpc" type="checkbox" class="default-action">
                                <label class="slider" for="bpc"></label>
                            </div>
                        </div>
                        <div class="input-group" id="wpc-group">
                            <label for="wpc">WPC</label>
                            <div class="switch">
                                <input id="wpc" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="wpc"></label>
                            </div>
                        </div>
                        <div class="input-group" id="raw_gma-group">
                            <label for="raw_gma">Raw GMA</label>
                            <div class="switch">
                                <input id="raw_gma" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="raw_gma"></label>
                            </div>
                        </div>
                        <div class="input-group" id="lenc-group">
                            <label for="lenc">Lens Correction</label>
                            <div class="switch">
                                <input id="lenc" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="lenc"></label>
                            </div>
                        </div>
                        <div class="input-group" id="hmirror-group">
                            <label for="hmirror">H-Mirror</label>
                            <div class="switch">
                                <input id="hmirror" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="hmirror"></label>
                            </div>
                        </div>
                        <div class="input-group" id="vflip-group">
                            <label for="vflip">V-Flip</label>
                            <div class="switch">
                                <input id="vflip" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="vflip"></label>
                            </div>
                        </div>
                        <div class="input-group" id="dcw-group">
                            <label for="dcw">DCW (Downsize EN)</label>
                            <div class="switch">
                                <input id="dcw" type="checkbox" class="default-action" checked="checked">
                                <label class="slider" for="dcw"></label>
                            </div>
                        </div>
                        <div class="input-group" id="colorbar-group">
                            <label for="colorbar">Color Bar</label>
                            <div class="switch">
                                <input id="colorbar" type="checkbox" class="default-action">
                                <label class="slider" for="colorbar"></label>
                            </div>
                        </div>
                      
                              
                        <section id="buttons">
                            <button id="get-still">Get Still</button>
                            <button id="toggle-stream">Start Stream</button>
                            <button id="Image2SD"  >Save Image2SD</button>
                               
                        </section>

                       
                    </nav>
                </div>
                <figure>
                    <div id="stream-container" class="image-container hidden">
                        <div class="close" id="close-stream">×</div>
                        <img id="stream" src="">
                    </div>
                </figure>
            </div>
        </section>        
     <script>
        document.addEventListener('DOMContentLoaded', function() {
    function b(B) {
        let C;
        switch (B.type) {
            case 'checkbox':
                C = B.checked ? 1 : 0;
                break;
            case 'range':
            case 'select-one':
                C = B.value;
                break;
            case 'button':
            case 'submit':
      console.log ("foto Javascript to uri to image2sd");
       if (B.id ==="Image2SD"){              
                 const fotomaken = `${c}/control?var=${B.id}&val=${"fotoaub"}`; //send javascript to url handler /control?var=face_enroll&val=1
        fetch(fotomaken).then(E => {console.log(`request to ${D} finished, status: ${E.status}`)})  //where the magic happens
        return;              
              }

     
              
        C = '1';
                break;
            default:
                return;
        }
        const D = `${c}/control?var=${B.id}&val=${C}`; //send javascript to url handler /control?var=face_enroll&val=1
        fetch(D).then(E => {console.log(`request to ${D} finished, status: ${E.status}`)})  //where the magic happens
    }
    var c = document.location.origin;
    const e = B => {
            B.classList.add('hidden')
        },
        f = B => {
            B.classList.remove('hidden')
        },
        g = B => {
            B.classList.add('disabled'), B.disabled = !0
        },
        h = B => {
            B.classList.remove('disabled'), B.disabled = !1
        },
        i = (B, C, D) => {
            D = !(null != D) || D;
            let E;
            'checkbox' === B.type ? (E = B.checked, C = !!C, B.checked = C) : (E = B.value, B.value = C), D && E !== C ? b(B) : !D && ('aec' === B.id ? C ? e(v) : f(v) : 'agc' === B.id ? C ? (f(t), e(s)) : (e(t), f(s)) : 'awb_gain' === B.id ? C ? f(x) : e(x) : 'face_recognize' === B.id && (C ? h(n) : g(n)))
        };
    document.querySelectorAll('.close').forEach(B => {
        B.onclick = () => {
            e(B.parentNode)
        }
    }), fetch(`${c}/status`).then(function(B) {
   
        return B.json()
    }).then(function(B) {
        document.querySelectorAll('.default-action').forEach(C => {
            i(C, B[C.id], !1)
        })
    });
    const j = document.getElementById('stream'),
        k = document.getElementById('stream-container'),
        l = document.getElementById('get-still'),
        m = document.getElementById('toggle-stream'),
        n = document.getElementById('Image2SD'),
         TL = document.getElementById('TimeLapse'),
        o = document.getElementById('close-stream'),
        p = () => {window.stop(), m.innerHTML = 'Start Stream'  },
        q = () => {j.src = `${c+':9601'}/stream`, f(k), m.innerHTML = 'Stop Stream'};
       l.onclick = () => {
        p(), j.src = `${c}/capture?_cb=${Date.now()}`, f(k)
    }, o.onclick = () => {
        p(), e(k)
    }, m.onclick = () => {
  const B = 'Stop Stream' === m.innerHTML;
        B ? p() : q()  }, 
    
    
  
      
      
      
      n.onclick = () => {
        b(n)
    }, document.querySelectorAll('.default-action').forEach(B => {
        B.onchange = () => b(B)
    });
    const r = document.getElementById('agc'),
        s = document.getElementById('agc_gain-group'),
        t = document.getElementById('gainceiling-group');
    r.onchange = () => {
        b(r), r.checked ? (f(t), e(s)) : (e(t), f(s))
    };
    const u = document.getElementById('aec'),
        v = document.getElementById('aec_value-group');
    u.onchange = () => {
        b(u), u.checked ? e(v) : f(v)
    };
    const w = document.getElementById('awb_gain'),
        x = document.getElementById('wb_mode-group');
    w.onchange = () => {
        b(w), w.checked ? f(x) : e(x)
    };
    //const y = document.getElementById('face_detect'),
      //  z = document.getElementById('face_recognize'),
    const    A = document.getElementById('framesize');
    A.onchange = () => {
        b(A), 5 < A.value && (i(y, !1), i(z, !1))
    } 
});
      </script>     
    </body>
</html>

)rawliteral";



/////////////////exporthtml
static const char PROGMEM EXPORT_HTML[] = R"rawliteral(





<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>v12345vtm export</title>
<script>
var websock;
var vt_tjson;

function wsjsonrecieved(evt)
{
 var vt_t = evt.data;
 try {
   vt_tjson = JSON.parse(vt_t);
  console.log(vt_tjson);
console.log("hierboven?");
  } catch (e) {
    // error  
    // console.log('geen json format gezien');
    return;
  }
var vt_tJSONtostring = JSON.stringify(vt_tjson); //js tostring
document.getElementById("vt_html").innerHTML = vt_tjson.SDmax + ", " + vt_tjson.SDused;
document.getElementById("jsonhtml").innerHTML = vt_tJSONtostring;

//progressbar invullen sdmem
//var progressSDcard = document.getElementById("sdmem");
//alert(   vt_tjson.SDused);
//alert(   vt_tjson.SDmax);
document.getElementById("sdmem").value =  vt_tjson.SDused;
document.getElementById("sdmem").max =  vt_tjson.SDmax;
  var keys = Object.keys(vt_tjson);
  var waardes = Object.values(vt_tjson);
  var txt = "";
 txt += "<table border='1'>"
  for (var key in keys ) {
   txt += "<tr><td>" +  keys[key] + "</td><td>" +  waardes[key] + "</td></tr>";
      //  console.log(key + " -> " + keys[key]  + "-" + waardes[key]);
    }
    txt += "</table>" 
   document.getElementById("tabel").innerHTML = txt;
   
   
  // alert(   vt_tjson.Led);
   
   var e = document.getElementById('ledstatus');
    if (vt_tjson.Led === "1") {
      e.style.color = 'red';
    }
    else if (vt_tjson.Led === "0") {
      e.style.color = 'black';
    }
   
}


function start() {
  //websock = new WebSocket('ws://' + window.location.hostname + ':81/');
  websock = new WebSocket('ws://192.168.1.20:81/');
  
  websock.onopen = function(evt) { 
  console.log('websock open'); 
   wsjsonrecieved(evt);//parse json to html
  
  };
  
  websock.onclose = function(evt) { console.log('websock close'); };
  websock.onerror = function(evt) { console.log(evt); };
  websock.onmessage = function(evt) {
   // console.log(evt);
    var e = document.getElementById('ledstatus');
    if (true) {
      console.log('socket komt binnen event:');
      console.log(evt);
 wsjsonrecieved(evt);//parse updated json to html

    }
  };
}
function buttonclick(e) {
 // console.log ("export buttonklik js");
  websock.send(e.id);
 // console.log (e);
}



function keuzeclick(e) {
 // console.log ("export buttonklik js");
  websock.send(e.id + ":" + e.value);
  console.log (e.id + ":" + e.value);
}



//foto op deze site zetten  p(), j.src = `${c}/capture?_cb=${Date.now()}`, f(k)

function fotoclick(e) {
 // console.log ("export buttonklik js");
  websock.send(e.id  );
   console.log (e.id);
   
   console.log ('http://ipadresesp32cam/capture?');
 //   document.getElementById('fotostream').src =   'http://' + window.location.hostname + '/capture?';
     document.getElementById('fotostream').src =   'http://192.168.1.20/capture?_cb='+Math.floor(Math.random() * 100) + 1;
  
  
}

//einde foto op deze site zetten





function streamclick(e) {
 // console.log ("export buttonklik js");
  websock.send(e.id  );
   console.log (e.id);
 
// alert (  'http://192.168.1.20' + ':' + vt_tjson.Streamport +'/stream'  );
   
   console.log ('http://ipadresesp32cam/capture?');
 //   document.getElementById('fotostream').src =   'http://' + window.location.hostname + ':' + vt_tjson.SDmax +'/stream';
     document.getElementById('fotostream').src =   'http://192.168.1.20' + ':' + vt_tjson.Streamport +'/stream' ;
  
  
}



</script>
    </head>
   
   <body onload="javascript:start();">


<h2>ESP32-CAM JSON Websocket</h2>
v12345vtm<br>
<a href=" /"   style="color:red">main page settings</a>
<p id="jsonhtml"></p>
   <p id="fromsketch"></p>
    <p id="vt_html"></p>
    
   <div id="ledstatus"><b>LED</b></div>
<button id="ledon"  type="button" onclick="buttonclick(this);">On</button> 
<button id="ledoff" type="button" onclick="buttonclick(this);">Off</button>


         
                     
              
              
                  <div class="input-group" id="special_effect-group"><label  >timelapseinterval</label>
                                <select id="TLint" onchange="keuzeclick(this)" >
                                <option value="1" selected="selected">1m</option>
                                <option value="10">10</option>
                                <option value="20">20</option>
                                <option value="30">30</option>
                                <option value="40">40</option>
                <option value="50">50</option>
                <option value="60">60</option>
                            </select>
                        </div>
                   
   
<button id="Ba" onclick="fotoclick(this);">Ba Get Still</button>
<button id="Bb" onclick="streamclick(this);">Bb Start Stream</button>
<button id="Bc" onclick="buttonclick(this); ">Bc Save Image2SD</button>
<button id="Bd" onclick="buttonclick(this);" >Bd Startstop Timelapse2SD</button>                 

 <br> SDcard free space:<progress id="sdmem" value="50" max="100"></progress>
  
   <br>
   <img id="fotostream" src="" width="400" height="300">
  
  
    <p id="tabel"></p>
  
  

    </body>
</html>


























)rawliteral";
//////////////////////websocket template

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        webSocket.sendTXT(num, "hello from arduino sketch");
        
        makejsoncontainer (); //vith json string updaten
webSocket.sendTXT(num,  vith );//send 1e json to connected client        
        
      }
      break;
    case WStype_TEXT:
     {  //jump to case label [-fpermissive]
      Serial.printf("[%u] get Text: %s\r\n", num, payload);
 for (int i=0;i<length;i++) {    
 // string achter pointer to string doen
   payloadstring = payloadstring+ (char)payload[i];
  } 
int pos = payloadstring.indexOf(':');
 plkey  =  payloadstring.substring(0, pos);
 plvalue = payloadstring.substring(pos +1);
// Serial.print(plkey);
// Serial.print(" ++ " +plvalue);
// Serial.println("payloadsplitted");
 if (strcmp("ledon", (const char *)payload) == 0) {
        writeLED(true);
      }
      else if (strcmp("ledoff", (const char *)payload) == 0) {
        writeLED(false);
      }


    else if (strcmp("Ba", (const char *)payload) == 0) {
         Serial.println("Ba knop dedrukt");//get still
      }


   else if (strcmp("Bb", (const char *)payload) == 0) {
         Serial.println("Bb knop dedrukt");//start stream
      }


         else if (strcmp("Bc", (const char *)payload) == 0) {
         Serial.println("Bc knop dedrukt");//save2imagesd
       
         ImageToSd();//foto to sd

      }



         else if (strcmp("Bd", (const char *)payload) == 0) {
         Serial.println("Bd knop dedrukt create timer");//timelapse starten , creat timer
// https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/system/esp_timer.html

 // voor ons gemak resetten we de esp timer ??
 toggleTimelapse(); //toggle de timer die de timelapse doet
/////////////////////////////////////
         
      }

          else if (plkey.equals("TLint") ) {
            TLinterval =  plvalue.toInt();//TLint is een id van de htlm   ,  de socket stuurde TLint:60        
         Serial.println("tlint met value gekozen:"  + plvalue);//timelapse
      }
      
      else {
        Serial.println("Unknown command");
      }

payloadstring = "";
makejsoncontainer (); //vith json string updaten 
webSocket.sendTXT(num,  vith );//send updated  json to connected client

// send data to all connected clients 
char vithchar[300];
vith.toCharArray(vithchar, vith.length()+1);
 Serial.println(vith.length()+1); //126
 Serial.println("vithlengte letop 200bytes lengte voorzien");
 webSocket.broadcastTXT(vithchar ,vith.length());       
    //  webSocket.broadcastTXT(payload, length);
      break;  }//jump to case label [-fpermissive]
      
    case WStype_BIN:
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
 // echo data back to browser
      webSocket.sendBIN(num, payload, length);
      break;
      
    default:
      Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}
////////////////////einde websockettemplate

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
      Serial.printf("index webpage loading \n");

   return httpd_resp_send(req, (const char *)INDEX2_HTML, strlen(INDEX2_HTML));
}

static esp_err_t export_handler(httpd_req_t *req){
       httpd_resp_set_type(req, "text/html");
      Serial.printf("index webpage loading \n");

   return httpd_resp_send(req, (const char *)EXPORT_HTML, strlen(EXPORT_HTML));
}

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };
    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };
    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };
    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };
   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };
       httpd_uri_t export_uri = {
        .uri       = "/export",
        .method    = HTTP_GET,
        .handler   = export_handler,
        .user_ctx  = NULL
    };
     Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
         httpd_register_uri_handler(camera_httpd, &export_uri);
    }
  config.server_port =  streamportvalue.toInt() ; //9601;//Cint(streamportvalue)
    config.ctrl_port = streamportvalue.toInt() ; //9601;//Cint(streamportvalue)
     
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void printLocalTime()
{ 
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.print(&timeinfo, "%A, %B %d %Y %H:%M:%S");//Friday, February 22 2019 22:37:45
  //Serial.print(&timeinfo, "%y%m%d-%H%M%S"); //timestamp for pictures eg. 190222
}

/*
  %a Abbreviated weekday name
  %A Full weekday name
  %b Abbreviated month name
  %B Full month name
  %c Date and time representation for your locale
  %d Day of month as a decimal number (01-31)
  %H Hour in 24-hour format (00-23)
  %I Hour in 12-hour format (01-12)
  %j Day of year as decimal number (001-366)
  %m Month as decimal number (01-12)
  %M Minute as decimal number (00-59)
  %p Current locale's A.M./P.M. indicator for 12-hour clock
  %S Second as decimal number (00-59)
  %U Week of year as decimal number,  Sunday as first day of week (00-51)
  %w Weekday as decimal number (0-6; Sunday is 0)
  %W Week of year as decimal number, Monday as first day of week (00-51)
  %x Date representation for current locale
  %X Time representation for current locale
  %y Year without century, as decimal number (00-99)
  %Y Year with century, as decimal number
  %z %Z Time-zone name or abbreviation, (no characters if time zone is unknown)
  %% Percent sign
  You can include text literals (such as spaces and colons) to make a neater display or for padding between adjoining columns.
  You can suppress the display of leading zeroes  by using the "#" character  (%#d, %#H, %#I, %#j, %#m, %#M, %#S, %#U, %#w, %#W, %#y, %#Y)
*/

#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
  #else
#error "Camera model not selected"
#endif

//void startCameraServer();

static void writeLED(bool LEDon)
{
  LEDStatus = LEDon;
  // Note inverted logic for Adafruit HUZZAH board
  if (LEDon) {
    digitalWrite(LED_1, 0);
  }
  else {
    digitalWrite(LED_1, 1);
  }
}










void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout problems



 pinMode (LED_1, OUTPUT);//back led
  pinMode (4, OUTPUT);//flashled
  writeLED(false);
 Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
   Serial.println("v12345vtm \n");
   
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
    config.frame_size = FRAMESIZE_UXGA; 
    config.jpeg_quality = 10;
    config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
///////////websock
  webSocket.begin();
    webSocket.onEvent(webSocketEvent);
//einde websock

 ////////////////

//einde json



configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);//ntp timeserversetting

  printLocalTime();
   Serial.println("\n internet time ok \n");

   if(!SD_MMC.begin()){
        Serial.println("initCard Mount Failed");
    }
    else
    {
       Serial.println("SD_MMC.begin ok en flashled niet als output aan te sturen é");
      }
  
   uint8_t cardType = SD_MMC.cardType();
    if(cardType == CARD_NONE){
        Serial.println("No SD_MMC card attached");
    }
    Serial.print("SD_MMC Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
    listDir(SD_MMC, "/", 0);
//    createDir(SD_MMC, "/mydir");
//    listDir(SD_MMC, "/", 0);
//    removeDir(SD_MMC, "/mydir");
//    listDir(SD_MMC, "/", 2);
//    writeFile(SD_MMC, "/hello.txt", "Hello ");
//    appendFile(SD_MMC, "/hello.txt", "World!\n");
//    readFile(SD_MMC, "/hello.txt");
//    deleteFile(SD_MMC, "/foo.txt");
//    renameFile(SD_MMC, "/hello.txt", "/foo.txt");
//    readFile(SD_MMC, "/foo.txt");
//    testFileIO(SD_MMC, "/test.txt");
    Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
    Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));


sdmaxvalue = longlong2string (SD_MMC.totalBytes());
sdusedvalue = longlong2string (SD_MMC.usedBytes());

     
  startCameraServer();
 Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect , de stream zit op een andere poortkanaal 9601 ");
  Serial.print("stream Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println(":9601/stream ");
  Serial.print("image Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("/capture ");
   Serial.print("websocketport:81 Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("/export ");
}


void loop() {
   webSocket.loop();
   if (longlong2string(esp_timer_get_time()).equals(Timelapsecountervalue)  && tlrunningvalue)

{
  
  ImageToSd();  //time is verstreken
  resetTLcounter();
  
  }


   

}  //end loop
