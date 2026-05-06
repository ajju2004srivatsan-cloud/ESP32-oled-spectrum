#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

// ─── OLED ──────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ─── I2S MIC PINS ──────────────────────────────────────────
#define I2S_WS   17
#define I2S_SD   15
#define I2S_SCK  18
#define I2S_PORT I2S_NUM_0

// ─── FFT ───────────────────────────────────────────────────
#define SAMPLES        512
#define SAMPLING_FREQ  40000

double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLES, SAMPLING_FREQ);

// ─── SPECTRUM BARS ─────────────────────────────────────────
#define NUM_BANDS   8
#define BAR_WIDTH  14
#define BAR_GAP     2
#define MAX_BAR_H  52

float barHeights[NUM_BANDS] = {0};
float barPeaks[NUM_BANDS]   = {0};
float peakFall[NUM_BANDS]   = {0};

#define SMOOTHING       0.65f
#define SILENCE_DECAY   0.72f
#define ATTACK_SPEED    0.35f
#define NOISE_FLOOR     0.025f

// ─── LOGARITHMIC BAND BOUNDARIES (Hz) ──────────────────────
// Spread evenly on a log scale so low bands aren't crammed
// into a tiny slice of noisy bins
int bandFreqs[NUM_BANDS + 1] = {
  200, 400, 800, 1200, 2000, 4000, 8000, 14000, 20000
};

// ─── PER-BAND SENSITIVITY SCALE ────────────────────────────
// Lower values suppress a band, higher values boost it.
// Low bands intentionally dampened to fight mic LF noise.
float bandGain[NUM_BANDS] = {
  0.3f,   // Band 0: 200–400 Hz   (sub-bass — very suppressed)
  0.5f,   // Band 1: 400–800 Hz   (bass — suppressed)
  0.75f,  // Band 2: 800–1200 Hz  (low-mid — slight damp)
  1.0f,   // Band 3: 1200–2000 Hz (mid)
  1.1f,   // Band 4: 2000–4000 Hz (upper-mid)
  1.2f,   // Band 5: 4000–8000 Hz (presence)
  1.1f,   // Band 6: 8000–14000 Hz (brilliance)
  0.9f    // Band 7: 14000–20000 Hz (air)
};

// ─── DC OFFSET TRACKING ────────────────────────────────────
float dcOffset = 0.0f;

// ───────────────────────────────────────────────────────────
void i2s_install() {
  i2s_config_t i2s_config = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLING_FREQ,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 512,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0
  };
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin() {
  i2s_pin_config_t pin_config = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };
  i2s_set_pin(I2S_PORT, &pin_config);
}

// ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306 not found!");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 25);
  display.println("Spectrum Ready!");
  display.display();
  delay(1500);

  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
}

// ───────────────────────────────────────────────────────────
void readMicData() {
  int32_t rawSamples[SAMPLES];
  size_t bytesRead = 0;

  i2s_read(I2S_PORT, rawSamples, sizeof(rawSamples), &bytesRead, portMAX_DELAY);

  int samplesRead = bytesRead / sizeof(int32_t);

  // Step 1: convert to float and compute mean (DC offset)
  float sum = 0;
  for (int i = 0; i < samplesRead; i++) {
    float s = (float)(rawSamples[i] >> 8) / 8388608.0f;
    sum += s;
  }
  float mean = sum / samplesRead;

  // Step 2: slowly track DC offset with IIR filter
  dcOffset = 0.95f * dcOffset + 0.05f * mean;

  // Step 3: fill FFT buffer with DC-removed samples
  for (int i = 0; i < SAMPLES; i++) {
    if (i < samplesRead) {
      float s = (float)(rawSamples[i] >> 8) / 8388608.0f;
      vReal[i] = (double)(s - dcOffset);   // remove DC bias
    } else {
      vReal[i] = 0.0;
    }
    vImag[i] = 0.0;
  }
}

// ───────────────────────────────────────────────────────────
void computeFFT() {
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();
}

// ───────────────────────────────────────────────────────────
void mapToBands() {
  float freqResolution = (float)SAMPLING_FREQ / SAMPLES;  // ~78 Hz per bin

  // Compute RMS to detect silence (skip DC bin 0 and 1)
  float rms = 0;
  for (int i = 2; i < SAMPLES / 2; i++) {
    rms += vReal[i] * vReal[i];
  }
  rms = sqrt(rms / (SAMPLES / 2));

  bool isSilent = (rms < NOISE_FLOOR);

  for (int b = 0; b < NUM_BANDS; b++) {
    int startBin = (int)(bandFreqs[b]     / freqResolution);
    int endBin   = (int)(bandFreqs[b + 1] / freqResolution);

    if (startBin < 2)           startBin = 2;   // always skip DC bins
    if (endBin > SAMPLES / 2)   endBin   = SAMPLES / 2;

    if (isSilent) {
      // Fast fall to zero
      barHeights[b] *= SILENCE_DECAY;
      if (barHeights[b] < 0.5f) barHeights[b] = 0;

      peakFall[b] += 1.5f;
      barPeaks[b] -= peakFall[b];
      if (barPeaks[b] < 0) barPeaks[b] = 0;

    } else {
      // Average (not max) magnitude across the band — reduces LF spike dominance
      float bandSum = 0;
      int   binCount = 0;
      for (int i = startBin; i <= endBin; i++) {
        bandSum += vReal[i];
        binCount++;
      }
      float avgMag = (binCount > 0) ? (bandSum / binCount) : 0;

      // Apply per-band gain to balance low vs high frequencies
      float scaled = avgMag * 300.0f * bandGain[b];
      if (scaled > MAX_BAR_H) scaled = MAX_BAR_H;
      if (scaled < 0)         scaled = 0;

      // Fast attack, slower decay
      if (scaled > barHeights[b]) {
        barHeights[b] = (ATTACK_SPEED * barHeights[b]) + ((1.0f - ATTACK_SPEED) * scaled);
      } else {
        barHeights[b] = (SMOOTHING * barHeights[b]) + ((1.0f - SMOOTHING) * scaled);
      }

      // Peak dot
      if (barHeights[b] >= barPeaks[b]) {
        barPeaks[b] = barHeights[b];
        peakFall[b] = 0;
      } else {
        peakFall[b] += 0.3f;
        barPeaks[b] -= peakFall[b];
        if (barPeaks[b] < 0) barPeaks[b] = 0;
      }
    }
  }
}

// ───────────────────────────────────────────────────────────
void drawSpectrum() {
  display.clearDisplay();

  int totalWidth = NUM_BANDS * (BAR_WIDTH + BAR_GAP) - BAR_GAP;
  int startX     = (SCREEN_WIDTH - totalWidth) / 2;
  int baseY      = SCREEN_HEIGHT - 1;

  for (int b = 0; b < NUM_BANDS; b++) {
    int x     = startX + b * (BAR_WIDTH + BAR_GAP);
    int h     = (int)barHeights[b];
    int peakY = baseY - (int)barPeaks[b];

    if (h < 1) h = 1;  // always show tiny stub

    display.fillRect(x, baseY - h, BAR_WIDTH, h, SSD1306_WHITE);

    if (peakY < baseY - 2) {
      display.drawFastHLine(x, peakY, BAR_WIDTH, SSD1306_WHITE);
    }
  }

  display.display();
}

// ───────────────────────────────────────────────────────────
void loop() {
  readMicData();
  computeFFT();
  mapToBands();
  drawSpectrum();
}