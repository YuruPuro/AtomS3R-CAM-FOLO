/**
 * AtomS3R CAM で6足歩行 プログラミングFOLOを操作
 * 公式のWebカメラのスケッチにAtomS3でFOLOを操作するスケッチを合体しています。
 **/

#include "camera_pins.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include "esp_camera.h"

//#define DEBUG

// ----- FOLO用の定義 -----
// --- Micro:bitのピンとAtomS3のピン変換
#define MB_PIN1 7 
#define MB_PIN2 8
#define MB_PIN12 1 
#define MB_PIN13 5 
#define MB_PIN14 6 
#define MB_PIN15 39 
#define MB_PIN16 38

// --- ボタンステータス
#define BTN_STAT_OFF 0
#define BTN_STAT_A 1
#define BTN_STAT_B 2
#define BTN_STAT_AB 3

// --- モニターの動作モード
#define MOVE_MODE_AUTO true
#define MOVE_MODE_MANUAL false
#define MOVE_ACTION_STOP 0
#define MOVE_ACTION_MS 1
#define MOVE_ACTION_F 2
#define MOVE_ACTION_B 3
#define MOVE_ACTION_RS 4
#define MOVE_ACTION_R 5
#define MOVE_ACTION_L 6

bool moveMode = MOVE_MODE_MANUAL ;
int moveAction = MOVE_ACTION_STOP ;
int btnMode = BTN_STAT_OFF ;
int delayTime = 0 ;
int delayStart = 0 ;

// ----- Webカメラ用の定義 -----
//#define STA_MODE
#define AP_MODE

#ifdef STA_MODE
const char* ssid     = "SSID";
const char* password = "PASSWORD";
#else
const char* ssid     = "AtomS3R";
const char* password = "";
#endif

WebServer server(80);
camera_fb_t* fb    = NULL;
uint8_t* out_jpg   = NULL;
size_t out_jpg_len = 0;

// Webブラウザに表示するHTML
const char html[] =
"<!DOCTYPE html>\
<html lang='ja'>\
<head>\
<meta name='viewport' content='width=device-width, initial-scale=1.0'>\
<meta charset='UTF-8'>\
<style>\
  body { display: flex; flex-direction: column; align-items: center; justify-content: flex-start; height: 100%; overflow: hidden; margin: 0; }\
  div { order: 1; width: 100%; overflow: hidden; }\
  input { padding: 10px; font-size: 16px; background-color: #4CAF50; color: #fff; border: none; border-radius: 5px; cursor: pointer; margin: 6px; }\
  div.controls { order: 3; font-size: 6vw; color: black; text-align: center; width: 90%; margin: 0 auto; padding: 0; }\
</style>\
<title>WiFi_Car Controller</title>\
<script>\
  function pushButton(cmd) {\
    var url = '/control?name='+cmd+'&T='+ new Date().getTime();\
    var ajax = new XMLHttpRequest;\
    ajax.open('GET', url, true);\
    ajax.onload = function (data) {\
       var repos = data.currentTarget.response;\
    };\
    ajax.send(null);\
  }\
  function updateCameraStream() {\
    clearInterval(tid );\
    var img = document.getElementById('camera-stream');\
    img.src = '/jpeg?t=' + new Date().getTime();\
    img.onload = function(){\
      tid = setInterval(updateCameraStream, 10);\
    } ;\
  }\
  var tid = setInterval(updateCameraStream, 250);\
</script>\
</head>\
<body>\
<div align='center'><img id='camera-stream' src=''  width='320' height='240'></div>\
<div class='controls'>AtomS3R-CAM FOLO\
    <form method='post' action='/control'>\
      <input type='button' value='前進' onclick=pushButton('F')><br>\
      <input type='button' value='左旋回' onclick=pushButton('L');>\
      <input type='button' value='停止' onclick=pushButton('S')>\
      <input type='button' value='右旋回' onclick=pushButton('R');><br>\
      <input type='button' value='自動' onclick=pushButton('A')>\
      <input type='button' value='後退' onclick=pushButton('B')>\
      <input type='button' value='手動' onclick=pushButton('M')><br>\
    </form>\
  </div>\
</body>\
</html>";

// カメラの設定値　※公式のサンプルスケッチから流用
static camera_config_t camera_config = {
    .pin_pwdn     = PWDN_GPIO_NUM,
    .pin_reset    = RESET_GPIO_NUM,
    .pin_xclk     = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7       = Y9_GPIO_NUM,
    .pin_d6       = Y8_GPIO_NUM,
    .pin_d5       = Y7_GPIO_NUM,
    .pin_d4       = Y6_GPIO_NUM,
    .pin_d3       = Y5_GPIO_NUM,
    .pin_d2       = Y4_GPIO_NUM,
    .pin_d1       = Y3_GPIO_NUM,
    .pin_d0       = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href  = HREF_GPIO_NUM,
    .pin_pclk  = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format  = PIXFORMAT_RGB565,
    .frame_size    = FRAMESIZE_QVGA,
    .jpeg_quality  = 0,
    .fb_count      = 2,
    .fb_location   = CAMERA_FB_IN_PSRAM,
    .grab_mode     = CAMERA_GRAB_LATEST,
    .sccb_i2c_port = 0,
};

