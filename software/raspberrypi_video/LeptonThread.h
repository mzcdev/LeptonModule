#ifndef TEXTTHREAD
#define TEXTTHREAD

#include <ctime>
#include <pwd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <QThread>
#include <QtCore>
#include <QPixmap>
#include <QImage>

#define PACKET_SIZE 164
#define PACKET_SIZE_UINT16 (PACKET_SIZE / 2)
#define PACKETS_PER_FRAME 60
#define FRAME_SIZE_UINT16 (PACKET_SIZE_UINT16 * PACKETS_PER_FRAME)

class LeptonThread : public QThread
{
  Q_OBJECT;

public:
  LeptonThread();
  ~LeptonThread();

  void setLogLevel(uint16_t);
  void setMirror(bool);
  void setAutoCapture(bool);
  void useColormap(int);
  void useLepton(int);
  void useSpiSpeedMhz(unsigned int);
  void setAutomaticScalingRange();
  void useRangeBaseValue(uint16_t);
  void useRangeMinValue(uint16_t);
  void useRangeMaxValue(uint16_t);
  void run();

public slots:
  void capture();
  void performFFC();

signals:
  void updateText(QString);
  void updateImage(QImage);

private:
  void log_message(uint16_t, std::string);
  uint16_t loglevel;
  bool mirror;
  bool auto_capture;
  bool uploading;
  int typeColormap;
  const int *selectedColormap;
  int selectedColormapSize;
  int typeLepton;
  unsigned int spiSpeed;
  bool autoRangeMin;
  bool autoRangeMax;
  uint16_t rangeBase;
  uint16_t rangeMin;
  uint16_t rangeMax;
  int myImageWidth;
  int myImageHeight;
  float minCelsius;
  float maxCelsius;
  float convertCelsius(float);
  QImage myImage;
  QString myString;

  uint8_t result[PACKET_SIZE * PACKETS_PER_FRAME];
  uint8_t shelf[4][PACKET_SIZE * PACKETS_PER_FRAME];
  uint16_t *frameBuffer;
};

#endif
