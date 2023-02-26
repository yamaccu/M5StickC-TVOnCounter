#include <M5Unified.h>
#include <Avatar.h>
#include <CEC_Device.h>
#include <SPIFFS.h>

/// need ESP8266Audio library. ( URL : https://github.com/earlephilhower/ESP8266Audio/ )
#include <AudioOutput.h>
#include <AudioFileSourceSPIFFS.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>

#define IN_LINE 32           // M5StickC Grove
#define OUT_LINE 33          // M5StickC Grove
#define SPEAKER_ENABLE 25    // M5StickC Hat, LOW:5Vout Disable, HIGH:5Vout Enable

// M5Unified MP3 Player======================================
/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
int16_t outLevel;

/// set your mp3 filename
static constexpr const char* filename[] =
{
  "/hello.mp3",
  "/60min.mp3",
  "/90min.mp3",
  "/120min.mp3",
};
static constexpr const size_t filecount = sizeof(filename) / sizeof(filename[0]);

class AudioOutputM5Speaker : public AudioOutput
{
  public:
    AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0)
    {
      _m5sound = m5sound;
      _virtual_ch = virtual_sound_channel;
    }
    virtual ~AudioOutputM5Speaker(void) {};
    virtual bool begin(void) override { return true; }
    virtual bool ConsumeSample(int16_t sample[2]) override
    {
      if (_tri_buffer_index < tri_buf_size)
      {
        _tri_buffer[_tri_index][_tri_buffer_index  ] = sample[0];
        _tri_buffer[_tri_index][_tri_buffer_index+1] = sample[1];
        _tri_buffer_index += 2;

        return true;
      }

      flush();
      return false;
    }
    virtual void flush(void) override
    {
      if (_tri_buffer_index)
      {
        outLevel = abs(_tri_buffer[_tri_index][_tri_buffer_index]);  // lipsync用に追加
        _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, true, 1, _virtual_ch);
        _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
        _tri_buffer_index = 0;
      }
    }
    virtual bool stop(void) override
    {
      flush();
      _m5sound->stop(_virtual_ch);
      return true;
    }

    const int16_t* getBuffer(void) const { return _tri_buffer[(_tri_index + 2) % 3]; }

  protected:
    m5::Speaker_Class* _m5sound;
    uint8_t _virtual_ch;
    static constexpr size_t tri_buf_size = 1536;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
};

static AudioFileSourceSPIFFS file;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
static AudioGeneratorMP3 mp3;
static AudioFileSourceID3* id3 = nullptr;
static int header_height = 0;
static size_t fileindex = 0;

void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  (void)cbData;
  if (string[0] == 0) { return; }
  if (strcmp(type, "eof") == 0)
  {
    M5.Display.display();
    return;
  }
  int y = M5.Display.getCursorY();
  if (y+1 >= header_height) { return; }
  M5.Display.fillRect(0, y, M5.Display.width(), 12, M5.Display.getBaseColor());
  M5.Display.printf("%s: %s", type, string);
  M5.Display.setCursor(0, y+12);
}

void stop(void)
{
  if (id3 == nullptr) return;
  out.stop();
  mp3.stop();
  id3->RegisterMetadataCB(nullptr, nullptr);
  id3->close();
  file.close();
  delete id3;
  id3 = nullptr;
}

void play(const char* fname)
{
  if (id3 != nullptr) { stop(); }
  M5.Display.setCursor(0, 8);
  file.open(fname);
  id3 = new AudioFileSourceID3(&file);
  id3->RegisterMetadataCB(MDCallback, (void*)"ID3TAG");
  id3->open(fname);
  mp3.begin(id3, &out);
}

// avatar =============================================================
using namespace m5avatar;
Avatar avatar;

char text[25]; //吹き出し用

const Expression expressions[] = {
  Expression::Angry,
  Expression::Sleepy,
  Expression::Happy,
  Expression::Sad,
  Expression::Doubt,
  Expression::Neutral
};
const int expressionsSize = sizeof(expressions) / sizeof(Expression);
int idx = 0;

// CEC =============================================================
enum timeState
{
  time0minutes,
  time60minutes,
  time90minutes,
  time120minutes,
};

bool TVON = true;
bool isTVONCount = true;
unsigned long TVOnTimeCounter = 0;
unsigned long TVOffTimeCount = 0;
int FLG_ErapsedTime = time0minutes;
unsigned long timer = 0;
unsigned long now = 0;

class MyCEC: public CEC_Device {
  public:
    MyCEC(int physAddr): CEC_Device(physAddr,IN_LINE,OUT_LINE) { }