// ========== WebServerの機能 =====

/**
 * ルートページの表示　HTMLコンテンツを返す)
 */
void handleRoot() {
    String message = "ROOT\nURI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(200, "text/html", html);
}

/**
 * 操作コマンドの受付
 * Ajaxでの利用を想定していおり、空コンテンツを応答を返す。
 * 表示すると真っ白ページになるので注意
 */
void handleControl() {
    String message = "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " [" + server.argName(i) + "]: [" + server.arg(i) + "]\n";
      if (server.argName(i) == "name") {
         if (server.arg(i) == "F") {
            moterControl(MOVE_ACTION_F) ;
         } else
         if (server.arg(i) == "L") {
            moterControl(MOVE_ACTION_L) ;
         }
         if (server.arg(i) == "S") {
            moterControl(MOVE_ACTION_STOP) ;
         } else
         if (server.arg(i) == "R") {
            moterControl(MOVE_ACTION_R) ;
         } else
         if (server.arg(i) == "B") {
            moterControl(MOVE_ACTION_B) ;
         } else
         if (server.arg(i) == "M") {
            moterControl(MOVE_ACTION_STOP) ;
            moveMode = MOVE_MODE_MANUAL ;
         } else
         if (server.arg(i) == "A") {
            moterControl(MOVE_ACTION_F) ;
            moveMode = MOVE_MODE_AUTO ;
         }
      }
    }
    server.send(200, "text/html", html);
}

/**
 * 意味があるのか分からないけど 404エラー処理
 */
void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

/**
 * JPEG画像を返す。
 */
void sendJpeg( ) {
    fb = esp_camera_fb_get();
    if (fb) {
        frame2jpg(fb, 255, &out_jpg, &out_jpg_len);
  
        esp_camera_fb_return(fb);
        // REF: void send_P(int code, PGM_P content_type, PGM_P content, size_t contentLength);
        server.send_P(200, "image/jpeg", (PGM_P)out_jpg, out_jpg_len);
        fb = NULL;
        if (out_jpg) {
            free(out_jpg);
            out_jpg     = NULL;
            out_jpg_len = 0;
        }
    }
}

// ========== フォロの制御機能 =====

/**
 * 自動操作(プログラム動作)
 */
void FOLO() {
    // モーターを一定時間動かし続けるためのタイマー処理
    if (delayTime > 0) {
      if (delayTime <= millis() - delayStart) {
        delayTime = 0 ;
      }
      return ;
    }

    // --- IR Senser の読み取り
    int senserRight = analogRead(MB_PIN1) ;
    int senserLeft  = analogRead(MB_PIN2) ;
#ifdef DEBUG
      Serial.print("FOLO senser:");
      Serial.print(senserLeft);
      Serial.print(",");
      Serial.println(senserRight);
#endif

    // --- 自動モードで動作 ---  
    if (moveAction == MOVE_ACTION_R) {
        // 右に旋回動作中：障害物が確認できなくなったら停止
        if (senserRight < 600) {
          moterControl(MOVE_ACTION_RS) ;
          moveAction = MOVE_ACTION_F ;
          moterControl(moveAction) ;
        }
    } else
    if (moveAction == MOVE_ACTION_L) {
        // 右に旋回動作中：障害物が確認できなくなったら停止
        if (senserLeft < 600) {
          moterControl(MOVE_ACTION_RS) ;
          moveAction = MOVE_ACTION_F ;
          moterControl(moveAction) ;
        }
    } else
    if (senserRight > 1000 && senserLeft > 1000) {
        // 障害物が前面にあり右回転
        moterControl(MOVE_ACTION_MS) ;
        moveAction = MOVE_ACTION_R ;
        moterControl(moveAction) ;
        delayTime = 400 ;
    } else
    if (senserRight > 1000) {
        // 左に旋回
        moveAction = MOVE_ACTION_L ;
        moterControl(moveAction) ;
        delayTime = 400 ;
    } else
    if (senserLeft > 1000) {
        // 右に旋回
        moveAction = MOVE_ACTION_R ;
        moterControl(moveAction) ;
        delayTime = 400 ;
    } else {
        moveAction = MOVE_ACTION_F ;
        moterControl(moveAction) ;
    }
}

/**
 * モーターの操作
 * action : 動作モード
 */
