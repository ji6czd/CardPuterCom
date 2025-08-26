#include <M5Cardputer.h>
#include <SPIFFS.h>

#include <list>

const char* SoundFile[] = {
    "/cat.wav", "/dog.wav", "/cow.wav", "/sheep.wav", "/rooster.wav",
};

struct NotificationSound_T {
  const float freq;
  const int duration;
};

NotificationSound_T const NotificationSound[] = {
    {2000, 5}, {1500, 5}, {2500, 5}, {2000, 100}};

// 配列サイズを定数として定義
constexpr size_t SOUND_FILE_COUNT = sizeof(SoundFile) / sizeof(SoundFile[0]);
constexpr size_t NOTIFICATION_SOUND_COUNT =
    sizeof(NotificationSound) / sizeof(NotificationSound[0]);

// 異常終了したときにESP32をリスタートします。
void fail() {
  // リスタートに失敗したとしてもリスタートを繰り返す
  while (1) {
    Serial.println("Fail!");
    ESP.restart();
  }
}

void initialize() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Speaker.setVolume(32);

  // ディスプレイの初期化
  M5Cardputer.Display.begin();
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.println("Hello!");

  // SPIFFSの初期化
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
    fail();
  }
}

void playNotification(int type) {
  if (type < 0 || type >= NOTIFICATION_SOUND_COUNT) {
    return;
  }
  const NotificationSound_T sound = NotificationSound[type];
  M5Cardputer.Speaker.tone(sound.freq, sound.duration);
}

void playFile(const char* fileName) {
  if (!SPIFFS.exists(fileName)) {
    fail();
  }
  File wavFile = SPIFFS.open(fileName, "r");
  if (!wavFile) {
    fail();
  }

  size_t fileSize = wavFile.size();
  uint8_t* wavData = new uint8_t[fileSize];
  if (!wavData) {
    fail();
  }

  wavFile.read(wavData, fileSize);
  wavFile.close();
  M5Cardputer.Speaker.playWav(wavData, fileSize);
  delete[] wavData;
}

void keyClick(const m5::Keyboard_Class::KeysState& key) {
  if (key.enter)
    playNotification(1);
  else if (key.word.empty())
    playNotification(2);
  else
    playNotification(0);
}
void keyInput(m5::Keyboard_Class::KeysState& keys_status) {
  if (M5Cardputer.Keyboard.isChange()) {
    if (M5Cardputer.Keyboard.isPressed()) {
      keys_status = M5Cardputer.Keyboard.keysState();
      keyClick(keys_status);
    }
  }
}

String line;

void sendLine() {
  if (line.isEmpty()) return;
  line.clear();
}

void keyProcess(const m5::Keyboard_Class::KeysState& keys_status) {
  if (keys_status.enter) {
    sendLine();
  }
  if (keys_status.word.empty()) return;

  char key = keys_status.word[0];
  line += key;
}

// BLEを使って文字列を送信する
void setup() {
  initialize();
  // playFile(SoundFile[0]);
}

void loop() {
  M5Cardputer.update();
  // キーボードからの入力をチェック
  if (M5Cardputer.BtnA.wasPressed()) {
    playFile(SoundFile[0]);
  }
  m5::Keyboard_Class::KeysState keys_status;
  keyInput(keys_status);
  keyProcess(keys_status);
  delay(10);
}