    void requestTVState() { unsigned char frame[2] = { 0x8F, 0x00 }; TransmitFrame(0x00, frame, sizeof(frame)); }
    
    void OnReceive(int source, int dest, unsigned char* buffer, int count) {
      Serial.println(count);
      if (count == 0) return;

      Serial.print("onReceive / ");
      for(int i = 0; i<sizeof(buffer); i++)
      {
        Serial.print(buffer[i]);
        Serial.print(":");
      }
      Serial.println();

      switch (buffer[0]) {
        case 0x90:   // res data: 00:90:00 or 01
          if(buffer[1] == 0x00)
          {
            TVON = true;
          }
          else
          {
            TVON = false;
          };
          break;

        default: CEC_Device::OnReceive(source,dest,buffer,count); break;
      }
    }
};

MyCEC device(0x1000);

// setup =============================================================
void setup(void)
{
  pinMode(SPEAKER_ENABLE, OUTPUT);
  digitalWrite(SPEAKER_ENABLE, LOW);

  SPIFFS.begin();
  auto cfg = M5.config();
  cfg.external_spk = true;    /// use external speaker (SPK HAT / ATOMIC SPK)
  M5.begin(cfg);
  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 96000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    M5.Speaker.config(spk_cfg);
  }
  M5.Speaker.begin();
  Serial.begin(115200);
  device.Initialize(CEC_LogicalDevice::CDT_PLAYBACK_DEVICE);
  timer = millis();

  M5.Lcd.setRotation(3);
  avatar.setScale(0.6);
  avatar.setPosition(0, 20);
  avatar.init(); // start drawing

  M5.Lcd.setBrightness(80);
  M5.Speaker.setVolume(60);

  device.requestTVState();
}

// loop ====================================================

void loop(void)
{
  now = millis();
  if((now - timer) > 10000)
  {
    timer = now;
    device.requestTVState();

    if(TVON)
    {
      M5.Lcd.setBrightness(80);
      avatar.setExpression(Expression::Neutral);
      if(isTVONCount)
      {
        TVOnTimeCounter += 10;    // 単位：sec
        sprintf(text, "%d minutes", TVOnTimeCounter / 60);
        avatar.setSpeechText(text);
      }
      TVOffTimeCount = 0;
      Serial.println(TVOnTimeCounter);
    }
    else
    {
      avatar.setExpression(Expression::Sleepy);
      TVOffTimeCount += 10;
      // テレビOFF後30分でカウントクリア & 画面OFF
      if(TVOffTimeCount >= 1800)
      {
        M5.Lcd.setBrightness(0);
        TVOnTimeCounter = 0;
        FLG_ErapsedTime = time0minutes;
        avatar.setSpeechText("");
      }
    }

    if(TVOnTimeCounter >= 3600 && FLG_ErapsedTime == time0minutes)
    {
      digitalWrite(SPEAKER_ENABLE, HIGH);
      FLG_ErapsedTime = time60minutes;
      stop();
      play(filename[time60minutes]);
      //Serial.println("1時間経過");
    }
    else if(TVOnTimeCounter >= 5400 && FLG_ErapsedTime == time60minutes)
    {
      digitalWrite(SPEAKER_ENABLE, HIGH);
      FLG_ErapsedTime = time90minutes;
      stop();
      play(filename[time90minutes]);
      //Serial.println("1時間半経過");
    }
    else if(TVOnTimeCounter >= 7200 && FLG_ErapsedTime == time90minutes)
    {
      digitalWrite(SPEAKER_ENABLE, HIGH);
      FLG_ErapsedTime = time120minutes;
      stop();
      play(filename[time120minutes]);
      //Serial.println("2時間経過");
    }
  }

  device.Run();

  if (mp3.isRunning())
  {
    // lipsync
    float f = outLevel / 9000.0;
    avatar.setMouthOpenRatio(f);
    Serial.println(outLevel);
    if (!mp3.loop()) { mp3.stop(); }
  }
  else
  {
    avatar.setMouthOpenRatio(0);
    digitalWrite(SPEAKER_ENABLE, LOW);
  }

  M5.update();
  if (M5.BtnA.wasClicked())
  {
    // カウント一時停止/再開
    isTVONCount = !isTVONCount;
    if(isTVONCount)
    {
      sprintf(text, "%d minutes", TVOnTimeCounter / 60);
      avatar.setSpeechText(text);
    }
    else
    {
      avatar.setSpeechText("stop");
    }
  }
}