void moterControl(int action) {
  switch(action) {
    case MOVE_ACTION_STOP: // 完全停止
      Serial.println("MOVE_ACTION_STOP");
      digitalWrite(MB_PIN13,LOW) ;
      digitalWrite(MB_PIN14,LOW) ;
      digitalWrite(MB_PIN15,LOW) ;
      digitalWrite(MB_PIN16,LOW) ;
      break ;
    case MOVE_ACTION_MS:  // 前進・後進の停止
#ifdef DEBUG
      Serial.println("MOVE_ACTION_MS");
#endif
      digitalWrite(MB_PIN13,LOW) ;
      digitalWrite(MB_PIN14,LOW) ;
      break ;
    case MOVE_ACTION_F: // 前進
      Serial.println("MOVE_ACTION_F");
      digitalWrite(MB_PIN13,HIGH) ;
      digitalWrite(MB_PIN14,LOW) ;
      break ;
    case MOVE_ACTION_B: // 後退
      Serial.println("MOVE_ACTION_B");
      digitalWrite(MB_PIN13,LOW) ;
      digitalWrite(MB_PIN14,HIGH) ;
      break ;
    case MOVE_ACTION_RS: // 旋回の停止
      Serial.println("MOVE_ACTION_RS");
      digitalWrite(MB_PIN15,LOW) ;
      digitalWrite(MB_PIN16,LOW) ;
      break ;
    case MOVE_ACTION_L: // 左旋回
      Serial.println("MOVE_ACTION_L");
      digitalWrite(MB_PIN15,HIGH) ;
      digitalWrite(MB_PIN16,LOW) ;
      break ;
    case MOVE_ACTION_R: // 右旋回
      Serial.println("MOVE_ACTION_R");
      digitalWrite(MB_PIN15,LOW) ;
      digitalWrite(MB_PIN16,HIGH) ;
      break ;
  }
}

/**
 * タクトスイッチの読み取り
 */
void checkButton( ) {
    // --- Push BUTTON : 自動モードの手動切り替え ---
    int readValue = analogRead(G2) ;
    int btn = BTN_STAT_OFF ;
    if (readValue > 3500) btn = BTN_STAT_OFF ;
    else if (readValue > 2500) btn = BTN_STAT_A ;
    else if (readValue > 1800) btn = BTN_STAT_B ;
    else btn = BTN_STAT_AB ;

    if (btnMode != btn) {
      btnMode = btn ;
      switch(btn) {
        case BTN_STAT_A :
            moveMode = MOVE_MODE_AUTO  ;
            moterControl(MOVE_ACTION_F) ;
            break ;
        case BTN_STAT_B :
            moveMode = MOVE_MODE_MANUAL ;
            moterControl(MOVE_ACTION_STOP) ;
            break ;
      }
    }  
}

// ========== SETUP ===
void setup() {
    Serial.begin(115200);
    pinMode(POWER_GPIO_NUM, OUTPUT);
    digitalWrite(POWER_GPIO_NUM, LOW);
    delay(500);

    // --- FOLO センサー/モータドライバ設定 ---
    // IR Senser
    pinMode(MB_PIN12,OUTPUT) ;
    digitalWrite(MB_PIN12,HIGH) ;

    // MOVE MOTER
    pinMode(MB_PIN13,OUTPUT) ;
    pinMode(MB_PIN14,OUTPUT) ;
    digitalWrite(MB_PIN13,LOW) ;
    digitalWrite(MB_PIN14,LOW) ;

    // ROTATE MOTER
    pinMode(MB_PIN15,OUTPUT) ;
    pinMode(MB_PIN16,OUTPUT) ;
    digitalWrite(MB_PIN15,LOW) ;
    digitalWrite(MB_PIN16,LOW) ;

    // --- カメラ設定 ---
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        Serial.println("Camera Init Fail");
        delay(1000);
        esp_restart();
    } else {
        Serial.println("Camera Init Success");
    }
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL) {
        Serial.println("sensor Addr Fail");
        delay(1000);
        esp_restart();
    }
    if (sensor->set_hmirror(sensor, true) != ESP_OK) {
        Serial.println("Camera set_hmirror Fail");
        delay(1000);
        esp_restart();
    }
    if (sensor->set_vflip(sensor, true) != ESP_OK) {
        Serial.println("Camera set_vflip Fail");
        delay(1000);
        esp_restart();
    }
    delay(100);

    // --- WiFi設定 ---
#ifdef STA_MODE
    Serial.println("Setup STA_MODE");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    Serial.println("");

    Serial.print("Connecting to ");
    Serial.println(ssid);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
#endif

#ifdef AP_MODE
    Serial.println("Setup AP_MODE");
    if (!WiFi.softAP(ssid, password)) {
        log_e("Soft AP creation failed.");
        while (1);
    }

    Serial.println("AP SSID:");
    Serial.println(ssid);
    Serial.println("AP PASSWORD:");
    Serial.println(password);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
#endif

    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/jpeg", sendJpeg);
    server.onNotFound(handleNotFound);

    server.begin();
}

// ========== LOOP =====
void loop() {
    // --- FOLO制御 ---
    checkButton( ) ;
    if (moveMode) {
      FOLO( ) ;
    } else {
      // マニュアルモード時の非常ブレーキ
      // --- IR Senser Test
      int senserRight = analogRead(MB_PIN1) ;
      int senserLeft  = analogRead(MB_PIN2) ;
#ifdef DEBUG
      Serial.print("CAM senser:");
      Serial.print(senserLeft);
      Serial.print(",");
      Serial.println(senserRight);
#endif
      if (senserRight > 1000 && senserLeft > 1000) {
        // 障害物が前面にあり停止
        moterControl(MOVE_ACTION_MS) ;
      }
    }

    // --- Webクライアント ---  
    server.handleClient();
    delay(2);//allow the cpu to switch to other tasks
}
